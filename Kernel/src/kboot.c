#include "kio.h"

int KbootCmdHas(const RoboBootInfo *info, const char *key)
{
    const char *p;
    unsigned i;

    if (!info || !key || !key[0])
        return 0;
    p = info->cmdline;
    while (*p) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        for (i = 0; key[i] && p[i] && p[i] == key[i]; i++) { }
        if (!key[i] && (p[i] == 0 || p[i] == ' ' || p[i] == '='))
            return 1;
        while (*p && *p != ' ')
            p++;
    }
    return 0;
}

int KbootDebug(const RoboBootInfo *info)
{
    if (!info)
        return 0;
    if (info->flags & RoboBootDebug)
        return 1;
    return KbootCmdHas(info, "debug");
}

int KbootSelftest(const RoboBootInfo *info)
{
    return KbootCmdHas(info, "selftest");
}

static uint64_t SumType(const RoboBootInfo *info, uint32_t type)
{
    uint64_t n = 0, i;
    const RoboMemoryRegion *map;
    if (!info || !info->memoryMap) return 0;
    map = (const RoboMemoryRegion *)(uintptr_t)info->memoryMap;
    for (i = 0; i < info->memoryMapCount; i++) {
        if (map[i].type == type) n += map[i].length;
    }
    return n;
}

static unsigned CountType(const RoboBootInfo *info, uint32_t type)
{
    unsigned n = 0;
    uint64_t i;
    const RoboMemoryRegion *map;
    if (!info || !info->memoryMap) return 0;
    map = (const RoboMemoryRegion *)(uintptr_t)info->memoryMap;
    for (i = 0; i < info->memoryMapCount; i++)
        if (map[i].type == type) n++;
    return n;
}

static const char *MemName(uint32_t type)
{
    switch (type) {
    case RoboMemAvailable: return "avail";
    case RoboMemReserved: return "resv";
    case RoboMemAcpi: return "acpi";
    case RoboMemNvs: return "nvs";
    case RoboMemMmio: return "mmio";
    case RoboMemLoader: return "loader";
    default: return "other";
    }
}

static void kv(const char *k, uint32_t kcol, const char *v, uint32_t vcol)
{
    KfbPuts(k, kcol);
    KfbPuts(v, vcol);
    KfbPuts("\n", vcol);
}

static void TermLine(const char *s)
{
    KfbPuts(s, 0xA1A1A6u);
    KfbPuts("\n", 0xA1A1A6u);
}

void KbootReport(const RoboBootInfo *info)
{
    uint64_t avail, reserved;
    unsigned availMb, reservedMb;
    uint32_t white = 0xFFFFFFu;
    uint32_t mute = 0xA1A1A6u;
    int verbose;

    avail = SumType(info, RoboMemAvailable) + SumType(info, RoboMemLoader);
    reserved = SumType(info, RoboMemReserved) + SumType(info, RoboMemAcpi) +
               SumType(info, RoboMemNvs);
    availMb = (unsigned)(avail / (1024ull * 1024ull));
    reservedMb = (unsigned)(reserved / (1024ull * 1024ull));
    verbose = KbootDebug(info);

    kprintf("Roboticela Kernel\n");
    kprintf("Boot protocol: Roboticela\n");
    kprintf("CPU: x86_64\n");
    kprintf("Memory available: %u MB reserved: %u MB\n", availMb, reservedMb);

    if (verbose) {
        int x = 40, y = 0, w = 0, h = 220;
        if (!KfbTermActive()) {
            if (info && info->debugTermW && info->debugTermH) {
                x = (int)info->debugTermX;
                y = (int)info->debugTermY;
                w = (int)info->debugTermW;
                h = (int)info->debugTermH;
            } else if (info) {
                w = (int)info->framebufferWidth - 80;
                h = (int)info->framebufferHeight * 34 / 100;
                if (h < 160) h = 160;
                y = (int)info->framebufferHeight - 40 - h;
            }
            KfbTermOpen(x, y, w, h);
        }
        if (!info || info->debugLogCount < 1) {
            TermLine("kmain");
            TermLine("Logs  Up/Down to scroll");
        }
        kv("Serial  ", mute, "init", white);
        kv("Framebuffer  ", mute, "init", white);
        KfbPuts("Bootinfo  magic ", mute);
        KfbPuthex(info ? info->magic : 0, white);
        KfbPuts("  ver ", mute);
        KfbPutu(info ? info->version : 0, white);
        KfbPuts("  flags ", mute);
        KfbPuthex(info ? info->flags : 0, white);
        KfbPuts("\n", mute);
        KfbPuts("FB  ", mute);
        KfbPuthex(info ? info->framebufferAddress : 0, white);
        KfbPuts("  ", mute);
        KfbPutu(info ? info->framebufferWidth : 0, white);
        KfbPuts(" x ", mute);
        KfbPutu(info ? info->framebufferHeight : 0, white);
        KfbPuts("  pitch ", mute);
        KfbPutu(info ? info->framebufferPitch : 0, white);
        KfbPuts("  ", mute);
        KfbPutu(info ? info->framebufferBpp : 0, white);
        KfbPuts("-bit  fmt ", mute);
        KfbPutu(info ? info->framebufferPixfmt : 0, white);
        KfbPuts("\n", mute);
        KfbPuts("Memory  ", mute);
        KfbPutu(info ? info->memoryMapCount : 0, white);
        KfbPuts(" regions\n", mute);
        KfbPuts("  avail ", mute);
        KfbPutu(CountType(info, RoboMemAvailable), white);
        KfbPuts("  ", mute);
        KfbPutu((unsigned)(SumType(info, RoboMemAvailable) / (1024ull * 1024ull)), white);
        KfbPuts(" MB   loader ", mute);
        KfbPutu(CountType(info, RoboMemLoader), white);
        KfbPuts("  ", mute);
        KfbPutu((unsigned)(SumType(info, RoboMemLoader) / (1024ull * 1024ull)), white);
        KfbPuts(" MB\n", mute);
        KfbPuts("  resv ", mute);
        KfbPutu(reservedMb, white);
        KfbPuts(" MB   acpi ", mute);
        KfbPutu(CountType(info, RoboMemAcpi), white);
        KfbPuts("   nvs ", mute);
        KfbPutu(CountType(info, RoboMemNvs), white);
        KfbPuts("   mmio ", mute);
        KfbPutu(CountType(info, RoboMemMmio), white);
        KfbPuts("\n", mute);
        if (info && info->memoryMap && info->memoryMapCount) {
            const RoboMemoryRegion *map = (const RoboMemoryRegion *)(uintptr_t)info->memoryMap;
            unsigned n = info->memoryMapCount < 8 ? (unsigned)info->memoryMapCount : 8;
            unsigned ri;
            for (ri = 0; ri < n; ri++) {
                KfbPuts("  ", mute);
                KfbPuts(MemName(map[ri].type), mute);
                KfbPuts("  ", mute);
                KfbPuthex(map[ri].address, white);
                KfbPuts("  ", mute);
                KfbPutu(map[ri].length, white);
                KfbPuts("\n", mute);
            }
        }
        KfbPuts("ACPI  RSDP ", mute);
        if (info && info->rsdpAddress)
            KfbPuthex(info->rsdpAddress, white);
        else
            KfbPuts("missing", white);
        KfbPuts("\n", mute);
        KfbPuts("Kernel  ", mute);
        KfbPuthex(info ? info->kernelBase : 0, white);
        KfbPuts("  ", mute);
        KfbPutu(info ? info->kernelSize : 0, white);
        KfbPuts(" bytes\n", mute);
        KfbPuts("InitFS  ", mute);
        KfbPuthex(info ? info->initrdAddress : 0, white);
        KfbPuts("  ", mute);
        KfbPutu(info ? info->initrdSize : 0, white);
        KfbPuts(" bytes\n", mute);
        KfbPuts("Bootinfo  ", mute);
        KfbPuthex(info ? info->bootinfoAddress : 0, white);
        KfbPuts("  ", mute);
        KfbPutu(info ? info->bootinfoSize : 0, white);
        KfbPuts(" bytes\n", mute);
        if (info && info->cmdline[0]) {
            KfbPuts("Cmdline  ", mute);
            KfbPuts(info->cmdline, white);
            KfbPuts("\n", mute);
        }
        kv("CPU  ", mute, "x86_64", white);
        kv("Idle  ", mute, "hlt", white);
        return;
    }
}
