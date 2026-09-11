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

static void term_line(const char *s)
{
    uint32_t ink = 0xA1A1A6u;
    kfb_puts(s, ink);
    kfb_puts("\n", ink);
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
        term_line("Kernel entered");
        term_line("Serial initialized");
        kfb_puts("Framebuffer  ", mute);
        kfb_putu(info ? info->framebuffer_width : 0, white);
        kfb_puts("x", white);
        kfb_putu(info ? info->framebuffer_height : 0, white);
        kfb_puts("  ", mute);
        kfb_putu(info ? info->framebuffer_bpp : 0, white);
        kfb_puts(" BPP\n", mute);
        kfb_puts("Memory initialized  ", mute);
        kfb_putu(info ? info->memory_map_count : 0, white);
        kfb_puts(" regions\n", mute);
        kfb_puts("Available  ", mute);
        kfb_putu(avail_mb, white);
        kfb_puts(" MB   Reserved  ", mute);
        kfb_putu(reserved_mb, white);
        kfb_puts(" MB\n", mute);
        kfb_puts("ACPI  ", mute);
        kfb_puts((info && info->rsdp_address) ? "RSDP found\n" : "RSDP missing\n", white);
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
        if (info && info->cmdline[0]) {
            kfb_puts("Cmdline  ", mute);
            kfb_puts(info->cmdline, white);
            kfb_puts("\n", mute);
        }
        term_line("CPU x86_64 idle");
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
