#include "pci/pci.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/acpi/acpi.h"
#include "arch/x86_64/mmio.h"
#include "kio.h"

static RoboPciDevice gDev[RoboPciMax];
static unsigned gN;
static uint8_t *gEcam;
static uint8_t gEcamStart, gEcamEnd;
static int gUseEcam;

static uint32_t PioRead(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
{
    uint32_t addr = 0x80000000u |
                    ((uint32_t)bus << 16) |
                    ((uint32_t)(dev & 31u) << 11) |
                    ((uint32_t)(func & 7u) << 8) |
                    (uint32_t)(off & 0xFCu);
    Outl(0xCF8, addr);
    return Inl(0xCFC);
}

static void PioWrite(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off,
                      uint32_t v)
{
    uint32_t addr = 0x80000000u |
                    ((uint32_t)bus << 16) |
                    ((uint32_t)(dev & 31u) << 11) |
                    ((uint32_t)(func & 7u) << 8) |
                    (uint32_t)(off & 0xFCu);
    Outl(0xCF8, addr);
    Outl(0xCFC, v);
}

uint32_t PciCfgRead32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
{
    if (gUseEcam && gEcam && bus >= gEcamStart && bus <= gEcamEnd) {
        uint32_t idx = ((uint32_t)(bus - gEcamStart) << 20) |
                       ((uint32_t)(dev & 31u) << 15) |
                       ((uint32_t)(func & 7u) << 12) |
                       (uint32_t)(off & 0xFFCu);
        return *(volatile uint32_t *)(gEcam + idx);
    }
    return PioRead(bus, dev, func, off);
}

void PciCfgWrite32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off,
                          uint32_t v)
{
    if (gUseEcam && gEcam && bus >= gEcamStart && bus <= gEcamEnd) {
        uint32_t idx = ((uint32_t)(bus - gEcamStart) << 20) |
                       ((uint32_t)(dev & 31u) << 15) |
                       ((uint32_t)(func & 7u) << 12) |
                       (uint32_t)(off & 0xFFCu);
        *(volatile uint32_t *)(gEcam + idx) = v;
        return;
    }
    PioWrite(bus, dev, func, off, v);
}

uint16_t PciCfgRead16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
{
    uint32_t v = PciCfgRead32(bus, dev, func, (uint8_t)(off & 0xFCu));
    return (uint16_t)(v >> (8u * (off & 2u)));
}

uint8_t PciCfgRead8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
{
    uint32_t v = PciCfgRead32(bus, dev, func, (uint8_t)(off & 0xFCu));
    return (uint8_t)(v >> (8u * (off & 3u)));
}

static uint32_t BarRaw(uint8_t bus, uint8_t slot, uint8_t fn, unsigned bar)
{
    return PciCfgRead32(bus, slot, fn, (uint8_t)(0x10u + bar * 4u));
}

static void DecodeBars(RoboPciDevice *d)
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
        uint32_t orig = PciCfgRead32(d->bus, d->dev, d->func, off);
        uint32_t mask;
        uint64_t size;

        if (!orig)
            continue;
        PciCfgWrite32(d->bus, d->dev, d->func, off, 0xFFFFFFFFu);
        mask = PciCfgRead32(d->bus, d->dev, d->func, off);
        PciCfgWrite32(d->bus, d->dev, d->func, off, orig);
        if (orig & 1u) {
            d->bar[i].flags = RoboPciBarIo;
            d->bar[i].addr = orig & 0xFFFFFFFCu;
            size = (uint64_t)((~(mask & 0xFFFFFFFCu)) + 1u);
            d->bar[i].size = size & 0xFFFFull;
            continue;
        }
        d->bar[i].flags = RoboPciBarMem;
        if (orig & 8u)
            d->bar[i].flags |= RoboPciBarPre;
        if (((orig >> 1) & 3u) == 2u && i + 1 < nbar) {
            uint32_t origHi, maskHi;
            uint8_t offHi = (uint8_t)(off + 4u);
            origHi = PciCfgRead32(d->bus, d->dev, d->func, offHi);
            PciCfgWrite32(d->bus, d->dev, d->func, offHi, 0xFFFFFFFFu);
            maskHi = PciCfgRead32(d->bus, d->dev, d->func, offHi);
            PciCfgWrite32(d->bus, d->dev, d->func, offHi, origHi);
            d->bar[i].flags |= RoboPciBar64;
            d->bar[i].addr = (uint64_t)(orig & 0xFFFFFFF0u) |
                             ((uint64_t)origHi << 32);
            size = ~(((uint64_t)maskHi << 32) | (uint64_t)(mask & 0xFFFFFFF0u)) + 1ull;
            d->bar[i].size = size;
            i++;
            continue;
        }
        d->bar[i].addr = orig & 0xFFFFFFF0u;
        d->bar[i].size = (uint64_t)((~(mask & 0xFFFFFFF0u)) + 1u);
        (void)BarRaw;
    }
}

static void AddDev(uint8_t bus, uint8_t slot, uint8_t fn)
{
    RoboPciDevice *d;
    uint32_t id, classr, sub, htype, irq;

    if (gN >= RoboPciMax)
        return;
    id = PciCfgRead32(bus, slot, fn, 0);
    if ((id & 0xFFFFu) == 0xFFFFu)
        return;
    d = &gDev[gN++];
    d->bus = bus;
    d->dev = slot;
    d->func = fn;
    d->vendor = (uint16_t)(id & 0xFFFFu);
    d->device = (uint16_t)(id >> 16);
    classr = PciCfgRead32(bus, slot, fn, 0x08);
    d->revision = (uint8_t)(classr & 0xFFu);
    d->progif = (uint8_t)((classr >> 8) & 0xFFu);
    d->subclass = (uint8_t)((classr >> 16) & 0xFFu);
    d->classCode = (uint8_t)((classr >> 24) & 0xFFu);
    htype = PciCfgRead32(bus, slot, fn, 0x0C);
    d->hdr = (uint8_t)((htype >> 16) & 0xFFu);
    sub = PciCfgRead32(bus, slot, fn, 0x2C);
    d->subvendor = (uint16_t)(sub & 0xFFFFu);
    d->subdevice = (uint16_t)(sub >> 16);
    irq = PciCfgRead32(bus, slot, fn, 0x3C);
    d->irqLine = (uint8_t)(irq & 0xFFu);
    d->irqPin = (uint8_t)((irq >> 8) & 0xFFu);
    DecodeBars(d);
}

static void ScanBus(uint8_t bus);

static void ScanSlot(uint8_t bus, uint8_t slot)
{
    uint32_t id = PciCfgRead32(bus, slot, 0, 0);
    uint8_t hdr, fn, maxfn = 1;
    uint8_t sec;

    if ((id & 0xFFFFu) == 0xFFFFu)
        return;
    hdr = (uint8_t)((PciCfgRead32(bus, slot, 0, 0x0C) >> 16) & 0xFFu);
    if (hdr & 0x80u)
        maxfn = 8;
    for (fn = 0; fn < maxfn; fn++) {
        id = PciCfgRead32(bus, slot, fn, 0);
        if ((id & 0xFFFFu) == 0xFFFFu)
            continue;
        AddDev(bus, slot, fn);
        hdr = (uint8_t)((PciCfgRead32(bus, slot, fn, 0x0C) >> 16) & 0x7Fu);
        if (hdr == 0x01u) {
            sec = (uint8_t)((PciCfgRead32(bus, slot, fn, 0x18) >> 8) & 0xFFu);
            if (sec && sec != bus)
                ScanBus(sec);
        }
    }
}

static void ScanBus(uint8_t bus)
{
    uint8_t slot;
    for (slot = 0; slot < 32; slot++)
        ScanSlot(bus, slot);
}

const char *PciClassName(uint8_t classCode, uint8_t subclass)
{
    if (classCode == 0x01)
        return subclass == 0x06 ? "SATA Controller" : "Mass Storage";
    if (classCode == 0x02)
        return "Ethernet Controller";
    if (classCode == 0x03)
        return "VGA Compatible Controller";
    if (classCode == 0x04)
        return "Audio Device";
    if (classCode == 0x06)
        return subclass == 0x00 ? "Host Bridge" :
               (subclass == 0x01 ? "ISA Bridge" :
                (subclass == 0x04 ? "PCI Bridge" : "Bridge"));
    if (classCode == 0x0C)
        return subclass == 0x03 ? "USB Controller" : "Serial Bus";
    if (classCode == 0x08)
        return "Generic System Peripheral";
    return "Unknown Device";
}

void PciEnableIo(const RoboPciDevice *d)
{
    uint32_t v;
    if (!d)
        return;
    v = PciCfgRead32(d->bus, d->dev, d->func, 0x04);
    v |= 0x0001u;
    PciCfgWrite32(d->bus, d->dev, d->func, 0x04, v);
}

void PciEnableMmio(const RoboPciDevice *d)
{
    uint16_t cmd;
    if (!d)
        return;
    cmd = PciCfgRead16(d->bus, d->dev, d->func, 0x04);
    cmd = (uint16_t)(cmd | 0x0002u);
    {
        uint32_t v = PciCfgRead32(d->bus, d->dev, d->func, 0x04);
        v = (v & 0xFFFF0000u) | cmd;
        PciCfgWrite32(d->bus, d->dev, d->func, 0x04, v);
    }
}

void PciEnableBusmaster(const RoboPciDevice *d)
{
    uint32_t v;
    if (!d)
        return;
    v = PciCfgRead32(d->bus, d->dev, d->func, 0x04);
    v |= 0x0006u;
    PciCfgWrite32(d->bus, d->dev, d->func, 0x04, v);
}

int PciInit(void)
{
    RoboMcfgAlloc a;
    gN = 0;
    gUseEcam = 0;
    gEcam = 0;
    if (AcpiMcfgAt(0, &a) && a.base && a.endBus >= a.startBus) {
        uint64_t bytes = ((uint64_t)(a.endBus - a.startBus) + 1ull) << 20;
        gEcam = (uint8_t *)MmioMap((PhysAddr)a.base, bytes);
        if (gEcam) {
            gUseEcam = 1;
            gEcamStart = a.startBus;
            gEcamEnd = a.endBus;
        }
    }
    ScanBus(0);
    return 1;
}

unsigned PciCount(void)
{
    return gN;
}

RoboPciDevice *PciAt(unsigned i)
{
    return i < gN ? &gDev[i] : 0;
}

RoboPciDevice *PciFind(uint16_t vendor, uint16_t device)
{
    unsigned i;
    for (i = 0; i < gN; i++) {
        if (gDev[i].vendor == vendor && gDev[i].device == device)
            return &gDev[i];
    }
    return 0;
}

void PciReport(void)
{
    unsigned i;
    kprintf("PCI devices\n");
    kprintf("------------------------------------------------\n");
    kprintf("\n");
    for (i = 0; i < gN; i++) {
        RoboPciDevice *d = &gDev[i];
        kprintf("%x:%x.%x %s\n",
                (unsigned)d->bus, (unsigned)d->dev, (unsigned)d->func,
                PciClassName(d->classCode, d->subclass));
        kprintf("Vendor : %x\n", (unsigned)d->vendor);
        kprintf("Device : %x\n", (unsigned)d->device);
        kprintf("Class  : %x\n", (unsigned)d->classCode);
        kprintf("Subcls : %x\n", (unsigned)d->subclass);
        kprintf("\n");
    }
}

int PciSelftest(void)
{
    unsigned i;
    int sawHost = 0, barsOk = 1, idsOk = 1;
    if (gN == 0)
        return 0;
    for (i = 0; i < gN; i++) {
        RoboPciDevice *d = &gDev[i];
        if (d->vendor == 0 || d->vendor == 0xFFFFu)
            idsOk = 0;
        if (d->classCode == 0x06 && d->subclass == 0x00)
            sawHost = 1;
        if ((d->bar[0].flags & RoboPciBar64) && d->bar[0].size == 0)
            barsOk = 0;
    }
    kprintf("PCI_ENUM  devices %u  host %d  ecam %d\n",
            gN, sawHost, gUseEcam);
    return idsOk && (sawHost || gN > 0) && barsOk;
}
