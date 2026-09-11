#include "kio.h"

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
    kprintf("idle hlt\n");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
