#include "storage/virtio_blk.h"
#include "pci/pci.h"
#include "arch/x86_64/mmio.h"
#include "arch/x86_64/io.h"
#include "mm/pmm.h"
#include "mm/page.h"
#include "mm/heap/heap.h"
#include "fs/block/block.h"
#include "fs/rfs/rfs.h"
#include "fs/vfs/vfs.h"
#include "fs/vfs/vnode.h"
#include "syscall/syscall_errors.h"
#include "kio.h"

#define VIRTIO_PCI_VENDOR     0x1AF4u
#define VIRTIO_DEV_BLK_LEGACY 0x1001u
#define VIRTIO_DEV_BLK_MODERN 0x1042u

#define VIRTIO_PCI_CAP_COMMON 1u
#define VIRTIO_PCI_CAP_NOTIFY 2u
#define VIRTIO_PCI_CAP_ISR    3u
#define VIRTIO_PCI_CAP_DEVICE 4u

#define VIRTIO_ACK       1u
#define VIRTIO_DRIVER    2u
#define VIRTIO_DRIVER_OK 4u
#define VIRTIO_FEAT_OK   8u
#define VIRTIO_FAILED    128u

#define VIRTIO_F_VERSION_1 (1ull << 32)
#define VIRTIO_F_ACCESS_PLATFORM (1ull << 33)
#define VIRTIO_BLK_F_FLUSH (1ull << 9)

#define VIRTIO_CFG_DFSELECT 0u
#define VIRTIO_CFG_DF       4u
#define VIRTIO_CFG_GFSELECT 8u
#define VIRTIO_CFG_GF       12u
#define VIRTIO_CFG_MSIX     16u
#define VIRTIO_CFG_STATUS   20u
#define VIRTIO_CFG_QSEL     22u
#define VIRTIO_CFG_QSIZE    24u
#define VIRTIO_CFG_QMSIX    26u
#define VIRTIO_CFG_QENABLE  28u
#define VIRTIO_CFG_QNOTIFY  30u
#define VIRTIO_CFG_QDESC    32u
#define VIRTIO_CFG_QDRIVER  40u
#define VIRTIO_CFG_QDEVICE  48u

#define VIRTQ_NEXT  1u
#define VIRTQ_WRITE 2u

#define VIRTIO_BLK_T_IN    0u
#define VIRTIO_BLK_T_OUT   1u
#define VIRTIO_BLK_T_FLUSH 4u

#define VQ_MAX 256u

struct vqDesc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct vqAvail {
    volatile uint16_t flags;
    volatile uint16_t idx;
    uint16_t ring[VQ_MAX];
};

struct vqUsedElem {
    uint32_t id;
    uint32_t len;
};

struct vqUsed {
    volatile uint16_t flags;
    volatile uint16_t idx;
    struct vqUsedElem ring[VQ_MAX];
};

struct virtioBlk {
    RoboPciDevice *pci;
    volatile uint8_t *common;
    volatile uint8_t *notify;
    volatile uint8_t *devcfg;
    uint32_t notifyMul;
    uint16_t qsz;
    uint16_t lastUsed;
    struct vqDesc *desc;
    struct vqAvail *avail;
    struct vqUsed *used;
    PhysAddr descPa, availPa, usedPa, hdrPa, bouncePa;
    uint8_t *hdr;
    uint8_t *bounce;
    uint64_t sectors;
    int flushOk;
    int legacy;
    uint16_t iobase;
    RoboBlockDevice bdev;
};

static struct virtioBlk gVb;
static int gVbReady;

static void wmb(void)
{
    __asm__ volatile ("mfence" ::: "memory");
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

static uint8_t *MapBar(const RoboPciDevice *d, uint8_t bar, uint32_t off,
                        uint32_t len)
{
    if (bar >= 6u)
        return 0;
    if (!(d->bar[bar].flags & RoboPciBarMem) || !d->bar[bar].addr)
        return 0;
    if ((uint64_t)off + len > d->bar[bar].size)
        return 0;
    if (!MmioMap((PhysAddr)d->bar[bar].addr, d->bar[bar].size))
        return 0;
    return (uint8_t *)(uintptr_t)(d->bar[bar].addr + off);
}

static int FindCaps(RoboPciDevice *d, struct virtioBlk *v)
{
    uint16_t sts;
    uint8_t ptr;
    uint32_t notifyMul = 0;

    v->common = v->notify = v->devcfg = 0;
    sts = PciCfgRead16(d->bus, d->dev, d->func, 0x06);
    if (!(sts & 0x10u))
        return 0;
    ptr = PciCfgRead8(d->bus, d->dev, d->func, 0x34);
    while (ptr && ptr != 0xFFu) {
        uint8_t id = PciCfgRead8(d->bus, d->dev, d->func, ptr);
        uint8_t next = PciCfgRead8(d->bus, d->dev, d->func, (uint8_t)(ptr + 1));
        if (id == 0x09u) {
            uint8_t type = PciCfgRead8(d->bus, d->dev, d->func, (uint8_t)(ptr + 3));
            uint8_t bar = PciCfgRead8(d->bus, d->dev, d->func, (uint8_t)(ptr + 4));
            uint32_t off = PciCfgRead32(d->bus, d->dev, d->func, (uint8_t)(ptr + 8));
            uint32_t len = PciCfgRead32(d->bus, d->dev, d->func, (uint8_t)(ptr + 12));
            uint8_t *base = MapBar(d, bar, off, len ? len : 4u);
            if (type == VIRTIO_PCI_CAP_COMMON)
                v->common = base;
            else if (type == VIRTIO_PCI_CAP_NOTIFY) {
                v->notify = base;
                notifyMul = PciCfgRead32(d->bus, d->dev, d->func,
                                                 (uint8_t)(ptr + 16));
            } else if (type == VIRTIO_PCI_CAP_DEVICE)
                v->devcfg = base;
        }
        ptr = next;
    }
    v->notifyMul = notifyMul;
    kinfo("[INFO] [DRIVER] VirtIO caps common=%llx notify=%llx cfg=%llx\n",
            (unsigned long long)(uintptr_t)v->common,
            (unsigned long long)(uintptr_t)v->notify,
            (unsigned long long)(uintptr_t)v->devcfg);
    return v->common && v->notify && v->devcfg;
}

static uint8_t CR8(struct virtioBlk *v, unsigned off)
{
    return MmioR8(v->common + off);
}

static void CW8(struct virtioBlk *v, unsigned off, uint8_t x)
{
    MmioW8(v->common + off, x);
}

static uint16_t CR16(struct virtioBlk *v, unsigned off)
{
    return MmioR16(v->common + off);
}

static void CW16(struct virtioBlk *v, unsigned off, uint16_t x)
{
    MmioW16(v->common + off, x);
}

static uint32_t CR32(struct virtioBlk *v, unsigned off)
{
    return MmioR32(v->common + off);
}

static void CW32(struct virtioBlk *v, unsigned off, uint32_t x)
{
    MmioW32(v->common + off, x);
}

static void CW64(struct virtioBlk *v, unsigned off, uint64_t x)
{
    MmioW64(v->common + off, x);
}

static int VqSubmit(struct virtioBlk *v, uint32_t type, uint64_t sector,
                     void *buf, uint32_t len, int writeDev)
{
    uint8_t *hdr = v->hdr;
    uint8_t *st = v->hdr + 16;
    unsigned spins = 0;
    uint16_t last;
    unsigned i;

    hdr[0] = (uint8_t)type;
    hdr[1] = (uint8_t)(type >> 8);
    hdr[2] = (uint8_t)(type >> 16);
    hdr[3] = (uint8_t)(type >> 24);
    for (i = 4; i < 8; i++)
        hdr[i] = 0;
    for (i = 0; i < 8; i++)
        hdr[8 + i] = (uint8_t)(sector >> (8 * i));
    st[0] = 0xFF;

    if (buf && len) {
        if (len > RoboPageSize)
            return -RoboEinval;
        if (writeDev) {
            uint8_t *src = (uint8_t *)buf;
            for (i = 0; i < len; i++)
                v->bounce[i] = src[i];
        }
    }

    v->desc[0].addr = v->hdrPa;
    v->desc[0].len = 16;
    v->desc[0].flags = VIRTQ_NEXT;
    v->desc[0].next = 1;
    if (type == VIRTIO_BLK_T_FLUSH || !buf || !len) {
        v->desc[1].addr = v->hdrPa + 16;
        v->desc[1].len = 1;
        v->desc[1].flags = VIRTQ_WRITE;
        v->desc[1].next = 0;
    } else {
        v->desc[1].addr = v->bouncePa;
        v->desc[1].len = len;
        v->desc[1].flags = (uint16_t)(VIRTQ_NEXT | (writeDev ? 0u : VIRTQ_WRITE));
        v->desc[1].next = 2;
        v->desc[2].addr = v->hdrPa + 16;
        v->desc[2].len = 1;
        v->desc[2].flags = VIRTQ_WRITE;
        v->desc[2].next = 0;
    }
    last = v->used->idx;
    wmb();
    v->avail->ring[v->avail->idx % v->qsz] = 0;
    wmb();
    v->avail->idx = (uint16_t)(v->avail->idx + 1);
    wmb();
    {
        uint16_t noff = v->legacy ? 0 : CR16(v, VIRTIO_CFG_QNOTIFY);
        if (v->legacy)
            Outw((uint16_t)(v->iobase + 16u), 0);
        else
            MmioW16(v->notify + (uint32_t)noff * v->notifyMul, 0);
    }
    while (v->used->idx == last) {
        __asm__ volatile ("pause");
        if (++spins > 20000000u)
            return -RoboEio;
    }
    wmb();
    if (st[0] != 0)
        return -RoboEio;
    if (buf && len && !writeDev) {
        uint8_t *dst = (uint8_t *)buf;
        for (i = 0; i < len; i++)
            dst[i] = v->bounce[i];
    }
    return 0;
}

static int VbRead(RoboBlockDevice *dev, uint64_t lba, void *buf)
{
    struct virtioBlk *v = (struct virtioBlk *)dev->priv;
    uint64_t sec = lba * (dev->blockSize / 512u);
    return VqSubmit(v, VIRTIO_BLK_T_IN, sec, buf, dev->blockSize, 0);
}

static int VbWrite(RoboBlockDevice *dev, uint64_t lba, const void *buf)
{
    struct virtioBlk *v = (struct virtioBlk *)dev->priv;
    uint64_t sec = lba * (dev->blockSize / 512u);
    return VqSubmit(v, VIRTIO_BLK_T_OUT, sec, (void *)buf, dev->blockSize, 1);
}

static int VbFlush(RoboBlockDevice *dev)
{
    struct virtioBlk *v = (struct virtioBlk *)dev->priv;
    if (!v->flushOk)
        return 0;
    return VqSubmit(v, VIRTIO_BLK_T_FLUSH, 0, 0, 0, 0);
}

static int VirtioSetupLegacy(struct virtioBlk *v, RoboPciDevice *pci)
{
    unsigned i;
    uint16_t qsz;
    uint32_t hostf, guestf;
    uint32_t availOff, usedOff, bytes, np;
    PhysAddr pages[8];
    uint8_t *base;

    v->legacy = 1;
    v->iobase = 0;
    for (i = 0; i < 6; i++) {
        if ((pci->bar[i].flags & RoboPciBarIo) && pci->bar[i].addr) {
            v->iobase = (uint16_t)pci->bar[i].addr;
            break;
        }
    }
    if (!v->iobase) {
        kprintf("[ERROR] [DRIVER] VirtIO legacy BAR missing\n");
        return 0;
    }
    PciEnableIo(pci);
    PciEnableBusmaster(pci);
    Outb((uint16_t)(v->iobase + 18u), 0);
    Outb((uint16_t)(v->iobase + 18u), VIRTIO_ACK);
    Outb((uint16_t)(v->iobase + 18u), (uint8_t)(VIRTIO_ACK | VIRTIO_DRIVER));
    hostf = Inl((uint16_t)(v->iobase + 0u));
    guestf = hostf & (uint32_t)VIRTIO_BLK_F_FLUSH;
    v->flushOk = guestf != 0;
    Outl((uint16_t)(v->iobase + 4u), guestf);
    Outw((uint16_t)(v->iobase + 14u), 0);
    qsz = Inw((uint16_t)(v->iobase + 12u));
    if (!qsz || qsz > VQ_MAX) {
        kprintf("[ERROR] [DRIVER] VirtIO legacy qsz %u\n", (unsigned)qsz);
        return 0;
    }
    v->qsz = qsz;
    availOff = 16u * qsz;
    usedOff = (availOff + 6u + 2u * qsz + 4095u) & ~4095u;
    bytes = usedOff + 6u + 8u * qsz;
    bytes = (bytes + 4095u) & ~4095u;
    np = bytes / (uint32_t)RoboPageSize;
    if (!np || np > 8u) {
        kprintf("[ERROR] [DRIVER] VirtIO vring too big\n");
        return 0;
    }
    for (i = 0; i < np; i++) {
        pages[i] = PageAlloc();
        if (pages[i] == RoboInvalidPhysAddr)
            return 0;
        if (i && pages[i] != pages[0] + i * RoboPageSize) {
            kprintf("[ERROR] [DRIVER] VirtIO DMA not contiguous\n");
            return 0;
        }
        {
            uint8_t *p = (uint8_t *)(uintptr_t)pages[i];
            unsigned k;
            for (k = 0; k < RoboPageSize; k++)
                p[k] = 0;
        }
    }
    base = (uint8_t *)(uintptr_t)pages[0];
    v->desc = (struct vqDesc *)base;
    v->avail = (struct vqAvail *)(base + availOff);
    v->used = (struct vqUsed *)(base + usedOff);
    v->descPa = pages[0];
    v->availPa = pages[0] + availOff;
    v->usedPa = pages[0] + usedOff;
    v->hdr = DmaPage(&v->hdrPa);
    v->bounce = DmaPage(&v->bouncePa);
    if (!v->hdr || !v->bounce)
        return 0;
    Outl((uint16_t)(v->iobase + 8u), (uint32_t)(pages[0] >> 12));
    {
        uint32_t lo = Inl((uint16_t)(v->iobase + 20u));
        uint32_t hi = Inl((uint16_t)(v->iobase + 24u));
        v->sectors = (uint64_t)lo | ((uint64_t)hi << 32);
    }
    if (v->sectors < 8ull || v->sectors > 32768ull) {
        kprintf("[ERROR] [DRIVER] VirtIO capacity %llu\n",
                (unsigned long long)v->sectors);
        return 0;
    }
    v->lastUsed = 0;
    Outb((uint16_t)(v->iobase + 18u),
              (uint8_t)(VIRTIO_ACK | VIRTIO_DRIVER | VIRTIO_DRIVER_OK));
    v->bdev.blockSize = 4096;
    v->bdev.blockCount = v->sectors / 8ull;
    v->bdev.priv = v;
    v->bdev.read = VbRead;
    v->bdev.write = VbWrite;
    v->bdev.flush = VbFlush;
    v->bdev.destroy = 0;
    kinfo("[INFO] [DRIVER] VirtIO legacy I/O 0x%x qsz %u\n",
            (unsigned)v->iobase, (unsigned)qsz);
    return 1;
}

static int VirtioSetup(struct virtioBlk *v, RoboPciDevice *pci)
{
    uint32_t lo, hi;
    uint64_t feat, drv;
    uint16_t qsz;

    v->pci = pci;
    v->legacy = 0;
    v->iobase = 0;
    PciEnableIo(pci);
    PciEnableMmio(pci);
    PciEnableBusmaster(pci);
    if (!FindCaps(pci, v)) {
        kinfo("[INFO] [DRIVER] VirtIO modern caps missing\n");
        return VirtioSetupLegacy(v, pci);
    }

    CW8(v, VIRTIO_CFG_STATUS, 0);
    CW8(v, VIRTIO_CFG_STATUS, VIRTIO_ACK);
    CW8(v, VIRTIO_CFG_STATUS, (uint8_t)(VIRTIO_ACK | VIRTIO_DRIVER));
    kinfo("[INFO] [DRIVER] VirtIO status after ack %x\n",
            (unsigned)CR8(v, VIRTIO_CFG_STATUS));

    CW32(v, VIRTIO_CFG_DFSELECT, 0);
    lo = CR32(v, VIRTIO_CFG_DF);
    CW32(v, VIRTIO_CFG_DFSELECT, 1);
    hi = CR32(v, VIRTIO_CFG_DF);
    feat = (uint64_t)lo | ((uint64_t)hi << 32);
    drv = VIRTIO_F_VERSION_1;
    if (!(feat & VIRTIO_F_VERSION_1)) {
        kprintf("[ERROR] [DRIVER] VirtIO missing VERSION_1 feat=%llx\n",
                (unsigned long long)feat);
        return VirtioSetupLegacy(v, pci);
    }
    if (feat & VIRTIO_BLK_F_FLUSH)
        drv |= VIRTIO_BLK_F_FLUSH;
    v->flushOk = (drv & VIRTIO_BLK_F_FLUSH) != 0;
    kinfo("[INFO] [DRIVER] VirtIO feat=%llx drv=%llx\n",
            (unsigned long long)feat, (unsigned long long)drv);
    CW32(v, VIRTIO_CFG_GFSELECT, 0);
    CW32(v, VIRTIO_CFG_GF, (uint32_t)drv);
    CW32(v, VIRTIO_CFG_GFSELECT, 1);
    CW32(v, VIRTIO_CFG_GF, (uint32_t)(drv >> 32));
    wmb();
    CW8(v, VIRTIO_CFG_STATUS,
         (uint8_t)(VIRTIO_ACK | VIRTIO_DRIVER | VIRTIO_FEAT_OK));
    wmb();
    {
        uint8_t st = CR8(v, VIRTIO_CFG_STATUS);
        kinfo("[INFO] [DRIVER] VirtIO status after feat %x\n", (unsigned)st);
        if (!(st & VIRTIO_FEAT_OK)) {
            kprintf("[ERROR] [DRIVER] VirtIO features rejected feat=%llx\n",
                    (unsigned long long)feat);
            return VirtioSetupLegacy(v, pci);
        }
    }

    CW16(v, VIRTIO_CFG_MSIX, 0xFFFFu);
    CW16(v, VIRTIO_CFG_QSEL, 0);
    qsz = CR16(v, VIRTIO_CFG_QSIZE);
    if (!qsz) {
        kprintf("[ERROR] [DRIVER] VirtIO queue size 0\n");
        return VirtioSetupLegacy(v, pci);
    }
    if (qsz > 16u) {
        qsz = 16u;
        CW16(v, VIRTIO_CFG_QSIZE, qsz);
    }
    v->qsz = qsz;
    kinfo("[INFO] [DRIVER] VirtIO qsz %u notify_mul %u\n",
            (unsigned)qsz, (unsigned)v->notifyMul);
    v->desc = DmaPage(&v->descPa);
    v->avail = DmaPage(&v->availPa);
    v->used = DmaPage(&v->usedPa);
    v->hdr = DmaPage(&v->hdrPa);
    v->bounce = DmaPage(&v->bouncePa);
    if (!v->desc || !v->avail || !v->used || !v->hdr || !v->bounce) {
        kprintf("[ERROR] [DRIVER] VirtIO DMA alloc failed\n");
        return 0;
    }
    CW16(v, VIRTIO_CFG_QMSIX, 0xFFFFu);
    CW64(v, VIRTIO_CFG_QDESC, v->descPa);
    CW64(v, VIRTIO_CFG_QDRIVER, v->availPa);
    CW64(v, VIRTIO_CFG_QDEVICE, v->usedPa);
    CW16(v, VIRTIO_CFG_QENABLE, 1);

    v->sectors = MmioR64(v->devcfg);
    if (v->sectors < 8ull || v->sectors > 32768ull) {
        kprintf("[ERROR] [DRIVER] VirtIO capacity %llu\n",
                (unsigned long long)v->sectors);
        return 0;
    }
    v->lastUsed = 0;
    CW8(v, VIRTIO_CFG_STATUS,
         (uint8_t)(VIRTIO_ACK | VIRTIO_DRIVER | VIRTIO_FEAT_OK |
                   VIRTIO_DRIVER_OK));
    kinfo("[INFO] [DRIVER] VirtIO status driver_ok %x\n",
            (unsigned)CR8(v, VIRTIO_CFG_STATUS));

    v->bdev.blockSize = 4096;
    v->bdev.blockCount = v->sectors / 8ull;
    v->bdev.priv = v;
    v->bdev.read = VbRead;
    v->bdev.write = VbWrite;
    v->bdev.flush = VbFlush;
    v->bdev.destroy = 0;
    return 1;
}

static int VirtioProbe(RoboDevice *dev)
{
    RoboPciDevice *pci;
    if (!dev || !dev->busData)
        return -1;
    pci = (RoboPciDevice *)dev->busData;
    if (pci->vendor != VIRTIO_PCI_VENDOR)
        return -1;
    if (pci->device != VIRTIO_DEV_BLK_LEGACY &&
        pci->device != VIRTIO_DEV_BLK_MODERN)
        return -1;
    return 0;
}

static int VirtioStart(RoboDevice *dev)
{
    if (gVbReady)
        return 0;
    if (!VirtioSetup(&gVb, (RoboPciDevice *)dev->busData)) {
        kprintf("[ERROR] [DRIVER] VirtIO Block init failed\n");
        return -1;
    }
    gVbReady = 1;
    kinfo("[INFO] [DRIVER] VirtIO Block detected\n");
    kinfo("[INFO] [DRIVER] Device %x:%x\n",
            (unsigned)gVb.pci->vendor, (unsigned)gVb.pci->device);
    kinfo("[INFO] [DRIVER] sectors %llu  blocks %llu\n",
            (unsigned long long)gVb.sectors,
            (unsigned long long)gVb.bdev.blockCount);
    return 0;
}

static int VirtioStop(RoboDevice *dev)
{
    (void)dev;
    return 0;
}

static int VirtioRemove(RoboDevice *dev)
{
    (void)dev;
    return 0;
}

static const RoboDriverId virtioIds[] = {
    { RoboBusPci, VIRTIO_PCI_VENDOR, VIRTIO_DEV_BLK_MODERN, 0, 0, 0, 0, 0,
      RoboMatchBus | RoboMatchVendor | RoboMatchDevice },
    { RoboBusPci, VIRTIO_PCI_VENDOR, VIRTIO_DEV_BLK_LEGACY, 0, 0, 0, 0, 0,
      RoboMatchBus | RoboMatchVendor | RoboMatchDevice }
};

static RoboDriver virtioDrv = {
    "virtio-blk",
    "Roboticela",
    0x00010000u,
    RoboDriverAbiMajor,
    RoboDriverAbiMinor,
    RoboCapPciConfig | RoboCapMmio | RoboCapDma | RoboCapBlockIo,
    RoboDrvChannelBuiltin,
    RoboDrvStable,
    RoboDrvDiscovered,
    1,
    virtioIds,
    2,
    VirtioProbe,
    VirtioStart,
    VirtioStop,
    VirtioRemove
};

int VirtioBlkRegister(void)
{
    return DriverRegister(&virtioDrv);
}

RoboBlockDevice *VirtioBlkBdev(void)
{
    return gVbReady ? &gVb.bdev : 0;
}

int StorageSelftest(void)
{
    RoboBlockDevice *d = VirtioBlkBdev();
    uint8_t a[4096], b[4096];
    unsigned i;
    int ok = 1;
    RoboMount *m = 0;
    RoboVnode *n = 0;
    char rbuf[32];
    int64_t nr;

    if (!d) {
        kprintf("STORAGE_TEST  no virtio-blk\n");
        return 0;
    }
    if (d->blockSize != 4096 || d->blockCount < 16)
        return 0;
    for (i = 0; i < 4096; i++)
        a[i] = (uint8_t)(i * 3u + 1u);
    if (BdevWrite(d, 2, a) != 0)
        ok = 0;
    if (BdevFlush(d) != 0)
        ok = 0;
    for (i = 0; i < 4096; i++)
        b[i] = 0;
    if (BdevRead(d, 2, b) != 0)
        ok = 0;
    for (i = 0; i < 4096; i++) {
        if (a[i] != b[i])
            ok = 0;
    }
    if (BdevWriteN(d, 3, 2, a) != 0)
        ok = 0;
    if (BdevReadN(d, 3, 2, b) != 0)
        ok = 0;
    if (BdevRead(d, d->blockCount, a) == 0)
        ok = 0;
    if (ok && RobofsFormat(d) == 0 && RobofsMount(d, 0, &m) == 0 && m) {
        if (m->root && m->root->ops && m->root->ops->create &&
            m->root->ops->create(m->root, "disk.txt", RoboVnodeFile, 0, &n) == 0 && n) {
            nr = VnodeWrite(n, 0, "virtio-robofs", 13, 0);
            if (nr != 13)
                ok = 0;
            VnodePut(n);
            n = 0;
        } else {
            ok = 0;
        }
        RobofsUnmount(m);
        m = 0;
        if (RobofsMount(d, 0, &m) == 0 && m && m->root &&
            m->root->ops->lookup(m->root, "disk.txt", &n) == 0 && n) {
            nr = VnodeRead(n, 0, rbuf, 13, 0);
            if (nr != 13 || rbuf[0] != 'v' || rbuf[12] != 's')
                ok = 0;
            VnodePut(n);
        } else {
            ok = 0;
        }
        if (m)
            RobofsUnmount(m);
    } else {
        ok = 0;
    }
    kprintf("STORAGE_TEST  virtio-blk %s\n", ok ? "ok" : "fail");
    return ok;
}
