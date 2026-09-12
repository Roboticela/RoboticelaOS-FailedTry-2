#include "pci/pci.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/acpi/acpi.h"
#include "arch/x86_64/mmio.h"
#include "kio.h"

static RoboPciDevice g_dev[ROBO_PCI_MAX];
static unsigned g_n;
static uint8_t *g_ecam;
static uint8_t g_ecam_start, g_ecam_end;
static int g_use_ecam;

static uint32_t pio_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
{
    uint32_t addr = 0x80000000u |
                    ((uint32_t)bus << 16) |
                    ((uint32_t)(dev & 31u) << 11) |
                    ((uint32_t)(func & 7u) << 8) |
                    (uint32_t)(off & 0xFCu);
    robo_outl(0xCF8, addr);
    return robo_inl(0xCFC);
}

static void pio_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off,
                      uint32_t v)
{
    uint32_t addr = 0x80000000u |
                    ((uint32_t)bus << 16) |
                    ((uint32_t)(dev & 31u) << 11) |
                    ((uint32_t)(func & 7u) << 8) |
                    (uint32_t)(off & 0xFCu);
    robo_outl(0xCF8, addr);
    robo_outl(0xCFC, v);
}

uint32_t robo_pci_cfg_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
{
    if (g_use_ecam && g_ecam && bus >= g_ecam_start && bus <= g_ecam_end) {
        uint32_t idx = ((uint32_t)(bus - g_ecam_start) << 20) |
                       ((uint32_t)(dev & 31u) << 15) |
                       ((uint32_t)(func & 7u) << 12) |
                       (uint32_t)(off & 0xFFCu);
        return *(volatile uint32_t *)(g_ecam + idx);
    }
    return pio_read(bus, dev, func, off);
}

void robo_pci_cfg_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off,
                          uint32_t v)
{
    if (g_use_ecam && g_ecam && bus >= g_ecam_start && bus <= g_ecam_end) {
        uint32_t idx = ((uint32_t)(bus - g_ecam_start) << 20) |
                       ((uint32_t)(dev & 31u) << 15) |
                       ((uint32_t)(func & 7u) << 12) |
                       (uint32_t)(off & 0xFFCu);
        *(volatile uint32_t *)(g_ecam + idx) = v;
        return;
    }
    pio_write(bus, dev, func, off, v);
}

uint16_t robo_pci_cfg_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
{
    uint32_t v = robo_pci_cfg_read32(bus, dev, func, (uint8_t)(off & 0xFCu));
    return (uint16_t)(v >> (8u * (off & 2u)));
}

uint8_t robo_pci_cfg_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
{
    uint32_t v = robo_pci_cfg_read32(bus, dev, func, (uint8_t)(off & 0xFCu));
    return (uint8_t)(v >> (8u * (off & 3u)));
}

static uint32_t bar_raw(uint8_t bus, uint8_t slot, uint8_t fn, unsigned bar)
{
    return robo_pci_cfg_read32(bus, slot, fn, (uint8_t)(0x10u + bar * 4u));
}

static void decode_bars(RoboPciDevice *d)
{
    unsigned i;
    uint8_t nbar = (d->hdr & 0x7fu) == 0x01u ? 2u : 6u;

    for (i = 0; i < 6; i++) {
        d->bar[i].addr = 0;
        d->bar[i].size = 0;
        d->bar[i].flags = 0;
    }
    for (i = 0; i < nbar; i++) {
        uint8_t off = (uint8_t)(0x10u + i * 4u);
        uint32_t orig = robo_pci_cfg_read32(d->bus, d->dev, d->func, off);
        uint32_t mask;
        uint64_t size;

        if (!orig)
            continue;
        robo_pci_cfg_write32(d->bus, d->dev, d->func, off, 0xFFFFFFFFu);
        mask = robo_pci_cfg_read32(d->bus, d->dev, d->func, off);
        robo_pci_cfg_write32(d->bus, d->dev, d->func, off, orig);
        if (orig & 1u) {
            d->bar[i].flags = ROBO_PCI_BAR_IO;
            d->bar[i].addr = orig & 0xFFFFFFFCu;
            size = (uint64_t)((~(mask & 0xFFFFFFFCu)) + 1u);
            d->bar[i].size = size & 0xFFFFull;
            continue;
        }
        d->bar[i].flags = ROBO_PCI_BAR_MEM;
        if (orig & 8u)
            d->bar[i].flags |= ROBO_PCI_BAR_PRE;
        if (((orig >> 1) & 3u) == 2u && i + 1 < nbar) {
            uint32_t orig_hi, mask_hi;
            uint8_t off_hi = (uint8_t)(off + 4u);
            orig_hi = robo_pci_cfg_read32(d->bus, d->dev, d->func, off_hi);
            robo_pci_cfg_write32(d->bus, d->dev, d->func, off_hi, 0xFFFFFFFFu);
            mask_hi = robo_pci_cfg_read32(d->bus, d->dev, d->func, off_hi);
            robo_pci_cfg_write32(d->bus, d->dev, d->func, off_hi, orig_hi);
            d->bar[i].flags |= ROBO_PCI_BAR_64;
            d->bar[i].addr = (uint64_t)(orig & 0xFFFFFFF0u) |
                             ((uint64_t)orig_hi << 32);
            size = ~(((uint64_t)mask_hi << 32) | (uint64_t)(mask & 0xFFFFFFF0u)) + 1ull;
            d->bar[i].size = size;
            i++;
            continue;
        }
        d->bar[i].addr = orig & 0xFFFFFFF0u;
        d->bar[i].size = (uint64_t)((~(mask & 0xFFFFFFF0u)) + 1u);
        (void)bar_raw;
    }
}

static void add_dev(uint8_t bus, uint8_t slot, uint8_t fn)
{
    RoboPciDevice *d;
    uint32_t id, classr, sub, htype, irq;

    if (g_n >= ROBO_PCI_MAX)
        return;
    id = robo_pci_cfg_read32(bus, slot, fn, 0);
    if ((id & 0xFFFFu) == 0xFFFFu)
        return;
    d = &g_dev[g_n++];
    d->bus = bus;
    d->dev = slot;
    d->func = fn;
    d->vendor = (uint16_t)(id & 0xFFFFu);
    d->device = (uint16_t)(id >> 16);
    classr = robo_pci_cfg_read32(bus, slot, fn, 0x08);
    d->revision = (uint8_t)(classr & 0xFFu);
    d->progif = (uint8_t)((classr >> 8) & 0xFFu);
    d->subclass = (uint8_t)((classr >> 16) & 0xFFu);
    d->class_code = (uint8_t)((classr >> 24) & 0xFFu);
    htype = robo_pci_cfg_read32(bus, slot, fn, 0x0C);
    d->hdr = (uint8_t)((htype >> 16) & 0xFFu);
    sub = robo_pci_cfg_read32(bus, slot, fn, 0x2C);
    d->subvendor = (uint16_t)(sub & 0xFFFFu);
    d->subdevice = (uint16_t)(sub >> 16);
    irq = robo_pci_cfg_read32(bus, slot, fn, 0x3C);
    d->irq_line = (uint8_t)(irq & 0xFFu);
    d->irq_pin = (uint8_t)((irq >> 8) & 0xFFu);
    decode_bars(d);
}

static void scan_bus(uint8_t bus);

static void scan_slot(uint8_t bus, uint8_t slot)
{
    uint32_t id = robo_pci_cfg_read32(bus, slot, 0, 0);
    uint8_t hdr, fn, maxfn = 1;
    uint8_t sec;

    if ((id & 0xFFFFu) == 0xFFFFu)
        return;
    hdr = (uint8_t)((robo_pci_cfg_read32(bus, slot, 0, 0x0C) >> 16) & 0xFFu);
    if (hdr & 0x80u)
        maxfn = 8;
    for (fn = 0; fn < maxfn; fn++) {
        id = robo_pci_cfg_read32(bus, slot, fn, 0);
        if ((id & 0xFFFFu) == 0xFFFFu)
            continue;
        add_dev(bus, slot, fn);
        hdr = (uint8_t)((robo_pci_cfg_read32(bus, slot, fn, 0x0C) >> 16) & 0x7Fu);
        if (hdr == 0x01u) {
            sec = (uint8_t)((robo_pci_cfg_read32(bus, slot, fn, 0x18) >> 8) & 0xFFu);
            if (sec && sec != bus)
                scan_bus(sec);
        }
    }
}

static void scan_bus(uint8_t bus)
{
    uint8_t slot;
    for (slot = 0; slot < 32; slot++)
        scan_slot(bus, slot);
}

const char *robo_pci_class_name(uint8_t class_code, uint8_t subclass)
{
    if (class_code == 0x01)
        return subclass == 0x06 ? "SATA Controller" : "Mass Storage";
    if (class_code == 0x02)
        return "Ethernet Controller";
    if (class_code == 0x03)
        return "VGA Compatible Controller";
    if (class_code == 0x04)
        return "Audio Device";
    if (class_code == 0x06)
        return subclass == 0x00 ? "Host Bridge" :
               (subclass == 0x01 ? "ISA Bridge" :
                (subclass == 0x04 ? "PCI Bridge" : "Bridge"));
    if (class_code == 0x0C)
        return subclass == 0x03 ? "USB Controller" : "Serial Bus";
    if (class_code == 0x08)
        return "Generic System Peripheral";
    return "Unknown Device";
}

void robo_pci_enable_io(const RoboPciDevice *d)
{
    uint32_t v;
    if (!d)
        return;
    v = robo_pci_cfg_read32(d->bus, d->dev, d->func, 0x04);
    v |= 0x0001u;
    robo_pci_cfg_write32(d->bus, d->dev, d->func, 0x04, v);
}

void robo_pci_enable_mmio(const RoboPciDevice *d)
{
    uint16_t cmd;
    if (!d)
        return;
    cmd = robo_pci_cfg_read16(d->bus, d->dev, d->func, 0x04);
    cmd = (uint16_t)(cmd | 0x0002u);
    {
        uint32_t v = robo_pci_cfg_read32(d->bus, d->dev, d->func, 0x04);
        v = (v & 0xFFFF0000u) | cmd;
        robo_pci_cfg_write32(d->bus, d->dev, d->func, 0x04, v);
    }
}

void robo_pci_enable_busmaster(const RoboPciDevice *d)
{
    uint32_t v;
    if (!d)
        return;
    v = robo_pci_cfg_read32(d->bus, d->dev, d->func, 0x04);
    v |= 0x0006u;
    robo_pci_cfg_write32(d->bus, d->dev, d->func, 0x04, v);
}

int robo_pci_init(void)
{
    RoboMcfgAlloc a;
    g_n = 0;
    g_use_ecam = 0;
    g_ecam = 0;
    if (robo_acpi_mcfg_at(0, &a) && a.base && a.end_bus >= a.start_bus) {
        uint64_t bytes = ((uint64_t)(a.end_bus - a.start_bus) + 1ull) << 20;
        g_ecam = (uint8_t *)robo_mmio_map((phys_addr_t)a.base, bytes);
        if (g_ecam) {
            g_use_ecam = 1;
            g_ecam_start = a.start_bus;
            g_ecam_end = a.end_bus;
        }
    }
    scan_bus(0);
    return 1;
}

unsigned robo_pci_count(void)
{
    return g_n;
}

RoboPciDevice *robo_pci_at(unsigned i)
{
    return i < g_n ? &g_dev[i] : 0;
}

RoboPciDevice *robo_pci_find(uint16_t vendor, uint16_t device)
{
    unsigned i;
    for (i = 0; i < g_n; i++) {
        if (g_dev[i].vendor == vendor && g_dev[i].device == device)
            return &g_dev[i];
    }
    return 0;
}

void robo_pci_report(void)
{
    unsigned i;
    kprintf("PCI devices\n");
    kprintf("------------------------------------------------\n");
    kprintf("\n");
    for (i = 0; i < g_n; i++) {
        RoboPciDevice *d = &g_dev[i];
        kprintf("%x:%x.%x %s\n",
                (unsigned)d->bus, (unsigned)d->dev, (unsigned)d->func,
                robo_pci_class_name(d->class_code, d->subclass));
        kprintf("Vendor : %x\n", (unsigned)d->vendor);
        kprintf("Device : %x\n", (unsigned)d->device);
        kprintf("Class  : %x\n", (unsigned)d->class_code);
        kprintf("Subcls : %x\n", (unsigned)d->subclass);
        kprintf("\n");
    }
}

int robo_pci_selftest(void)
{
    unsigned i;
    int saw_host = 0, bars_ok = 1, ids_ok = 1;
    if (g_n == 0)
        return 0;
    for (i = 0; i < g_n; i++) {
        RoboPciDevice *d = &g_dev[i];
        if (d->vendor == 0 || d->vendor == 0xFFFFu)
            ids_ok = 0;
        if (d->class_code == 0x06 && d->subclass == 0x00)
            saw_host = 1;
        if ((d->bar[0].flags & ROBO_PCI_BAR_64) && d->bar[0].size == 0)
            bars_ok = 0;
    }
    kprintf("PCI_ENUM  devices %u  host %d  ecam %d\n",
            g_n, saw_host, g_use_ecam);
    return ids_ok && (saw_host || g_n > 0) && bars_ok;
}
