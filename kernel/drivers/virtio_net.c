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

struct vq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct vq_avail {
    volatile uint16_t flags;
    volatile uint16_t idx;
    uint16_t ring[VQ_MAX];
};

struct vq_used_elem {
    uint32_t id;
    uint32_t len;
};

struct vq_used {
    volatile uint16_t flags;
    volatile uint16_t idx;
    struct vq_used_elem ring[VQ_MAX];
};

struct vnet_q {
    uint16_t qsz;
    uint16_t last_used;
    uint16_t notify_off;
    struct vq_desc *desc;
    struct vq_avail *avail;
    struct vq_used *used;
    phys_addr_t desc_pa, avail_pa, used_pa;
};

struct virtio_net {
    RoboPciDevice *pci;
    volatile uint8_t *common;
    volatile uint8_t *notify;
    volatile uint8_t *devcfg;
    volatile uint8_t *isr;
    uint32_t notify_mul;
    uint8_t mac[6];
    struct vnet_q rx;
    struct vnet_q tx;
    phys_addr_t rx_pa[VQ_MAX];
    uint8_t *rx_va[VQ_MAX];
    phys_addr_t tx_pa;
    uint8_t *tx_va;
    uint16_t tx_free;
    RoboNetIf *ifp;
    volatile int pending;
};

static struct virtio_net g_vn;
static int g_vn_ready;

static void wmb(void)
{
    __asm__ volatile ("mfence" ::: "memory");
}

static void *dma_page(phys_addr_t *pa)
{
    phys_addr_t p = robo_page_alloc();
    uint8_t *v;
    unsigned i;
    if (p == ROBO_INVALID_PHYS_ADDR)
        return 0;
    v = (uint8_t *)(uintptr_t)p;
    for (i = 0; i < ROBO_PAGE_SIZE; i++)
        v[i] = 0;
    if (pa)
        *pa = p;
    return v;
}

static uint8_t *map_bar(const RoboPciDevice *d, uint8_t bar, uint32_t off,
                        uint32_t len)
{
    if (bar >= 6u)
        return 0;
    if (!(d->bar[bar].flags & ROBO_PCI_BAR_MEM) || !d->bar[bar].addr)
        return 0;
    if ((uint64_t)off + len > d->bar[bar].size)
        return 0;
    if (!robo_mmio_map((phys_addr_t)d->bar[bar].addr, d->bar[bar].size))
        return 0;
    return (uint8_t *)(uintptr_t)(d->bar[bar].addr + off);
}

static int find_caps(RoboPciDevice *d, struct virtio_net *v)
{
    uint16_t sts;
    uint8_t ptr;
    uint32_t notify_mul = 0;

    v->common = v->notify = v->devcfg = v->isr = 0;
    sts = robo_pci_cfg_read16(d->bus, d->dev, d->func, 0x06);
    if (!(sts & 0x10u))
        return 0;
    ptr = robo_pci_cfg_read8(d->bus, d->dev, d->func, 0x34);
    while (ptr && ptr != 0xFFu) {
        uint8_t id = robo_pci_cfg_read8(d->bus, d->dev, d->func, ptr);
        uint8_t next = robo_pci_cfg_read8(d->bus, d->dev, d->func, (uint8_t)(ptr + 1));
        if (id == 0x09u) {
            uint8_t type = robo_pci_cfg_read8(d->bus, d->dev, d->func, (uint8_t)(ptr + 3));
            uint8_t bar = robo_pci_cfg_read8(d->bus, d->dev, d->func, (uint8_t)(ptr + 4));
            uint32_t off = robo_pci_cfg_read32(d->bus, d->dev, d->func, (uint8_t)(ptr + 8));
            uint32_t len = robo_pci_cfg_read32(d->bus, d->dev, d->func, (uint8_t)(ptr + 12));
            uint8_t *base = map_bar(d, bar, off, len ? len : 4u);
            if (type == VIRTIO_PCI_CAP_COMMON)
                v->common = base;
            else if (type == VIRTIO_PCI_CAP_NOTIFY) {
                v->notify = base;
                notify_mul = robo_pci_cfg_read32(d->bus, d->dev, d->func,
                                                 (uint8_t)(ptr + 16));
            } else if (type == VIRTIO_PCI_CAP_DEVICE)
                v->devcfg = base;
            else if (type == VIRTIO_PCI_CAP_ISR)
                v->isr = base;
        }
        ptr = next;
    }
    v->notify_mul = notify_mul;
    return v->common && v->notify && v->devcfg;
}

static uint8_t c_r8(struct virtio_net *v, unsigned off)
{
    return robo_mmio_r8(v->common + off);
}

static void c_w8(struct virtio_net *v, unsigned off, uint8_t x)
{
    robo_mmio_w8(v->common + off, x);
}

static uint16_t c_r16(struct virtio_net *v, unsigned off)
{
    return robo_mmio_r16(v->common + off);
}

static void c_w16(struct virtio_net *v, unsigned off, uint16_t x)
{
    robo_mmio_w16(v->common + off, x);
}

static uint32_t c_r32(struct virtio_net *v, unsigned off)
{
    return robo_mmio_r32(v->common + off);
}

static void c_w32(struct virtio_net *v, unsigned off, uint32_t x)
{
    robo_mmio_w32(v->common + off, x);
}

static void c_w64(struct virtio_net *v, unsigned off, uint64_t x)
{
    robo_mmio_w64(v->common + off, x);
}

static void q_notify(struct virtio_net *v, struct vnet_q *q)
{
    uint16_t noff = q->notify_off;
    if (!v->notify_mul)
        robo_mmio_w16(v->notify + noff, 0);
    else
        robo_mmio_w16(v->notify + (uint32_t)noff * v->notify_mul, 0);
}

static int q_setup(struct virtio_net *v, struct vnet_q *q, uint16_t sel)
{
    uint16_t qsz;
    c_w16(v, VIRTIO_CFG_QSEL, sel);
    qsz = c_r16(v, VIRTIO_CFG_QSIZE);
    if (!qsz)
        return 0;
    if (qsz > 16u) {
        qsz = 16u;
        c_w16(v, VIRTIO_CFG_QSIZE, qsz);
    }
    q->qsz = qsz;
    q->desc = dma_page(&q->desc_pa);
    q->avail = dma_page(&q->avail_pa);
    q->used = dma_page(&q->used_pa);
    if (!q->desc || !q->avail || !q->used)
        return 0;
    q->notify_off = c_r16(v, VIRTIO_CFG_QNOTIFY);
    c_w16(v, VIRTIO_CFG_QMSIX, 0xFFFFu);
    c_w64(v, VIRTIO_CFG_QDESC, q->desc_pa);
    c_w64(v, VIRTIO_CFG_QDRIVER, q->avail_pa);
    c_w64(v, VIRTIO_CFG_QDEVICE, q->used_pa);
    c_w16(v, VIRTIO_CFG_QENABLE, 1);
    q->last_used = 0;
    return 1;
}

static void rx_fill(struct virtio_net *v)
{
    unsigned i;
    for (i = 0; i < v->rx.qsz; i++) {
        v->rx.desc[i].addr = v->rx_pa[i];
        v->rx.desc[i].len = (uint32_t)ROBO_PAGE_SIZE;
        v->rx.desc[i].flags = VIRTQ_WRITE;
        v->rx.desc[i].next = 0;
        v->rx.avail->ring[i] = (uint16_t)i;
    }
    wmb();
    v->rx.avail->idx = v->rx.qsz;
    wmb();
    q_notify(v, &v->rx);
}

static void irq_net(unsigned irq, void *ctx)
{
    struct virtio_net *v = (struct virtio_net *)ctx;
    (void)irq;
    if (v->isr)
        (void)robo_mmio_r8(v->isr);
    v->pending = 1;
}

static int vnet_send(RoboNetIf *ifp, const void *data, unsigned len)
{
    struct virtio_net *v = ifp ? (struct virtio_net *)ifp->drv : 0;
    uint16_t last, id;
    unsigned i, spins = 0;
    uint8_t *b;
    if (!v || !data || !len || len + VNET_HDR > ROBO_PAGE_SIZE)
        return -1;
    last = v->tx.used->idx;
    b = v->tx_va;
    for (i = 0; i < VNET_HDR; i++)
        b[i] = 0;
    robo_net_memcpy(b + VNET_HDR, data, len);
    id = 0;
    v->tx.desc[0].addr = v->tx_pa;
    v->tx.desc[0].len = VNET_HDR + len;
    v->tx.desc[0].flags = 0;
    v->tx.desc[0].next = 0;
    wmb();
    v->tx.avail->ring[v->tx.avail->idx % v->tx.qsz] = id;
    wmb();
    v->tx.avail->idx = (uint16_t)(v->tx.avail->idx + 1);
    wmb();
    q_notify(v, &v->tx);
    while (v->tx.used->idx == last) {
        __asm__ volatile ("pause");
        if (++spins > 2000000u)
            return -1;
    }
    v->tx.last_used = v->tx.used->idx;
    return 0;
}

static void rx_harvest(struct virtio_net *v)
{
    while (v->rx.last_used != v->rx.used->idx) {
        struct vq_used_elem *e = &v->rx.used->ring[v->rx.last_used % v->rx.qsz];
        uint16_t id = (uint16_t)e->id;
        uint32_t len = e->len;
        uint8_t *va;
        wmb();
        if (id < v->rx.qsz && len > VNET_HDR) {
            va = v->rx_va[id];
            if (v->ifp)
                robo_netif_rx(v->ifp, va + VNET_HDR, (unsigned)(len - VNET_HDR));
            v->rx.desc[id].addr = v->rx_pa[id];
            v->rx.desc[id].len = (uint32_t)ROBO_PAGE_SIZE;
            v->rx.desc[id].flags = VIRTQ_WRITE;
            wmb();
            v->rx.avail->ring[v->rx.avail->idx % v->rx.qsz] = id;
            wmb();
            v->rx.avail->idx = (uint16_t)(v->rx.avail->idx + 1);
        }
        v->rx.last_used = (uint16_t)(v->rx.last_used + 1);
    }
}

void robo_virtio_net_poll(void)
{
    if (!g_vn_ready)
        return;
    g_vn.pending = 0;
    if (g_vn.isr)
        (void)robo_mmio_r8(g_vn.isr);
    rx_harvest(&g_vn);
}

static int vnet_setup(struct virtio_net *v, RoboPciDevice *pci)
{
    uint32_t lo, hi;
    uint64_t feat, drv;
    unsigned i;

    v->pci = pci;
    robo_pci_enable_io(pci);
    robo_pci_enable_mmio(pci);
    robo_pci_enable_busmaster(pci);
    if (!find_caps(pci, v)) {
        kprintf("[ERROR] [NET] VirtIO-net caps missing\n");
        return 0;
    }
    c_w8(v, VIRTIO_CFG_STATUS, 0);
    c_w8(v, VIRTIO_CFG_STATUS, VIRTIO_ACK);
    c_w8(v, VIRTIO_CFG_STATUS, (uint8_t)(VIRTIO_ACK | VIRTIO_DRIVER));
    c_w32(v, VIRTIO_CFG_DFSELECT, 0);
    lo = c_r32(v, VIRTIO_CFG_DF);
    c_w32(v, VIRTIO_CFG_DFSELECT, 1);
    hi = c_r32(v, VIRTIO_CFG_DF);
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
    c_w32(v, VIRTIO_CFG_GFSELECT, 0);
    c_w32(v, VIRTIO_CFG_GF, (uint32_t)drv);
    c_w32(v, VIRTIO_CFG_GFSELECT, 1);
    c_w32(v, VIRTIO_CFG_GF, (uint32_t)(drv >> 32));
    wmb();
    c_w8(v, VIRTIO_CFG_STATUS,
         (uint8_t)(VIRTIO_ACK | VIRTIO_DRIVER | VIRTIO_FEAT_OK));
    wmb();
    if (!(c_r8(v, VIRTIO_CFG_STATUS) & VIRTIO_FEAT_OK)) {
        kprintf("[ERROR] [NET] VirtIO-net features rejected\n");
        return 0;
    }
    c_w16(v, VIRTIO_CFG_MSIX, 0xFFFFu);
    if (!q_setup(v, &v->rx, RXQ) || !q_setup(v, &v->tx, TXQ)) {
        kprintf("[ERROR] [NET] VirtIO-net queue setup failed\n");
        return 0;
    }
    for (i = 0; i < 6; i++)
        v->mac[i] = robo_mmio_r8(v->devcfg + i);
    for (i = 0; i < v->rx.qsz; i++) {
        v->rx_va[i] = dma_page(&v->rx_pa[i]);
        if (!v->rx_va[i])
            return 0;
    }
    v->tx_va = dma_page(&v->tx_pa);
    if (!v->tx_va)
        return 0;
    rx_fill(v);
    c_w8(v, VIRTIO_CFG_STATUS,
         (uint8_t)(VIRTIO_ACK | VIRTIO_DRIVER | VIRTIO_FEAT_OK |
                   VIRTIO_DRIVER_OK));
    if (pci->irq_line && pci->irq_line < 16u && pci->irq_line != 0)
        robo_irq_register(pci->irq_line, irq_net, v);
    {
        char macs[18];
        robo_mac_fmt(v->mac, macs);
        kprintf("[INFO] [NET] VirtIO-net mac %s qsz %u irq %u\n",
                macs, (unsigned)v->rx.qsz, (unsigned)pci->irq_line);
    }
    return 1;
}

static int vnet_probe(RoboDevice *dev)
{
    RoboPciDevice *pci;
    if (!dev || !dev->bus_data)
        return -1;
    pci = (RoboPciDevice *)dev->bus_data;
    if (pci->vendor != VIRTIO_PCI_VENDOR)
        return -1;
    if (pci->device != VIRTIO_DEV_NET_MODERN &&
        pci->device != VIRTIO_DEV_NET_LEGACY)
        return -1;
    return 0;
}

static int vnet_start(RoboDevice *dev)
{
    if (g_vn_ready)
        return 0;
    if (!vnet_setup(&g_vn, (RoboPciDevice *)dev->bus_data))
        return -1;
    g_vn.ifp = robo_netif_register("eth0", g_vn.mac, vnet_send, &g_vn);
    if (!g_vn.ifp)
        return -1;
    g_vn_ready = 1;
    return 0;
}

static int vnet_stop(RoboDevice *dev)
{
    (void)dev;
    return 0;
}

static int vnet_remove(RoboDevice *dev)
{
    (void)dev;
    return 0;
}

static const RoboDriverId vnet_ids[] = {
    { ROBO_BUS_PCI, VIRTIO_PCI_VENDOR, VIRTIO_DEV_NET_MODERN, 0, 0, 0, 0, 0,
      ROBO_MATCH_BUS | ROBO_MATCH_VENDOR | ROBO_MATCH_DEVICE },
    { ROBO_BUS_PCI, VIRTIO_PCI_VENDOR, VIRTIO_DEV_NET_LEGACY, 0, 0, 0, 0, 0,
      ROBO_MATCH_BUS | ROBO_MATCH_VENDOR | ROBO_MATCH_DEVICE }
};

static RoboDriver vnet_drv = {
    "virtio-net",
    "Roboticela",
    0x00010000u,
    ROBO_DRIVER_ABI_MAJOR,
    ROBO_DRIVER_ABI_MINOR,
    ROBO_CAP_PCI_CONFIG | ROBO_CAP_MMIO | ROBO_CAP_DMA | ROBO_CAP_IRQ,
    ROBO_DRV_CHANNEL_BUILTIN,
    ROBO_DRV_STABLE,
    ROBO_DRV_DISCOVERED,
    1,
    vnet_ids,
    2,
    vnet_probe,
    vnet_start,
    vnet_stop,
    vnet_remove
};

int robo_virtio_net_register(void)
{
    return robo_driver_register(&vnet_drv);
}

int robo_virtio_net_ready(void)
{
    return g_vn_ready;
}
