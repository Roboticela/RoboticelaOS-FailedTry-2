#include "storage/ahci.h"
#include "storage/partition.h"
#include "fs/block/bdev_table.h"
#include "pci/pci.h"
#include "arch/x86_64/mmio.h"
#include "mm/pmm.h"
#include "mm/page.h"
#include "mm/heap/heap.h"
#include "kio.h"

#define AHCI_GHC_CAP  0x00u
#define AHCI_GHC_GHC  0x04u
#define AHCI_GHC_IS   0x08u
#define AHCI_GHC_PI   0x0Cu
#define AHCI_GHC_VS   0x10u

#define AHCI_GHC_AE   (1u << 31)
#define AHCI_GHC_IE   (1u << 1)
#define AHCI_GHC_HR   (1u << 0)

#define AHCI_PORT_CLB  0x00u
#define AHCI_PORT_CLBU 0x04u
#define AHCI_PORT_FB   0x08u
#define AHCI_PORT_FBU  0x0Cu
#define AHCI_PORT_IS   0x10u
#define AHCI_PORT_IE   0x14u
#define AHCI_PORT_CMD  0x18u
#define AHCI_PORT_TFD  0x20u
#define AHCI_PORT_SIG  0x24u
#define AHCI_PORT_SSTS 0x28u
#define AHCI_PORT_SCTL 0x2Cu
#define AHCI_PORT_SERR 0x30u
#define AHCI_PORT_SACT 0x34u
#define AHCI_PORT_CI   0x38u

#define AHCI_CMD_ST    (1u << 0)
#define AHCI_CMD_FRE   (1u << 4)
#define AHCI_CMD_FR    (1u << 14)
#define AHCI_CMD_CR    (1u << 15)

#define AHCI_SSTS_DET_MASK 0x0Fu
#define AHCI_SSTS_DET_PRESENT 0x03u

#define AHCI_SIG_ATA 0x00000101u

#define AHCI_MAX_PORTS 32u
#define AHCI_MAX_DISKS 8u

struct AhciCmdHeader {
    uint16_t flags;
    uint16_t prdtl;
    uint32_t prdbc;
    uint64_t ctba;
    uint32_t reserved[4];
};

struct AhciPrdt {
    uint64_t dba;
    uint32_t reserved;
    uint32_t dbc; /* bit 31 = interrupt */
};

struct AhciCmdTable {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    struct AhciPrdt prdt[1];
};

struct AhciPort {
    volatile uint8_t *regs;
    struct AhciCmdHeader *cl;
    uint8_t *fis;
    struct AhciCmdTable *ct;
    PhysAddr clPa, fisPa, ctPa, bufPa;
    uint8_t *buf;
    uint64_t sectors;
    uint32_t sectorSize;
    int ready;
    RoboBlockDevice bdev;
    char name[RoboBdevNameMax];
};

struct AhciCtrl {
    RoboPciDevice *pci;
    volatile uint8_t *abar;
    uint32_t portsImpl;
    unsigned nDisk;
    struct AhciPort ports[AHCI_MAX_DISKS];
};

static struct AhciCtrl gAhci[4];
static unsigned gAhciN;
static unsigned gAhciDisks;

static uint32_t R32(volatile uint8_t *p, uint32_t off)
{
    return MmioR32(p + off);
}

static void W32(volatile uint8_t *p, uint32_t off, uint32_t v)
{
    MmioW32(p + off, v);
}

static void DelaySpins(unsigned n)
{
    volatile unsigned i;
    for (i = 0; i < n; i++)
        __asm__ volatile ("pause");
}

static void *DmaAlloc(PhysAddr *pa, unsigned bytes)
{
    PhysAddr p;
    uint8_t *v;
    unsigned i;
    unsigned pages = (bytes + (unsigned)RoboPageSize - 1u) / (unsigned)RoboPageSize;
    unsigned got = 0;

    if (pages == 0)
        pages = 1;
    /* Contiguous identity pages: allocate first, require sequential. */
    p = PageAlloc();
    if (p == RoboInvalidPhysAddr)
        return 0;
    for (got = 1; got < pages; got++) {
        PhysAddr q = PageAlloc();
        if (q != p + got * RoboPageSize) {
            unsigned j;
            if (q != RoboInvalidPhysAddr)
                PageFree(q);
            for (j = 0; j < got; j++)
                PageFree(p + j * RoboPageSize);
            return 0;
        }
    }
    v = (uint8_t *)(uintptr_t)p;
    for (i = 0; i < pages * (unsigned)RoboPageSize; i++)
        v[i] = 0;
    if (pa)
        *pa = p;
    return v;
}

static void PortStop(struct AhciPort *pt)
{
    uint32_t cmd = R32(pt->regs, AHCI_PORT_CMD);
    if (cmd & AHCI_CMD_ST) {
        cmd &= ~AHCI_CMD_ST;
        W32(pt->regs, AHCI_PORT_CMD, cmd);
    }
    DelaySpins(100000);
    cmd = R32(pt->regs, AHCI_PORT_CMD);
    if (cmd & AHCI_CMD_FRE) {
        cmd &= ~AHCI_CMD_FRE;
        W32(pt->regs, AHCI_PORT_CMD, cmd);
    }
    DelaySpins(100000);
}

static int PortStart(struct AhciPort *pt)
{
    uint32_t cmd;
    unsigned spins = 0;

    while ((R32(pt->regs, AHCI_PORT_CMD) & AHCI_CMD_CR) && spins++ < 1000000u)
        DelaySpins(100);
    W32(pt->regs, AHCI_PORT_CLB, (uint32_t)(pt->clPa & 0xFFFFFFFFu));
    W32(pt->regs, AHCI_PORT_CLBU, (uint32_t)(pt->clPa >> 32));
    W32(pt->regs, AHCI_PORT_FB, (uint32_t)(pt->fisPa & 0xFFFFFFFFu));
    W32(pt->regs, AHCI_PORT_FBU, (uint32_t)(pt->fisPa >> 32));
    cmd = R32(pt->regs, AHCI_PORT_CMD) | AHCI_CMD_FRE;
    W32(pt->regs, AHCI_PORT_CMD, cmd);
    cmd |= AHCI_CMD_ST;
    W32(pt->regs, AHCI_PORT_CMD, cmd);
    return 1;
}

static int PortWaitIdle(struct AhciPort *pt)
{
    unsigned spins = 0;
    while (spins++ < 2000000u) {
        uint32_t tfd = R32(pt->regs, AHCI_PORT_TFD);
        if ((tfd & 0x88u) == 0)
            return 1;
        DelaySpins(50);
    }
    return 0;
}

static int PortCmd(struct AhciPort *pt, uint8_t ataCmd, uint64_t lba,
                   uint16_t count, int write)
{
    struct AhciCmdHeader *h;
    struct AhciCmdTable *ct;
    uint8_t *fis;
    unsigned spins = 0;
    uint32_t is;
    int nodata = (count == 0);

    if (!PortWaitIdle(pt))
        return -1;
    W32(pt->regs, AHCI_PORT_IS, 0xFFFFFFFFu);
    h = &pt->cl[0];
    ct = pt->ct;
    fis = ct->cfis;
    {
        unsigned i;
        for (i = 0; i < sizeof(ct->cfis); i++)
            fis[i] = 0;
    }
    fis[0] = 0x27; /* H2D */
    fis[1] = 1 << 7;
    fis[2] = ataCmd;
    fis[4] = (uint8_t)(lba & 0xFFu);
    fis[5] = (uint8_t)((lba >> 8) & 0xFFu);
    fis[6] = (uint8_t)((lba >> 16) & 0xFFu);
    fis[7] = 0x40u | (uint8_t)((lba >> 24) & 0x0Fu);
    fis[8] = (uint8_t)((lba >> 32) & 0xFFu);
    fis[9] = (uint8_t)((lba >> 40) & 0xFFu);
    fis[12] = (uint8_t)(count & 0xFFu);
    fis[13] = (uint8_t)((count >> 8) & 0xFFu);

    if (!nodata) {
        ct->prdt[0].dba = pt->bufPa;
        ct->prdt[0].reserved = 0;
        ct->prdt[0].dbc = (uint32_t)(count * pt->sectorSize) - 1u;
    }

    h->flags = (uint16_t)(5u | (write ? (1u << 6) : 0));
    h->prdtl = nodata ? 0 : 1;
    h->prdbc = 0;
    h->ctba = pt->ctPa;

    W32(pt->regs, AHCI_PORT_CI, 1u);
    while (spins++ < 5000000u) {
        if ((R32(pt->regs, AHCI_PORT_CI) & 1u) == 0)
            break;
        is = R32(pt->regs, AHCI_PORT_IS);
        if (is & (1u << 30))
            return -1;
        DelaySpins(20);
    }
    if (R32(pt->regs, AHCI_PORT_CI) & 1u)
        return -1;
    return 0;
}

static int AhciIdentify(struct AhciPort *pt)
{
    uint16_t *id;
    uint64_t sectors;

    if (PortCmd(pt, 0xECu, 0, 1, 0) != 0)
        return 0;
    id = (uint16_t *)pt->buf;
    if (id[83] & (1u << 10)) {
        sectors = (uint64_t)id[100] | ((uint64_t)id[101] << 16) |
                  ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48);
    } else {
        sectors = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
    }
    if (!sectors)
        return 0;
    pt->sectors = sectors;
    pt->sectorSize = 512;
    return 1;
}

static void BufCopy(void *dst, const void *src, unsigned n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
        *d++ = *s++;
}

static int AhciRead(RoboBlockDevice *dev, uint64_t lba, void *buf)
{
    struct AhciPort *pt = (struct AhciPort *)dev->priv;
    if (!pt || !pt->ready)
        return -1;
    if (PortCmd(pt, 0x25u /* READ DMA EXT */, lba, 1, 0) != 0)
        return -1;
    BufCopy(buf, pt->buf, pt->sectorSize);
    return 0;
}

static int AhciWrite(RoboBlockDevice *dev, uint64_t lba, const void *buf)
{
    struct AhciPort *pt = (struct AhciPort *)dev->priv;
    if (!pt || !pt->ready)
        return -1;
    BufCopy(pt->buf, buf, pt->sectorSize);
    if (PortCmd(pt, 0x35u /* WRITE DMA EXT */, lba, 1, 1) != 0)
        return -1;
    return 0;
}

static int AhciFlush(RoboBlockDevice *dev)
{
    struct AhciPort *pt = (struct AhciPort *)dev->priv;
    if (!pt || !pt->ready)
        return -1;
    return PortCmd(pt, 0xE7u /* FLUSH CACHE */, 0, 0, 0);
}

static int SetupPort(struct AhciCtrl *c, unsigned portIndex)
{
    struct AhciPort *pt;
    volatile uint8_t *regs;
    uint32_t ssts, sig;

    if (c->nDisk >= AHCI_MAX_DISKS)
        return 0;
    regs = c->abar + 0x100u + portIndex * 0x80u;
    ssts = R32(regs, AHCI_PORT_SSTS);
    if ((ssts & AHCI_SSTS_DET_MASK) != AHCI_SSTS_DET_PRESENT)
        return 0;
    sig = R32(regs, AHCI_PORT_SIG);
    if (sig != 0 && sig != AHCI_SIG_ATA && (sig & 0xFFFFu) != 0x0101u)
        return 0;

    pt = &c->ports[c->nDisk];
    pt->regs = regs;
    PortStop(pt);
    pt->cl = (struct AhciCmdHeader *)DmaAlloc(&pt->clPa, 1024);
    pt->fis = (uint8_t *)DmaAlloc(&pt->fisPa, 256);
    pt->ct = (struct AhciCmdTable *)DmaAlloc(&pt->ctPa, 256);
    pt->buf = (uint8_t *)DmaAlloc(&pt->bufPa, 4096);
    if (!pt->cl || !pt->fis || !pt->ct || !pt->buf)
        return 0;
    PortStart(pt);
    pt->sectorSize = 512;
    if (!AhciIdentify(pt))
        return 0;

    pt->ready = 1;
    pt->bdev.blockCount = pt->sectors;
    pt->bdev.blockSize = pt->sectorSize;
    pt->bdev.priv = pt;
    pt->bdev.read = AhciRead;
    pt->bdev.write = AhciWrite;
    pt->bdev.flush = AhciFlush;
    pt->bdev.destroy = 0;
    if (BdevTableNextDiskName(pt->name, sizeof(pt->name)) != 0)
        return 0;
    if (BdevTableAdd(pt->name, RoboBdevKindDisk, &pt->bdev, 0) != 0)
        return 0;
    PartitionProbe(&pt->bdev, pt->name);
    c->nDisk++;
    gAhciDisks++;
    kinfo("[INFO] [DRIVER] AHCI %s  sectors %llu\n",
            pt->name, (unsigned long long)pt->sectors);
    return 1;
}

static int AhciInitCtrl(struct AhciCtrl *c, RoboPciDevice *pci)
{
    uint32_t pi, i;
    uint32_t ghc;

    if (!(pci->bar[5].flags & RoboPciBarMem) || !pci->bar[5].addr)
        return 0;
    PciEnableMmio(pci);
    PciEnableBusmaster(pci);
    c->pci = pci;
    c->abar = (volatile uint8_t *)MmioMap((PhysAddr)pci->bar[5].addr,
                                              pci->bar[5].size);
    if (!c->abar)
        return 0;
    ghc = R32(c->abar, AHCI_GHC_GHC);
    ghc |= AHCI_GHC_AE;
    W32(c->abar, AHCI_GHC_GHC, ghc);
    DelaySpins(10000);
    c->portsImpl = R32(c->abar, AHCI_GHC_PI);
    c->nDisk = 0;
    for (i = 0; i < AHCI_MAX_PORTS; i++) {
        if (!(c->portsImpl & (1u << i)))
            continue;
        SetupPort(c, i);
    }
    return c->nDisk > 0;
}

static int AhciProbe(RoboDevice *dev)
{
    RoboPciDevice *pci;
    if (!dev || !dev->busData)
        return -1;
    pci = (RoboPciDevice *)dev->busData;
    if (pci->classCode != 0x01 || pci->subclass != 0x06)
        return -1;
    if (pci->progif != 0x01 && pci->progif != 0)
        return -1;
    return 0;
}

static int AhciStart(RoboDevice *dev)
{
    struct AhciCtrl *c;
    if (gAhciN >= 4)
        return -1;
    c = &gAhci[gAhciN];
    if (!AhciInitCtrl(c, (RoboPciDevice *)dev->busData))
        return -1;
    gAhciN++;
    return 0;
}

static int AhciStop(RoboDevice *dev)
{
    (void)dev;
    return 0;
}

static int AhciRemove(RoboDevice *dev)
{
    (void)dev;
    return 0;
}

static const RoboDriverId ahciIds[] = {
    { RoboBusPci, 0, 0, 0, 0, 0x01, 0x06, 0x01,
      RoboMatchBus | RoboMatchClass }
};

static RoboDriver ahciDrv = {
    "ahci",
    "Roboticela",
    0x00010000u,
    RoboDriverAbiMajor,
    RoboDriverAbiMinor,
    RoboCapPciConfig | RoboCapMmio | RoboCapDma | RoboCapBlockIo,
    RoboDrvChannelBuiltin,
    RoboDrvStable,
    RoboDrvDiscovered,
    1,
    ahciIds,
    1,
    AhciProbe,
    AhciStart,
    AhciStop,
    AhciRemove
};

int AhciRegister(void)
{
    return DriverRegister(&ahciDrv);
}

unsigned AhciDiskCount(void)
{
    return gAhciDisks;
}

RoboBlockDevice *AhciDiskAt(unsigned i)
{
    unsigned c, p, n = 0;
    for (c = 0; c < gAhciN; c++) {
        for (p = 0; p < gAhci[c].nDisk; p++) {
            if (n == i)
                return &gAhci[c].ports[p].bdev;
            n++;
        }
    }
    return 0;
}
