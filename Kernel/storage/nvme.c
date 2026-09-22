#include "storage/nvme.h"
#include "storage/partition.h"
#include "fs/block/bdev_table.h"
#include "pci/pci.h"
#include "arch/x86_64/mmio.h"
#include "mm/pmm.h"
#include "mm/page.h"
#include "kio.h"

#define NVME_CAP   0x0000u
#define NVME_VS    0x0008u
#define NVME_CC    0x0014u
#define NVME_CSTS  0x001Cu
#define NVME_AQA   0x0024u
#define NVME_ASQ   0x0028u
#define NVME_ACQ   0x0030u

#define NVME_CC_EN (1u << 0)
#define NVME_CSTS_RDY (1u << 0)

#define NVME_QSIZE 16u
#define NVME_MAX_CTRL 4u

struct NvmeSqe {
    uint32_t cdw0;
    uint32_t nsid;
    uint64_t rsvd;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
};

struct NvmeCqe {
    uint32_t dw0;
    uint32_t rsvd;
    uint16_t sqhd;
    uint16_t sqid;
    uint16_t cid;
    uint16_t status;
};

struct NvmeCtrl {
    RoboPciDevice *pci;
    volatile uint8_t *bar;
    uint32_t doorbellStride;
    uint32_t maxQentries;
    struct NvmeSqe *asq;
    struct NvmeCqe *acq;
    struct NvmeSqe *iosq;
    struct NvmeCqe *iocq;
    PhysAddr asqPa, acqPa, iosqPa, iocqPa, idPa, ioBufPa;
    uint8_t *idBuf;
    uint8_t *ioBuf;
    uint16_t asqTail, acqHead, iosqTail, iocqHead;
    uint8_t acqPhase, iocqPhase;
    uint16_t nextCid;
    uint64_t nsSize;
    uint32_t lbaSize;
    int ready;
    RoboBlockDevice bdev;
    char name[RoboBdevNameMax];
};

static struct NvmeCtrl gNvme[NVME_MAX_CTRL];
static unsigned gNvmeN;
static unsigned gNvmeDisks;

static uint32_t NR32(volatile uint8_t *b, uint32_t off)
{
    return MmioR32(b + off);
}

static uint64_t NR64(volatile uint8_t *b, uint32_t off)
{
    return MmioR64(b + off);
}

static void NW32(volatile uint8_t *b, uint32_t off, uint32_t v)
{
    MmioW32(b + off, v);
}

static void NW64(volatile uint8_t *b, uint32_t off, uint64_t v)
{
    MmioW64(b + off, v);
}

static void DelaySpins(unsigned n)
{
    volatile unsigned i;
    for (i = 0; i < n; i++)
        __asm__ volatile ("pause");
}

static void MemZero(void *p, unsigned n)
{
    unsigned char *d = (unsigned char *)p;
    while (n--)
        *d++ = 0;
}

static void MemCopy(void *dst, const void *src, unsigned n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
        *d++ = *s++;
}

static void *DmaPage(PhysAddr *pa)
{
    PhysAddr p = PageAlloc();
    uint8_t *v;
    unsigned i;
    if (p == RoboInvalidPhysAddr)
        return 0;
    v = (uint8_t *)(uintptr_t)p;
    for (i = 0; i < RoboPageSize; i++)
        v[i] = 0;
    if (pa)
        *pa = p;
    return v;
}

static volatile uint32_t *Doorbell(struct NvmeCtrl *c, unsigned qid, int cq)
{
    uint32_t off = 0x1000u + ((2u * qid + (cq ? 1u : 0u)) *
                               (4u << c->doorbellStride));
    return (volatile uint32_t *)(c->bar + off);
}

static int SubmitWait(struct NvmeCtrl *c, struct NvmeSqe *sq,
                      struct NvmeCqe *cq, uint16_t *tail, uint16_t *head,
                      uint8_t *phase, PhysAddr sqPa, int admin,
                      struct NvmeSqe *cmd)
{
    uint16_t cid = c->nextCid++;
    unsigned spins = 0;
    struct NvmeCqe *entry;
    volatile uint32_t *db;

    (void)sqPa;
    MemCopy(&sq[*tail], cmd, sizeof(*cmd));
    sq[*tail].cdw0 = (sq[*tail].cdw0 & 0xFFFF0000u) | cid;
    *tail = (uint16_t)((*tail + 1u) % NVME_QSIZE);
    db = Doorbell(c, admin ? 0u : 1u, 0);
    *db = *tail;

    for (;;) {
        entry = &cq[*head];
        if (((entry->status >> 0) & 1u) == *phase) {
            uint16_t st = (uint16_t)(entry->status >> 1);
            *head = (uint16_t)((*head + 1u) % NVME_QSIZE);
            if (*head == 0)
                *phase ^= 1u;
            db = Doorbell(c, admin ? 0u : 1u, 1);
            *db = *head;
            return st == 0 ? 0 : -1;
        }
        if (spins++ > 5000000u)
            return -1;
        DelaySpins(20);
    }
}

static int AdminCmd(struct NvmeCtrl *c, struct NvmeSqe *cmd)
{
    return SubmitWait(c, c->asq, c->acq, &c->asqTail, &c->acqHead,
                      &c->acqPhase, c->asqPa, 1, cmd);
}

static int IoCmd(struct NvmeCtrl *c, struct NvmeSqe *cmd)
{
    return SubmitWait(c, c->iosq, c->iocq, &c->iosqTail, &c->iocqHead,
                      &c->iocqPhase, c->iosqPa, 0, cmd);
}

static int NvmeDisable(struct NvmeCtrl *c)
{
    uint32_t cc = NR32(c->bar, NVME_CC);
    unsigned spins = 0;
    cc &= ~NVME_CC_EN;
    NW32(c->bar, NVME_CC, cc);
    while ((NR32(c->bar, NVME_CSTS) & NVME_CSTS_RDY) && spins++ < 2000000u)
        DelaySpins(100);
    return (NR32(c->bar, NVME_CSTS) & NVME_CSTS_RDY) == 0;
}

static int NvmeEnable(struct NvmeCtrl *c)
{
    uint32_t cc;
    unsigned spins = 0;

    NW32(c->bar, NVME_AQA, ((NVME_QSIZE - 1u) << 16) | (NVME_QSIZE - 1u));
    NW64(c->bar, NVME_ASQ, c->asqPa);
    NW64(c->bar, NVME_ACQ, c->acqPa);
    cc = (6u << 16) | (4u << 20) | (0u << 4) | NVME_CC_EN; /* IOSQES=6 IOCQES=4 */
    NW32(c->bar, NVME_CC, cc);
    while (!(NR32(c->bar, NVME_CSTS) & NVME_CSTS_RDY) && spins++ < 5000000u)
        DelaySpins(100);
    return (NR32(c->bar, NVME_CSTS) & NVME_CSTS_RDY) != 0;
}

static int CreateIoQueues(struct NvmeCtrl *c)
{
    struct NvmeSqe cmd;

    MemZero(&cmd, sizeof(cmd));
    cmd.cdw0 = 0x05u; /* Create I/O CQ */
    cmd.prp1 = c->iocqPa;
    cmd.cdw10 = ((NVME_QSIZE - 1u) << 16) | 1u; /* QID 1 */
    cmd.cdw11 = 1u; /* physically contiguous */
    if (AdminCmd(c, &cmd) != 0)
        return 0;

    MemZero(&cmd, sizeof(cmd));
    cmd.cdw0 = 0x01u; /* Create I/O SQ */
    cmd.prp1 = c->iosqPa;
    cmd.cdw10 = ((NVME_QSIZE - 1u) << 16) | 1u;
    cmd.cdw11 = (1u << 16) | 1u; /* CQID 1, PC */
    return AdminCmd(c, &cmd) == 0;
}

static int IdentifyNs(struct NvmeCtrl *c)
{
    struct NvmeSqe cmd;
    uint8_t *id = c->idBuf;
    uint8_t lbads;
    uint32_t flbas;

    MemZero(&cmd, sizeof(cmd));
    cmd.cdw0 = 0x06u; /* Identify */
    cmd.nsid = 1;
    cmd.prp1 = c->idPa;
    cmd.cdw10 = 0; /* CNS = namespace */
    if (AdminCmd(c, &cmd) != 0)
        return 0;
    c->nsSize = (uint64_t)id[0] | ((uint64_t)id[1] << 8) |
                ((uint64_t)id[2] << 16) | ((uint64_t)id[3] << 24) |
                ((uint64_t)id[4] << 32) | ((uint64_t)id[5] << 40) |
                ((uint64_t)id[6] << 48) | ((uint64_t)id[7] << 56);
    flbas = id[26];
    lbads = id[128 + (flbas & 0x0Fu) * 16u + 2u];
    if (lbads < 9)
        lbads = 9;
    c->lbaSize = 1u << lbads;
    return c->nsSize > 0;
}

static int NvmeRead(RoboBlockDevice *dev, uint64_t lba, void *buf)
{
    struct NvmeCtrl *c = (struct NvmeCtrl *)dev->priv;
    struct NvmeSqe cmd;

    if (!c || !c->ready)
        return -1;
    MemZero(&cmd, sizeof(cmd));
    cmd.cdw0 = 0x02u; /* Read */
    cmd.nsid = 1;
    cmd.prp1 = c->ioBufPa;
    cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFFu);
    cmd.cdw11 = (uint32_t)(lba >> 32);
    cmd.cdw12 = 0; /* 1 block */
    if (IoCmd(c, &cmd) != 0)
        return -1;
    MemCopy(buf, c->ioBuf, c->lbaSize);
    return 0;
}

static int NvmeWrite(RoboBlockDevice *dev, uint64_t lba, const void *buf)
{
    struct NvmeCtrl *c = (struct NvmeCtrl *)dev->priv;
    struct NvmeSqe cmd;

    if (!c || !c->ready)
        return -1;
    MemCopy(c->ioBuf, buf, c->lbaSize);
    MemZero(&cmd, sizeof(cmd));
    cmd.cdw0 = 0x01u; /* Write */
    cmd.nsid = 1;
    cmd.prp1 = c->ioBufPa;
    cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFFu);
    cmd.cdw11 = (uint32_t)(lba >> 32);
    cmd.cdw12 = 0;
    return IoCmd(c, &cmd);
}

static int NvmeFlush(RoboBlockDevice *dev)
{
    struct NvmeCtrl *c = (struct NvmeCtrl *)dev->priv;
    struct NvmeSqe cmd;

    if (!c || !c->ready)
        return -1;
    MemZero(&cmd, sizeof(cmd));
    cmd.cdw0 = 0x00u; /* Flush */
    cmd.nsid = 1;
    return IoCmd(c, &cmd);
}

static int NvmeInitCtrl(struct NvmeCtrl *c, RoboPciDevice *pci)
{
    uint64_t cap;

    if (!(pci->bar[0].flags & RoboPciBarMem) || !pci->bar[0].addr)
        return 0;
    PciEnableMmio(pci);
    PciEnableBusmaster(pci);
    c->pci = pci;
    c->bar = (volatile uint8_t *)MmioMap((PhysAddr)pci->bar[0].addr,
                                             pci->bar[0].size);
    if (!c->bar)
        return 0;
    cap = NR64(c->bar, NVME_CAP);
    c->doorbellStride = (uint32_t)((cap >> 32) & 0xFu);
    c->maxQentries = (uint32_t)((cap & 0xFFFFu) + 1u);
    if (!NvmeDisable(c))
        return 0;

    c->asq = (struct NvmeSqe *)DmaPage(&c->asqPa);
    c->acq = (struct NvmeCqe *)DmaPage(&c->acqPa);
    c->iosq = (struct NvmeSqe *)DmaPage(&c->iosqPa);
    c->iocq = (struct NvmeCqe *)DmaPage(&c->iocqPa);
    c->idBuf = (uint8_t *)DmaPage(&c->idPa);
    c->ioBuf = (uint8_t *)DmaPage(&c->ioBufPa);
    if (!c->asq || !c->acq || !c->iosq || !c->iocq || !c->idBuf || !c->ioBuf)
        return 0;
    c->asqTail = c->acqHead = c->iosqTail = c->iocqHead = 0;
    c->acqPhase = c->iocqPhase = 1;
    c->nextCid = 1;
    if (!NvmeEnable(c))
        return 0;
    if (!CreateIoQueues(c))
        return 0;
    if (!IdentifyNs(c))
        return 0;

    c->ready = 1;
    c->bdev.blockCount = c->nsSize;
    c->bdev.blockSize = c->lbaSize;
    c->bdev.priv = c;
    c->bdev.read = NvmeRead;
    c->bdev.write = NvmeWrite;
    c->bdev.flush = NvmeFlush;
    c->bdev.destroy = 0;
    if (BdevTableNextDiskName(c->name, sizeof(c->name)) != 0)
        return 0;
    if (BdevTableAdd(c->name, RoboBdevKindDisk, &c->bdev, 0) != 0)
        return 0;
    PartitionProbe(&c->bdev, c->name);
    gNvmeDisks++;
    kinfo("[INFO] [DRIVER] NVMe %s  blocks %llu  lba %u\n",
            c->name, (unsigned long long)c->nsSize, (unsigned)c->lbaSize);
    return 1;
}

static int NvmeProbe(RoboDevice *dev)
{
    RoboPciDevice *pci;
    if (!dev || !dev->busData)
        return -1;
    pci = (RoboPciDevice *)dev->busData;
    if (pci->classCode != 0x01 || pci->subclass != 0x08)
        return -1;
    if (pci->progif != 0x02 && pci->progif != 0)
        return -1;
    return 0;
}

static int NvmeStart(RoboDevice *dev)
{
    struct NvmeCtrl *c;
    if (gNvmeN >= NVME_MAX_CTRL)
        return -1;
    c = &gNvme[gNvmeN];
    MemZero(c, sizeof(*c));
    if (!NvmeInitCtrl(c, (RoboPciDevice *)dev->busData))
        return -1;
    gNvmeN++;
    return 0;
}

static int NvmeStop(RoboDevice *dev)
{
    (void)dev;
    return 0;
}

static int NvmeRemove(RoboDevice *dev)
{
    (void)dev;
    return 0;
}

static const RoboDriverId nvmeIds[] = {
    { RoboBusPci, 0, 0, 0, 0, 0x01, 0x08, 0x02,
      RoboMatchBus | RoboMatchClass }
};

static RoboDriver nvmeDrv = {
    "nvme",
    "Roboticela",
    0x00010000u,
    RoboDriverAbiMajor,
    RoboDriverAbiMinor,
    RoboCapPciConfig | RoboCapMmio | RoboCapDma | RoboCapBlockIo,
    RoboDrvChannelBuiltin,
    RoboDrvStable,
    RoboDrvDiscovered,
    1,
    nvmeIds,
    1,
    NvmeProbe,
    NvmeStart,
    NvmeStop,
    NvmeRemove
};

int NvmeRegister(void)
{
    return DriverRegister(&nvmeDrv);
}

unsigned NvmeDiskCount(void)
{
    return gNvmeDisks;
}

RoboBlockDevice *NvmeDiskAt(unsigned i)
{
    return i < gNvmeN ? &gNvme[i].bdev : 0;
}
