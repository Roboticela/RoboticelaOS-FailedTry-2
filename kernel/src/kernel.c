#include "kio.h"
#include "mm/pmm.h"

void kmain(const RoboBootInfo *info)
{
    kprintf("kmain\n");
    kserial_init();
    kprintf("serial init\n");
    kfb_init(info);
    kprintf("framebuffer init %u x %u\n",
            info ? info->framebuffer_width : 0,
            info ? info->framebuffer_height : 0);
    kboot_report(info);

    kprintf("PMM  bootinfo %s  ver %u\n",
            info ? "present" : "missing",
            info ? (unsigned)info->version : 0);
    if (info) {
        kprintf("PMM  map 0x%llx  %llu regions\n",
                (unsigned long long)info->memory_map,
                (unsigned long long)info->memory_map_count);
        kprintf("PMM  kernel 0x%llx +%llu  bootinfo 0x%llx +%llu\n",
                (unsigned long long)info->kernel_base,
                (unsigned long long)info->kernel_size,
                (unsigned long long)info->bootinfo_address,
                (unsigned long long)info->bootinfo_size);
    }

    if (!robo_pmm_init(info)) {
        kprintf("PMM_TEST_FAIL\n");
    } else {
        robo_pmm_report();
        if (kboot_debug(info))
            robo_pmm_fb_report();
        if (robo_pmm_selftest())
            kprintf("PMM_TEST_OK\n");
        else
            kprintf("PMM_TEST_FAIL\n");
    }

    kprintf("idle hlt\n");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
