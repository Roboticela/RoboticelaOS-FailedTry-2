#include "drivers/virtio_net.h"
#include "drivers/driver.h"
#include "pci/pci.h"
#include "arch/x86_64/mmio.h"
#include "irq/irq.h"
#include "mm/pmm.h"
#include "mm/page.h"
#include "net/core/interface.h"
#include "net/core/net_util.h"
#include "kio.h"

#define VIRTIO_PCI_VENDOR      0x1AF4u
#define VIRTIO_DEV_NET_LEGACY  0x1000u
#define VIRTIO_DEV_NET_MODERN  0x1041u

#define VIRTIO_PCI_CAP_COMMON 1u
#define VIRTIO_PCI_CAP_NOTIFY 2u
#define VIRTIO_PCI_CAP_ISR    3u
#define VIRTIO_PCI_CAP_DEVICE 4u

#define VIRTIO_ACK       1u
#define VIRTIO_DRIVER    2u
#define VIRTIO_DRIVER_OK 4u
#define VIRTIO_FEAT_OK   8u

#define VIRTIO_F_VERSION_1 (1ull << 32)
#define VIRTIO_NET_F_MAC    (1ull << 5)
#define VIRTIO_NET_F_STATUS (1ull << 16)

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
#define VQ_MAX 64u
#define VNET_HDR 12u
#define RXQ 0u
#define TXQ 1u

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

struct vnetQ {
    uint16_t qsz;
    uint16_t lastUsed;
    uint16_t notifyOff;
    struct vqDesc *desc;
    struct vqAvail *avail;
    struct vqUsed *used;
    PhysAddr descPa, availPa, usedPa;
};

struct virtioNet {
    RoboPciDevice *pci;
    volatile uint8_t *common;
    volatile uint8_t *notify;
    volatile uint8_t *devcfg;
    volatile uint8_t *isr;
    uint32_t notifyMul;
    uint8_t mac[6];
    struct vnetQ rx;
    struct vnetQ tx;
    PhysAddr rxPa[VQ_MAX];
    uint8_t *rxVa[VQ_MAX];
    PhysAddr txPa;
    uint8_t *txVa;
    uint16_t txFree;
    RoboNetIf *ifp;
    volatile int pending;
};

static struct virtioNet gVn;
static int gVnReady;

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

static int FindCaps(RoboPciDevice *d, struct virtioNet *v)
{
    uint16_t sts;
    uint8_t ptr;
    uint32_t notifyMul = 0;

    v->common = v->notify = v->devcfg = v->isr = 0;
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
            else if (type == VIRTIO_PCI_CAP_ISR)
                v->isr = base;
        }
        ptr = next;
    }
    v->notifyMul = notifyMul;
    return v->common && v->notify && v->devcfg;
}

static uint8_t CR8(struct virtioNet *v, unsigned off)
{
    return MmioR8(v->common + off);
}

static void CW8(struct virtioNet *v, unsigned off, uint8_t x)
{
    MmioW8(v->common + off, x);
}

static uint16_t CR16(struct virtioNet *v, unsigned off)
{
    return MmioR16(v->common + off);
}

static void CW16(struct virtioNet *v, unsigned off, uint16_t x)
{
    MmioW16(v->common + off, x);
}

static uint32_t CR32(struct virtioNet *v, unsigned off)
{
    return MmioR32(v->common + off);
}

static void CW32(struct virtioNet *v, unsigned off, uint32_t x)
{
    MmioW32(v->common + off, x);
}

static void CW64(struct virtioNet *v, unsigned off, uint64_t x)
{
    MmioW64(v->common + off, x);
}

static void QNotify(struct virtioNet *v, struct vnetQ *q)
{
    uint16_t noff = q->notifyOff;
    if (!v->notifyMul)
        MmioW16(v->notify + noff, 0);
    else
        MmioW16(v->notify + (uint32_t)noff * v->notifyMul, 0);
}

static int QSetup(struct virtioNet *v, struct vnetQ *q, uint16_t sel)
{
    uint16_t qsz;
    CW16(v, VIRTIO_CFG_QSEL, sel);
    qsz = CR16(v, VIRTIO_CFG_QSIZE);
    if (!qsz)
        return 0;
    if (qsz > 16u) {
        qsz = 16u;
        CW16(v, VIRTIO_CFG_QSIZE, qsz);
    }
    q->qsz = qsz;
    q->desc = DmaPage(&q->descPa);
    q->avail = DmaPage(&q->availPa);
    q->used = DmaPage(&q->usedPa);
    if (!q->desc || !q->avail || !q->used)
        return 0;
    q->notifyOff = CR16(v, VIRTIO_CFG_QNOTIFY);
    CW16(v, VIRTIO_CFG_QMSIX, 0xFFFFu);
    CW64(v, VIRTIO_CFG_QDESC, q->descPa);
    CW64(v, VIRTIO_CFG_QDRIVER, q->availPa);
    CW64(v, VIRTIO_CFG_QDEVICE, q->usedPa);
    CW16(v, VIRTIO_CFG_QENABLE, 1);
    q->lastUsed = 0;
    return 1;
}

static void RxFill(struct virtioNet *v)
{
    unsigned i;
    for (i = 0; i < v->rx.qsz; i++) {
        v->rx.desc[i].addr = v->rxPa[i];
        v->rx.desc[i].len = (uint32_t)RoboPageSize;
        v->rx.desc[i].flags = VIRTQ_WRITE;
        v->rx.desc[i].next = 0;
        v->rx.avail->ring[i] = (uint16_t)i;
    }
    wmb();
    v->rx.avail->idx = v->rx.qsz;
    wmb();
    QNotify(v, &v->rx);
}

static void IrqNet(unsigned irq, void *ctx)
{
    struct virtioNet *v = (struct virtioNet *)ctx;
    (void)irq;
    if (v->isr)
        (void)MmioR8(v->isr);
    v->pending = 1;
}

static int VnetSend(RoboNetIf *ifp, const void *data, unsigned len)
{
    struct virtioNet *v = ifp ? (struct virtioNet *)ifp->drv : 0;
    uint16_t last, id;
    unsigned i, spins = 0;
    uint8_t *b;
    if (!v || !data || !len || len + VNET_HDR > RoboPageSize)
        return -1;
    last = v->tx.used->idx;
    b = v->txVa;
    for (i = 0; i < VNET_HDR; i++)
        b[i] = 0;
    NetMemcpy(b + VNET_HDR, data, len);
    id = 0;
    v->tx.desc[0].addr = v->txPa;
    v->tx.desc[0].len = VNET_HDR + len;
    v->tx.desc[0].flags = 0;
    v->tx.desc[0].next = 0;
    wmb();
    v->tx.avail->ring[v->tx.avail->idx % v->tx.qsz] = id;
    wmb();
    v->tx.avail->idx = (uint16_t)(v->tx.avail->idx + 1);
    wmb();
    QNotify(v, &v->tx);
    while (v->tx.used->idx == last) {
        __asm__ volatile ("pause");
        if (++spins > 2000000u)
            return -1;
    }
    v->tx.lastUsed = v->tx.used->idx;
    return 0;
}

static void RxHarvest(struct virtioNet *v)
{
    while (v->rx.lastUsed != v->rx.used->idx) {
        struct vqUsedElem *e = &v->rx.used->ring[v->rx.lastUsed % v->rx.qsz];
        uint16_t id = (uint16_t)e->id;
        uint32_t len = e->len;
        uint8_t *va;
        wmb();
        if (id < v->rx.qsz && len > VNET_HDR) {
            va = v->rxVa[id];
            if (v->ifp)
                NetifRx(v->ifp, va + VNET_HDR, (unsigned)(len - VNET_HDR));
            v->rx.desc[id].addr = v->rxPa[id];
            v->rx.desc[id].len = (uint32_t)RoboPageSize;
            v->rx.desc[id].flags = VIRTQ_WRITE;
            wmb();
            v->rx.avail->ring[v->rx.avail->idx % v->rx.qsz] = id;
            wmb();
            v->rx.avail->idx = (uint16_t)(v->rx.avail->idx + 1);
        }
        v->rx.lastUsed = (uint16_t)(v->rx.lastUsed + 1);
    }
}

void VirtioNetPoll(void)
{
    if (!gVnReady)
        return;
    gVn.pending = 0;
    if (gVn.isr)
        (void)MmioR8(gVn.isr);
    RxHarvest(&gVn);
}

static int VnetSetup(struct virtioNet *v, RoboPciDevice *pci)
{
    uint32_t lo, hi;
    uint64_t feat, drv;
    unsigned i;

    v->pci = pci;
    PciEnableIo(pci);
    PciEnableMmio(pci);
    PciEnableBusmaster(pci);
    if (!FindCaps(pci, v)) {
        kprintf("[ERROR] [NET] VirtIO-net caps missing\n");
        return 0;
    }
    CW8(v, VIRTIO_CFG_STATUS, 0);
    CW8(v, VIRTIO_CFG_STATUS, VIRTIO_ACK);
    CW8(v, VIRTIO_CFG_STATUS, (uint8_t)(VIRTIO_ACK | VIRTIO_DRIVER));
    CW32(v, VIRTIO_CFG_DFSELECT, 0);
    lo = CR32(v, VIRTIO_CFG_DF);
    CW32(v, VIRTIO_CFG_DFSELECT, 1);
    hi = CR32(v, VIRTIO_CFG_DF);
    feat = (uint64_t)lo | ((uint64_t)hi << 32);
    if (!(feat & VIRTIO_F_VERSION_1)) {
        kprintf("[ERROR] [NET] VirtIO-net missing VERSION_1\n");
        return 0;
    }
    drv = VIRTIO_F_VERSION_1;
    if (feat & VIRTIO_NET_F_MAC)
        drv |= VIRTIO_NET_F_MAC;
    if (feat & VIRTIO_NET_F_STATUS)
        drv |= VIRTIO_NET_F_STATUS;
    CW32(v, VIRTIO_CFG_GFSELECT, 0);
    CW32(v, VIRTIO_CFG_GF, (uint32_t)drv);
    CW32(v, VIRTIO_CFG_GFSELECT, 1);
    CW32(v, VIRTIO_CFG_GF, (uint32_t)(drv >> 32));
    wmb();
    CW8(v, VIRTIO_CFG_STATUS,
         (uint8_t)(VIRTIO_ACK | VIRTIO_DRIVER | VIRTIO_FEAT_OK));
    wmb();
    if (!(CR8(v, VIRTIO_CFG_STATUS) & VIRTIO_FEAT_OK)) {
        kprintf("[ERROR] [NET] VirtIO-net features rejected\n");
        return 0;
    }
    CW16(v, VIRTIO_CFG_MSIX, 0xFFFFu);
    if (!QSetup(v, &v->rx, RXQ) || !QSetup(v, &v->tx, TXQ)) {
        kprintf("[ERROR] [NET] VirtIO-net queue setup failed\n");
        return 0;
    }
    for (i = 0; i < 6; i++)
        v->mac[i] = MmioR8(v->devcfg + i);
    for (i = 0; i < v->rx.qsz; i++) {
        v->rxVa[i] = DmaPage(&v->rxPa[i]);
        if (!v->rxVa[i])
            return 0;
    }
    v->txVa = DmaPage(&v->txPa);
    if (!v->txVa)
        return 0;
    RxFill(v);
    CW8(v, VIRTIO_CFG_STATUS,
         (uint8_t)(VIRTIO_ACK | VIRTIO_DRIVER | VIRTIO_FEAT_OK |
                   VIRTIO_DRIVER_OK));
    if (pci->irqLine && pci->irqLine < 16u && pci->irqLine != 0)
        IrqRegister(pci->irqLine, IrqNet, v);
    {
        char macs[18];
        MacFmt(v->mac, macs);
        kinfo("[INFO] [NET] VirtIO-net mac %s qsz %u irq %u\n",
                macs, (unsigned)v->rx.qsz, (unsigned)pci->irqLine);
    }
    return 1;
}

static int VnetProbe(RoboDevice *dev)
{
    RoboPciDevice *pci;
    if (!dev || !dev->busData)
        return -1;
    pci = (RoboPciDevice *)dev->busData;
    if (pci->vendor != VIRTIO_PCI_VENDOR)
        return -1;
    if (pci->device != VIRTIO_DEV_NET_MODERN &&
        pci->device != VIRTIO_DEV_NET_LEGACY)
        return -1;
    return 0;
}

static int VnetStart(RoboDevice *dev)
{
    if (gVnReady)
        return 0;
    if (!VnetSetup(&gVn, (RoboPciDevice *)dev->busData))
        return -1;
    gVn.ifp = NetifRegister("eth0", gVn.mac, VnetSend, &gVn);
    if (!gVn.ifp)
        return -1;
    gVnReady = 1;
    return 0;
}

static int VnetStop(RoboDevice *dev)
{
    (void)dev;
    return 0;
}

static int VnetRemove(RoboDevice *dev)
{
    (void)dev;
    return 0;
}

static const RoboDriverId vnetIds[] = {
    { RoboBusPci, VIRTIO_PCI_VENDOR, VIRTIO_DEV_NET_MODERN, 0, 0, 0, 0, 0,
      RoboMatchBus | RoboMatchVendor | RoboMatchDevice },
    { RoboBusPci, VIRTIO_PCI_VENDOR, VIRTIO_DEV_NET_LEGACY, 0, 0, 0, 0, 0,
      RoboMatchBus | RoboMatchVendor | RoboMatchDevice }
};

static RoboDriver vnetDrv = {
    "virtio-net",
    "Roboticela",
    0x00010000u,
    RoboDriverAbiMajor,
    RoboDriverAbiMinor,
    RoboCapPciConfig | RoboCapMmio | RoboCapDma | RoboCapIrq,
    RoboDrvChannelBuiltin,
    RoboDrvStable,
    RoboDrvDiscovered,
    1,
    vnetIds,
    2,
    VnetProbe,
    VnetStart,
    VnetStop,
    VnetRemove
};

int VirtioNetRegister(void)
{
    return DriverRegister(&vnetDrv);
}

int VirtioNetReady(void)
{
    return gVnReady;
}
