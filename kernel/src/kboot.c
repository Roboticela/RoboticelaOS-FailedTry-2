#include "kio.h"

int kboot_debug(const RoboBootInfo *info)
{
    const char *p;
    if (!info) return 0;
    if (info->flags & ROBO_BOOT_DEBUG) return 1;
    p = info->cmdline;
    while (*p) {
        if ((p[0] == 'd' || p[0] == 'D') &&
            (p[1] == 'e' || p[1] == 'E') &&
            (p[2] == 'b' || p[2] == 'B') &&
            (p[3] == 'u' || p[3] == 'U') &&
            (p[4] == 'g' || p[4] == 'G') &&
            (p[5] == 0 || p[5] == ' '))
            return 1;
        p++;
    }
    return 0;
}

static uint64_t sum_type(const RoboBootInfo *info, uint32_t type)
{
    uint64_t n = 0, i;
    const RoboMemoryRegion *map;
    if (!info || !info->memory_map) return 0;
    map = (const RoboMemoryRegion *)(uintptr_t)info->memory_map;
    for (i = 0; i < info->memory_map_count; i++) {
        if (map[i].type == type) n += map[i].length;
    }
    return n;
}

static unsigned count_type(const RoboBootInfo *info, uint32_t type)
{
    unsigned n = 0;
    uint64_t i;
    const RoboMemoryRegion *map;
    if (!info || !info->memory_map) return 0;
    map = (const RoboMemoryRegion *)(uintptr_t)info->memory_map;
    for (i = 0; i < info->memory_map_count; i++)
        if (map[i].type == type) n++;
    return n;
}

static const char *mem_name(uint32_t type)
{
    switch (type) {
    case ROBO_MEM_AVAILABLE: return "avail";
    case ROBO_MEM_RESERVED: return "resv";
    case ROBO_MEM_ACPI: return "acpi";
    case ROBO_MEM_NVS: return "nvs";
    case ROBO_MEM_MMIO: return "mmio";
    case ROBO_MEM_LOADER: return "loader";
    default: return "other";
    }
}

static void kv(const char *k, uint32_t kcol, const char *v, uint32_t vcol)
{
    kfb_puts(k, kcol);
    kfb_puts(v, vcol);
    kfb_puts("\n", vcol);
}

static void term_line(const char *s)
{
    kfb_puts(s, 0xA1A1A6u);
    kfb_puts("\n", 0xA1A1A6u);
}

void kboot_report(const RoboBootInfo *info)
{
    uint64_t avail, reserved;
    unsigned avail_mb, reserved_mb;
    uint32_t white = 0xFFFFFFu;
    uint32_t mute = 0xA1A1A6u;
    int verbose;

    avail = sum_type(info, ROBO_MEM_AVAILABLE) + sum_type(info, ROBO_MEM_LOADER);
    reserved = sum_type(info, ROBO_MEM_RESERVED) + sum_type(info, ROBO_MEM_ACPI) +
               sum_type(info, ROBO_MEM_NVS);
    avail_mb = (unsigned)(avail / (1024ull * 1024ull));
    reserved_mb = (unsigned)(reserved / (1024ull * 1024ull));
    verbose = kboot_debug(info);

    kprintf("Roboticela Kernel\n");
    kprintf("Boot protocol: Roboticela\n");
    kprintf("CPU: x86_64\n");
    kprintf("Memory available: %u MB reserved: %u MB\n", avail_mb, reserved_mb);

    if (verbose) {
        int x = 40, y = 0, w = 0, h = 220;
        if (info && info->debug_term_w && info->debug_term_h) {
            x = (int)info->debug_term_x;
            y = (int)info->debug_term_y;
            w = (int)info->debug_term_w;
            h = (int)info->debug_term_h;
        } else if (info) {
            w = (int)info->framebuffer_width - 80;
            h = (int)info->framebuffer_height * 34 / 100;
            if (h < 160) h = 160;
            y = (int)info->framebuffer_height - 40 - h;
        }
        kfb_term_open(x, y, w, h);
        term_line("kmain");
        kv("Serial  ", mute, "init", white);
        kv("Framebuffer  ", mute, "init", white);
        kfb_puts("Bootinfo  magic ", mute);
        kfb_puthex(info ? info->magic : 0, white);
        kfb_puts("  ver ", mute);
        kfb_putu(info ? info->version : 0, white);
        kfb_puts("  flags ", mute);
        kfb_puthex(info ? info->flags : 0, white);
        kfb_puts("\n", mute);
        kfb_puts("FB  ", mute);
        kfb_puthex(info ? info->framebuffer_address : 0, white);
        kfb_puts("  ", mute);
        kfb_putu(info ? info->framebuffer_width : 0, white);
        kfb_puts(" x ", mute);
        kfb_putu(info ? info->framebuffer_height : 0, white);
        kfb_puts("  pitch ", mute);
        kfb_putu(info ? info->framebuffer_pitch : 0, white);
        kfb_puts("  ", mute);
        kfb_putu(info ? info->framebuffer_bpp : 0, white);
        kfb_puts("-bit  fmt ", mute);
        kfb_putu(info ? info->framebuffer_pixfmt : 0, white);
        kfb_puts("\n", mute);
        kfb_puts("Memory  ", mute);
        kfb_putu(info ? info->memory_map_count : 0, white);
        kfb_puts(" regions\n", mute);
        kfb_puts("  avail ", mute);
        kfb_putu(count_type(info, ROBO_MEM_AVAILABLE), white);
        kfb_puts("  ", mute);
        kfb_putu((unsigned)(sum_type(info, ROBO_MEM_AVAILABLE) / (1024ull * 1024ull)), white);
        kfb_puts(" MB   loader ", mute);
        kfb_putu(count_type(info, ROBO_MEM_LOADER), white);
        kfb_puts("  ", mute);
        kfb_putu((unsigned)(sum_type(info, ROBO_MEM_LOADER) / (1024ull * 1024ull)), white);
        kfb_puts(" MB\n", mute);
        kfb_puts("  resv ", mute);
        kfb_putu(reserved_mb, white);
        kfb_puts(" MB   acpi ", mute);
        kfb_putu(count_type(info, ROBO_MEM_ACPI), white);
        kfb_puts("   nvs ", mute);
        kfb_putu(count_type(info, ROBO_MEM_NVS), white);
        kfb_puts("   mmio ", mute);
        kfb_putu(count_type(info, ROBO_MEM_MMIO), white);
        kfb_puts("\n", mute);
        if (info && info->memory_map && info->memory_map_count) {
            const RoboMemoryRegion *map = (const RoboMemoryRegion *)(uintptr_t)info->memory_map;
            unsigned n = info->memory_map_count < 8 ? (unsigned)info->memory_map_count : 8;
            unsigned ri;
            for (ri = 0; ri < n; ri++) {
                kfb_puts("  ", mute);
                kfb_puts(mem_name(map[ri].type), mute);
                kfb_puts("  ", mute);
                kfb_puthex(map[ri].address, white);
                kfb_puts("  ", mute);
                kfb_putu(map[ri].length, white);
                kfb_puts("\n", mute);
            }
        }
        kfb_puts("ACPI  RSDP ", mute);
        if (info && info->rsdp_address)
            kfb_puthex(info->rsdp_address, white);
        else
            kfb_puts("missing", white);
        kfb_puts("\n", mute);
        kfb_puts("Kernel  ", mute);
        kfb_puthex(info ? info->kernel_base : 0, white);
        kfb_puts("  ", mute);
        kfb_putu(info ? info->kernel_size : 0, white);
        kfb_puts(" bytes\n", mute);
        kfb_puts("InitFS  ", mute);
        kfb_puthex(info ? info->initrd_address : 0, white);
        kfb_puts("  ", mute);
        kfb_putu(info ? info->initrd_size : 0, white);
        kfb_puts(" bytes\n", mute);
        kfb_puts("Bootinfo  ", mute);
        kfb_puthex(info ? info->bootinfo_address : 0, white);
        kfb_puts("  ", mute);
        kfb_putu(info ? info->bootinfo_size : 0, white);
        kfb_puts(" bytes\n", mute);
        if (info && info->cmdline[0]) {
            kfb_puts("Cmdline  ", mute);
            kfb_puts(info->cmdline, white);
            kfb_puts("\n", mute);
        }
        kv("CPU  ", mute, "x86_64", white);
        kv("Idle  ", mute, "hlt", white);
        return;
    }

    kfb_clear(0x000000);
    kfb_puts("Roboticela Kernel\n", white);
    kfb_puts("-----------------\n\n", mute);
    kfb_puts("Boot protocol: Roboticela\n", white);
    kfb_puts("CPU: x86_64\n\n", white);
    kfb_puts("Memory:\n", mute);
    kfb_puts("  Available: ", white);
    kfb_putu(avail_mb, white);
    kfb_puts(" MB\n  Reserved:  ", white);
    kfb_putu(reserved_mb, white);
    kfb_puts(" MB\n\n", white);
    kfb_puts("Framebuffer:\n", mute);
    kfb_puts("  ", white);
    kfb_putu(info ? info->framebuffer_width : 0, white);
    kfb_puts("x", white);
    kfb_putu(info ? info->framebuffer_height : 0, white);
    kfb_puts("\n  ", white);
    kfb_putu(info ? info->framebuffer_bpp : 0, white);
    kfb_puts(" BPP\n\n", white);
    kfb_puts("ACPI:\n", mute);
    kfb_puts(info && info->rsdp_address ? "  RSDP found\n\n" : "  RSDP missing\n\n", white);
    kfb_puts("Kernel:\n", mute);
    kfb_puts("  address: ", white);
    kfb_puthex(info ? info->kernel_base : 0, white);
    kfb_puts("\n  size: ", white);
    kfb_putu(info ? info->kernel_size : 0, white);
    kfb_puts("\n\nInitFS:\n", mute);
    kfb_puts("  address: ", white);
    kfb_puthex(info ? info->initrd_address : 0, white);
    kfb_puts("\n  size: ", white);
    kfb_putu(info ? info->initrd_size : 0, white);
    kfb_puts("\n", white);
}
