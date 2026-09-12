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

struct virtio_blk {
    RoboPciDevice *pci;
    volatile uint8_t *common;
    volatile uint8_t *notify;
    volatile uint8_t *devcfg;
    uint32_t notify_mul;
    uint16_t qsz;
    uint16_t last_used;
    struct vq_desc *desc;
    struct vq_avail *avail;
    struct vq_used *used;
    phys_addr_t desc_pa, avail_pa, used_pa, hdr_pa, bounce_pa;
    uint8_t *hdr;
    uint8_t *bounce;
    uint64_t sectors;
    int flush_ok;
    int legacy;
    uint16_t iobase;
    RoboBlockDevice bdev;
};

static struct virtio_blk g_vb;
static int g_vb_ready;

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

static int find_caps(RoboPciDevice *d, struct virtio_blk *v)
{
    uint16_t sts;
    uint8_t ptr;
    uint32_t notify_mul = 0;

    v->common = v->notify = v->devcfg = 0;
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
        }
        ptr = next;
    }
    v->notify_mul = notify_mul;
    kprintf("[INFO] [DRIVER] VirtIO caps common=%llx notify=%llx cfg=%llx\n",
            (unsigned long long)(uintptr_t)v->common,
            (unsigned long long)(uintptr_t)v->notify,
            (unsigned long long)(uintptr_t)v->devcfg);
    return v->common && v->notify && v->devcfg;
}

static uint8_t c_r8(struct virtio_blk *v, unsigned off)
{
    return robo_mmio_r8(v->common + off);
}

static void c_w8(struct virtio_blk *v, unsigned off, uint8_t x)
{
    robo_mmio_w8(v->common + off, x);
}

static uint16_t c_r16(struct virtio_blk *v, unsigned off)
{
    return robo_mmio_r16(v->common + off);
}

static void c_w16(struct virtio_blk *v, unsigned off, uint16_t x)
{
    robo_mmio_w16(v->common + off, x);
}

static uint32_t c_r32(struct virtio_blk *v, unsigned off)
{
    return robo_mmio_r32(v->common + off);
}

static void c_w32(struct virtio_blk *v, unsigned off, uint32_t x)
{
    robo_mmio_w32(v->common + off, x);
}

static void c_w64(struct virtio_blk *v, unsigned off, uint64_t x)
{
    robo_mmio_w64(v->common + off, x);
}

static int vq_submit(struct virtio_blk *v, uint32_t type, uint64_t sector,
                     void *buf, uint32_t len, int write_dev)
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
        if (len > ROBO_PAGE_SIZE)
            return -ROBO_EINVAL;
        if (write_dev) {
            uint8_t *src = (uint8_t *)buf;
            for (i = 0; i < len; i++)
                v->bounce[i] = src[i];
        }
    }

    v->desc[0].addr = v->hdr_pa;
    v->desc[0].len = 16;
    v->desc[0].flags = VIRTQ_NEXT;
    v->desc[0].next = 1;
    if (type == VIRTIO_BLK_T_FLUSH || !buf || !len) {
        v->desc[1].addr = v->hdr_pa + 16;
        v->desc[1].len = 1;
        v->desc[1].flags = VIRTQ_WRITE;
        v->desc[1].next = 0;
    } else {
        v->desc[1].addr = v->bounce_pa;
        v->desc[1].len = len;
        v->desc[1].flags = (uint16_t)(VIRTQ_NEXT | (write_dev ? 0u : VIRTQ_WRITE));
        v->desc[1].next = 2;
        v->desc[2].addr = v->hdr_pa + 16;
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
        uint16_t noff = v->legacy ? 0 : c_r16(v, VIRTIO_CFG_QNOTIFY);
        if (v->legacy)
            robo_outw((uint16_t)(v->iobase + 16u), 0);
        else
            robo_mmio_w16(v->notify + (uint32_t)noff * v->notify_mul, 0);
    }
    while (v->used->idx == last) {
        __asm__ volatile ("pause");
        if (++spins > 20000000u)
            return -ROBO_EIO;
    }
    wmb();
    if (st[0] != 0)
        return -ROBO_EIO;
    if (buf && len && !write_dev) {
        uint8_t *dst = (uint8_t *)buf;
        for (i = 0; i < len; i++)
            dst[i] = v->bounce[i];
    }
    return 0;
}

static int vb_read(RoboBlockDevice *dev, uint64_t lba, void *buf)
{
    struct virtio_blk *v = (struct virtio_blk *)dev->priv;
    uint64_t sec = lba * (dev->block_size / 512u);
    return vq_submit(v, VIRTIO_BLK_T_IN, sec, buf, dev->block_size, 0);
}

static int vb_write(RoboBlockDevice *dev, uint64_t lba, const void *buf)
{
    struct virtio_blk *v = (struct virtio_blk *)dev->priv;
    uint64_t sec = lba * (dev->block_size / 512u);
    return vq_submit(v, VIRTIO_BLK_T_OUT, sec, (void *)buf, dev->block_size, 1);
}

static int vb_flush(RoboBlockDevice *dev)
{
    struct virtio_blk *v = (struct virtio_blk *)dev->priv;
    if (!v->flush_ok)
        return 0;
    return vq_submit(v, VIRTIO_BLK_T_FLUSH, 0, 0, 0, 0);
}

static int virtio_setup_legacy(struct virtio_blk *v, RoboPciDevice *pci)
{
    unsigned i;
    uint16_t qsz;
    uint32_t hostf, guestf;
    uint32_t avail_off, used_off, bytes, np;
    phys_addr_t pages[8];
    uint8_t *base;

    v->legacy = 1;
    v->iobase = 0;
    for (i = 0; i < 6; i++) {
        if ((pci->bar[i].flags & ROBO_PCI_BAR_IO) && pci->bar[i].addr) {
            v->iobase = (uint16_t)pci->bar[i].addr;
            break;
        }
    }
    if (!v->iobase) {
        kprintf("[ERROR] [DRIVER] VirtIO legacy BAR missing\n");
        return 0;
    }
    robo_pci_enable_io(pci);
    robo_pci_enable_busmaster(pci);
    robo_outb((uint16_t)(v->iobase + 18u), 0);
    robo_outb((uint16_t)(v->iobase + 18u), VIRTIO_ACK);
    robo_outb((uint16_t)(v->iobase + 18u), (uint8_t)(VIRTIO_ACK | VIRTIO_DRIVER));
    hostf = robo_inl((uint16_t)(v->iobase + 0u));
    guestf = hostf & (uint32_t)VIRTIO_BLK_F_FLUSH;
    v->flush_ok = guestf != 0;
    robo_outl((uint16_t)(v->iobase + 4u), guestf);
    robo_outw((uint16_t)(v->iobase + 14u), 0);
    qsz = robo_inw((uint16_t)(v->iobase + 12u));
    if (!qsz || qsz > VQ_MAX) {
        kprintf("[ERROR] [DRIVER] VirtIO legacy qsz %u\n", (unsigned)qsz);
        return 0;
    }
    v->qsz = qsz;
    avail_off = 16u * qsz;
    used_off = (avail_off + 6u + 2u * qsz + 4095u) & ~4095u;
    bytes = used_off + 6u + 8u * qsz;
    bytes = (bytes + 4095u) & ~4095u;
    np = bytes / (uint32_t)ROBO_PAGE_SIZE;
    if (!np || np > 8u) {
        kprintf("[ERROR] [DRIVER] VirtIO vring too big\n");
        return 0;
    }
    for (i = 0; i < np; i++) {
        pages[i] = robo_page_alloc();
        if (pages[i] == ROBO_INVALID_PHYS_ADDR)
            return 0;
        if (i && pages[i] != pages[0] + i * ROBO_PAGE_SIZE) {
            kprintf("[ERROR] [DRIVER] VirtIO DMA not contiguous\n");
            return 0;
        }
        {
            uint8_t *p = (uint8_t *)(uintptr_t)pages[i];
            unsigned k;
            for (k = 0; k < ROBO_PAGE_SIZE; k++)
                p[k] = 0;
        }
    }
    base = (uint8_t *)(uintptr_t)pages[0];
    v->desc = (struct vq_desc *)base;
    v->avail = (struct vq_avail *)(base + avail_off);
    v->used = (struct vq_used *)(base + used_off);
    v->desc_pa = pages[0];
    v->avail_pa = pages[0] + avail_off;
    v->used_pa = pages[0] + used_off;
    v->hdr = dma_page(&v->hdr_pa);
    v->bounce = dma_page(&v->bounce_pa);
    if (!v->hdr || !v->bounce)
        return 0;
    robo_outl((uint16_t)(v->iobase + 8u), (uint32_t)(pages[0] >> 12));
    {
        uint32_t lo = robo_inl((uint16_t)(v->iobase + 20u));
        uint32_t hi = robo_inl((uint16_t)(v->iobase + 24u));
        v->sectors = (uint64_t)lo | ((uint64_t)hi << 32);
    }
    if (v->sectors < 8ull || v->sectors > 32768ull) {
        kprintf("[ERROR] [DRIVER] VirtIO capacity %llu\n",
                (unsigned long long)v->sectors);
        return 0;
    }
    v->last_used = 0;
    robo_outb((uint16_t)(v->iobase + 18u),
              (uint8_t)(VIRTIO_ACK | VIRTIO_DRIVER | VIRTIO_DRIVER_OK));
    v->bdev.block_size = 4096;
    v->bdev.block_count = v->sectors / 8ull;
    v->bdev.priv = v;
    v->bdev.read = vb_read;
    v->bdev.write = vb_write;
    v->bdev.flush = vb_flush;
    v->bdev.destroy = 0;
    kprintf("[INFO] [DRIVER] VirtIO legacy I/O 0x%x qsz %u\n",
            (unsigned)v->iobase, (unsigned)qsz);
    return 1;
}

static int virtio_setup(struct virtio_blk *v, RoboPciDevice *pci)
{
    uint32_t lo, hi;
    uint64_t feat, drv;
    uint16_t qsz;

    v->pci = pci;
    v->legacy = 0;
    v->iobase = 0;
    robo_pci_enable_io(pci);
    robo_pci_enable_mmio(pci);
    robo_pci_enable_busmaster(pci);
    if (!find_caps(pci, v)) {
        kprintf("[INFO] [DRIVER] VirtIO modern caps missing\n");
        return virtio_setup_legacy(v, pci);
    }

    c_w8(v, VIRTIO_CFG_STATUS, 0);
    c_w8(v, VIRTIO_CFG_STATUS, VIRTIO_ACK);
    c_w8(v, VIRTIO_CFG_STATUS, (uint8_t)(VIRTIO_ACK | VIRTIO_DRIVER));
    kprintf("[INFO] [DRIVER] VirtIO status after ack %x\n",
            (unsigned)c_r8(v, VIRTIO_CFG_STATUS));

    c_w32(v, VIRTIO_CFG_DFSELECT, 0);
    lo = c_r32(v, VIRTIO_CFG_DF);
    c_w32(v, VIRTIO_CFG_DFSELECT, 1);
    hi = c_r32(v, VIRTIO_CFG_DF);
    feat = (uint64_t)lo | ((uint64_t)hi << 32);
    drv = VIRTIO_F_VERSION_1;
    if (!(feat & VIRTIO_F_VERSION_1)) {
        kprintf("[ERROR] [DRIVER] VirtIO missing VERSION_1 feat=%llx\n",
                (unsigned long long)feat);
        return virtio_setup_legacy(v, pci);
    }
    if (feat & VIRTIO_BLK_F_FLUSH)
        drv |= VIRTIO_BLK_F_FLUSH;
    v->flush_ok = (drv & VIRTIO_BLK_F_FLUSH) != 0;
    kprintf("[INFO] [DRIVER] VirtIO feat=%llx drv=%llx\n",
            (unsigned long long)feat, (unsigned long long)drv);
    c_w32(v, VIRTIO_CFG_GFSELECT, 0);
    c_w32(v, VIRTIO_CFG_GF, (uint32_t)drv);
    c_w32(v, VIRTIO_CFG_GFSELECT, 1);
    c_w32(v, VIRTIO_CFG_GF, (uint32_t)(drv >> 32));
    wmb();
    c_w8(v, VIRTIO_CFG_STATUS,
         (uint8_t)(VIRTIO_ACK | VIRTIO_DRIVER | VIRTIO_FEAT_OK));
    wmb();
    {
        uint8_t st = c_r8(v, VIRTIO_CFG_STATUS);
        kprintf("[INFO] [DRIVER] VirtIO status after feat %x\n", (unsigned)st);
        if (!(st & VIRTIO_FEAT_OK)) {
            kprintf("[ERROR] [DRIVER] VirtIO features rejected feat=%llx\n",
                    (unsigned long long)feat);
            return virtio_setup_legacy(v, pci);
        }
    }

    c_w16(v, VIRTIO_CFG_MSIX, 0xFFFFu);
    c_w16(v, VIRTIO_CFG_QSEL, 0);
    qsz = c_r16(v, VIRTIO_CFG_QSIZE);
    if (!qsz) {
        kprintf("[ERROR] [DRIVER] VirtIO queue size 0\n");
        return virtio_setup_legacy(v, pci);
    }
    if (qsz > 16u) {
        qsz = 16u;
        c_w16(v, VIRTIO_CFG_QSIZE, qsz);
    }
    v->qsz = qsz;
    kprintf("[INFO] [DRIVER] VirtIO qsz %u notify_mul %u\n",
            (unsigned)qsz, (unsigned)v->notify_mul);
    v->desc = dma_page(&v->desc_pa);
    v->avail = dma_page(&v->avail_pa);
    v->used = dma_page(&v->used_pa);
    v->hdr = dma_page(&v->hdr_pa);
    v->bounce = dma_page(&v->bounce_pa);
    if (!v->desc || !v->avail || !v->used || !v->hdr || !v->bounce) {
        kprintf("[ERROR] [DRIVER] VirtIO DMA alloc failed\n");
        return 0;
    }
    c_w16(v, VIRTIO_CFG_QMSIX, 0xFFFFu);
    c_w64(v, VIRTIO_CFG_QDESC, v->desc_pa);
    c_w64(v, VIRTIO_CFG_QDRIVER, v->avail_pa);
    c_w64(v, VIRTIO_CFG_QDEVICE, v->used_pa);
    c_w16(v, VIRTIO_CFG_QENABLE, 1);

    v->sectors = robo_mmio_r64(v->devcfg);
    if (v->sectors < 8ull || v->sectors > 32768ull) {
        kprintf("[ERROR] [DRIVER] VirtIO capacity %llu\n",
                (unsigned long long)v->sectors);
        return 0;
    }
    v->last_used = 0;
    c_w8(v, VIRTIO_CFG_STATUS,
         (uint8_t)(VIRTIO_ACK | VIRTIO_DRIVER | VIRTIO_FEAT_OK |
                   VIRTIO_DRIVER_OK));
    kprintf("[INFO] [DRIVER] VirtIO status driver_ok %x\n",
            (unsigned)c_r8(v, VIRTIO_CFG_STATUS));

    v->bdev.block_size = 4096;
    v->bdev.block_count = v->sectors / 8ull;
    v->bdev.priv = v;
    v->bdev.read = vb_read;
    v->bdev.write = vb_write;
    v->bdev.flush = vb_flush;
    v->bdev.destroy = 0;
    return 1;
}

static int virtio_probe(RoboDevice *dev)
{
    RoboPciDevice *pci;
    if (!dev || !dev->bus_data)
        return -1;
    pci = (RoboPciDevice *)dev->bus_data;
    if (pci->vendor != VIRTIO_PCI_VENDOR)
        return -1;
    if (pci->device != VIRTIO_DEV_BLK_LEGACY &&
        pci->device != VIRTIO_DEV_BLK_MODERN)
        return -1;
    return 0;
}

static int virtio_start(RoboDevice *dev)
{
    if (g_vb_ready)
        return 0;
    if (!virtio_setup(&g_vb, (RoboPciDevice *)dev->bus_data)) {
        kprintf("[ERROR] [DRIVER] VirtIO Block init failed\n");
        return -1;
    }
    g_vb_ready = 1;
    kprintf("[INFO] [DRIVER] VirtIO Block detected\n");
    kprintf("[INFO] [DRIVER] Device %x:%x\n",
            (unsigned)g_vb.pci->vendor, (unsigned)g_vb.pci->device);
    kprintf("[INFO] [DRIVER] sectors %llu  blocks %llu\n",
            (unsigned long long)g_vb.sectors,
            (unsigned long long)g_vb.bdev.block_count);
    return 0;
}

static int virtio_stop(RoboDevice *dev)
{
    (void)dev;
    return 0;
}

static int virtio_remove(RoboDevice *dev)
{
    (void)dev;
    return 0;
}

static const RoboDriverId virtio_ids[] = {
    { ROBO_BUS_PCI, VIRTIO_PCI_VENDOR, VIRTIO_DEV_BLK_MODERN, 0, 0, 0, 0, 0,
      ROBO_MATCH_BUS | ROBO_MATCH_VENDOR | ROBO_MATCH_DEVICE },
    { ROBO_BUS_PCI, VIRTIO_PCI_VENDOR, VIRTIO_DEV_BLK_LEGACY, 0, 0, 0, 0, 0,
      ROBO_MATCH_BUS | ROBO_MATCH_VENDOR | ROBO_MATCH_DEVICE }
};

static RoboDriver virtio_drv = {
    "virtio-blk",
    "Roboticela",
    0x00010000u,
    ROBO_DRIVER_ABI_MAJOR,
    ROBO_DRIVER_ABI_MINOR,
    ROBO_CAP_PCI_CONFIG | ROBO_CAP_MMIO | ROBO_CAP_DMA | ROBO_CAP_BLOCK_IO,
    ROBO_DRV_CHANNEL_BUILTIN,
    ROBO_DRV_STABLE,
    ROBO_DRV_DISCOVERED,
    1,
    virtio_ids,
    2,
    virtio_probe,
    virtio_start,
    virtio_stop,
    virtio_remove
};

int robo_virtio_blk_register(void)
{
    return robo_driver_register(&virtio_drv);
}

RoboBlockDevice *robo_virtio_blk_bdev(void)
{
    return g_vb_ready ? &g_vb.bdev : 0;
}

int robo_storage_selftest(void)
{
    RoboBlockDevice *d = robo_virtio_blk_bdev();
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
    if (d->block_size != 4096 || d->block_count < 16)
        return 0;
    for (i = 0; i < 4096; i++)
        a[i] = (uint8_t)(i * 3u + 1u);
    if (robo_bdev_write(d, 2, a) != 0)
        ok = 0;
    if (robo_bdev_flush(d) != 0)
        ok = 0;
    for (i = 0; i < 4096; i++)
        b[i] = 0;
    if (robo_bdev_read(d, 2, b) != 0)
        ok = 0;
    for (i = 0; i < 4096; i++) {
        if (a[i] != b[i])
            ok = 0;
    }
    if (robo_bdev_write_n(d, 3, 2, a) != 0)
        ok = 0;
    if (robo_bdev_read_n(d, 3, 2, b) != 0)
        ok = 0;
    if (robo_bdev_read(d, d->block_count, a) == 0)
        ok = 0;
    if (ok && robofs_format(d) == 0 && robofs_mount(d, 0, &m) == 0 && m) {
        if (m->root && m->root->ops && m->root->ops->create &&
            m->root->ops->create(m->root, "disk.txt", ROBO_VNODE_FILE, 0, &n) == 0 && n) {
            nr = robo_vnode_write(n, 0, "virtio-robofs", 13, 0);
            if (nr != 13)
                ok = 0;
            robo_vnode_put(n);
            n = 0;
        } else {
            ok = 0;
        }
        robofs_unmount(m);
        m = 0;
        if (robofs_mount(d, 0, &m) == 0 && m && m->root &&
            m->root->ops->lookup(m->root, "disk.txt", &n) == 0 && n) {
            nr = robo_vnode_read(n, 0, rbuf, 13, 0);
            if (nr != 13 || rbuf[0] != 'v' || rbuf[12] != 's')
                ok = 0;
            robo_vnode_put(n);
        } else {
            ok = 0;
        }
        if (m)
            robofs_unmount(m);
    } else {
        ok = 0;
    }
    kprintf("STORAGE_TEST  virtio-blk %s\n", ok ? "ok" : "fail");
    return ok;
}
