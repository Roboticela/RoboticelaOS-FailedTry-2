#include "kio.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/exception.h"
#include "mm/pmm.h"
#include "mm/vmm/vmm.h"
#include "mm/heap/heap.h"
#include "time/time.h"
#include "scheduler/scheduler.h"

void kmain(const RoboBootInfo *info)
{
    kserial_init();
    kfb_init(info);
    kprintf("kmain\n");
    kprintf("serial init\n");
    kprintf("framebuffer init %u x %u\n",
            info ? info->framebuffer_width : 0,
            info ? info->framebuffer_height : 0);
    kboot_report(info);

    kprintf("CPU  init\n");
    if (!robo_cpu_init()) {
        kprintf("CPU_TEST_FAIL\n");
        kprintf("PMM_TEST_FAIL\n");
        kprintf("VMM_TEST_FAIL\n");
        kprintf("HEAP_TEST_FAIL\n");
        kprintf("idle\n");
        for (;;) {
            kfb_poll();
            __asm__ volatile ("pause");
        }
    }
    robo_cpu_report();
    if (kboot_debug(info))
        robo_cpu_fb_report();
    if (robo_cpu_selftest())
        kprintf("CPU_TEST_OK\n");
    else
        kprintf("CPU_TEST_FAIL\n");

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
        kprintf("VMM_TEST_FAIL\n");
        kprintf("HEAP_TEST_FAIL\n");
    } else {
        robo_pmm_report();
        if (kboot_debug(info))
            robo_pmm_fb_report();
        if (robo_pmm_selftest())
            kprintf("PMM_TEST_OK\n");
        else
            kprintf("PMM_TEST_FAIL\n");

        if (!robo_vmm_init(info)) {
            kprintf("VMM_TEST_FAIL\n");
            kprintf("HEAP_TEST_FAIL\n");
        } else {
            robo_vmm_report();
            if (kboot_debug(info))
                robo_vmm_fb_report();
            if (robo_vmm_selftest())
                kprintf("VMM_TEST_OK\n");
            else
                kprintf("VMM_TEST_FAIL\n");

            if (!robo_heap_init()) {
                kprintf("HEAP_TEST_FAIL\n");
            } else {
                robo_heap_report();
                if (kboot_debug(info))
                    robo_heap_fb_report();
                if (robo_heap_selftest())
                    kprintf("HEAP_TEST_OK\n");
                else
                    kprintf("HEAP_TEST_FAIL\n");
            }
        }
    }

    if (robo_vmm_ready()) {
        if (robo_pf_selftest())
            kprintf("PF_TEST_OK\n");
        else
            kprintf("PF_TEST_FAIL\n");
    } else {
        kprintf("PF_TEST_FAIL\n");
    }

    if (!robo_timer_init()) {
        kprintf("TIMER_TEST_FAIL\n");
    } else {
        robo_timer_report();
        if (kboot_debug(info))
            robo_timer_fb_report();
        if (robo_timer_selftest())
            kprintf("TIMER_TEST_OK\n");
        else
            kprintf("TIMER_TEST_FAIL\n");
    }

    if (robo_timer_ready() && robo_heap_ready()) {
        if (!robo_scheduler_init()) {
            kprintf("SCHED_TEST_FAIL\n");
        } else {
            robo_scheduler_report();
            if (kboot_debug(info))
                robo_scheduler_fb_report();
            if (robo_scheduler_selftest())
                kprintf("SCHED_TEST_OK\n");
            else
                kprintf("SCHED_TEST_FAIL\n");
        }
    } else {
        kprintf("SCHED_TEST_FAIL\n");
    }

    kprintf("idle\n");
    for (;;) {
        robo_sched_reap();
        kfb_poll();
        __asm__ volatile ("hlt");
    }
}
