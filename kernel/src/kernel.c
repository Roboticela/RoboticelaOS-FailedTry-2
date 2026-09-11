#include "kio.h"

void kmain(const RoboBootInfo *info)
{
    kserial_init();
    kfb_init(info);
    kboot_report(info);
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
