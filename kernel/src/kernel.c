#include "kio.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/exception.h"
#include "mm/pmm.h"
#include "mm/vmm/vmm.h"
#include "mm/heap/heap.h"
#include "time/time.h"
#include "scheduler/scheduler.h"
#include "thread/thread.h"
#include "process/process.h"
#include "arch/x86_64/context/context_switch.h"
#include "fs/robofs.h"
#include "fs/fd.h"
#include "fs/vfs/vfs.h"
#include "fs/rfs/rfs.h"
#include "fs/rfs/rfs_disk.h"
#include "fs/block/block.h"
#include "exec/exec.h"
#include "syscall/syscall.h"
#include "process/init.h"
#include "drivers/manager.h"

static void boot_bar(unsigned p)
{
    kfb_splash_set(p);
    kprintf("Loader  %u percent\n", (unsigned)(p / 100u));
}

static int run_user_prog(const char *path)
{
    RoboProcess *p;
    unsigned i;
    int ok;

    p = robo_process_create_from_elf(path);
    if (!p)
        return 0;
    for (i = 0; i < 200000u; i++) {
        if (p->state == ROBO_PROCESS_TERMINATED)
            break;
        robo_thread_yield();
        robo_sched_reap();
    }
    ok = p->state == ROBO_PROCESS_TERMINATED && p->exit_status == 0;
    robo_sched_reap();
    robo_process_destroy(p);
    return ok;
}

void kmain(const RoboBootInfo *info)
{
    int handoff = 0;
    kserial_init();
    kfb_init(info);
    kprintf("kmain\n");
    kprintf("serial init\n");
    kprintf("framebuffer init %u x %u\n",
            info ? info->framebuffer_width : 0,
            info ? info->framebuffer_height : 0);
    boot_bar(ROBO_BOOT_PROGRESS_HANDOFF);
    kboot_report(info);
    boot_bar(4200);

    kprintf("CPU  init\n");
    if (!robo_cpu_init()) {
        kprintf("CPU_TEST_FAIL\n");
        kprintf("PMM_TEST_FAIL\n");
        kprintf("VMM_TEST_FAIL\n");
        kprintf("HEAP_TEST_FAIL\n");
        kprintf("idle\n");
        boot_bar(ROBO_BOOT_PROGRESS_MAX);
        kprintf("Desktop  ready\n");
        for (;;) {
            kfb_poll();
            __asm__ volatile ("pause");
        }
    }
    robo_cpu_report();
    if (kboot_debug(info))
        robo_cpu_fb_report();
    boot_bar(4600);
    if (robo_cpu_selftest())
        kprintf("CPU_TEST_OK\n");
    else
        kprintf("CPU_TEST_FAIL\n");
    boot_bar(5000);

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
        boot_bar(5400);
        if (robo_pmm_selftest())
            kprintf("PMM_TEST_OK\n");
        else
            kprintf("PMM_TEST_FAIL\n");
        boot_bar(5800);

        if (!robo_vmm_init(info)) {
            kprintf("VMM_TEST_FAIL\n");
            kprintf("HEAP_TEST_FAIL\n");
        } else {
            robo_vmm_report();
            if (kboot_debug(info))
                robo_vmm_fb_report();
            boot_bar(6200);
            if (robo_vmm_selftest())
                kprintf("VMM_TEST_OK\n");
            else
                kprintf("VMM_TEST_FAIL\n");
            boot_bar(6600);

            if (!robo_heap_init()) {
                kprintf("HEAP_TEST_FAIL\n");
            } else {
                robo_heap_report();
                if (kboot_debug(info))
                    robo_heap_fb_report();
                boot_bar(7000);
                if (robo_heap_selftest())
                    kprintf("HEAP_TEST_OK\n");
                else
                    kprintf("HEAP_TEST_FAIL\n");
                boot_bar(7400);
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
    boot_bar(7800);

    if (!robo_timer_init()) {
        kprintf("TIMER_TEST_FAIL\n");
    } else {
        robo_timer_report();
        if (kboot_debug(info))
            robo_timer_fb_report();
        boot_bar(8200);
        if (robo_timer_selftest())
            kprintf("TIMER_TEST_OK\n");
        else
            kprintf("TIMER_TEST_FAIL\n");
        boot_bar(8600);
    }

    if (robo_timer_ready() && robo_heap_ready()) {
        if (!robo_scheduler_init()) {
            kprintf("CTX_TEST_FAIL\n");
            kprintf("SCHED_TEST_FAIL\n");
            kprintf("THREAD_TEST_FAIL\n");
            kprintf("PROC_TEST_FAIL\n");
        } else {
            robo_scheduler_report();
            if (kboot_debug(info))
                robo_scheduler_fb_report();
            robo_scheduler_start();
            robo_x86_context_report();
            if (kboot_debug(info))
                robo_x86_context_fb_report();
            boot_bar(9000);
            if (robo_context_selftest())
                kprintf("CTX_TEST_OK\n");
            else
                kprintf("CTX_TEST_FAIL\n");
            if (kboot_debug(info))
                robo_x86_context_fb_report();
            boot_bar(9200);
            if (robo_scheduler_selftest())
                kprintf("SCHED_TEST_OK\n");
            else
                kprintf("SCHED_TEST_FAIL\n");
            robo_thread_report();
            if (kboot_debug(info))
                robo_thread_fb_report();
            boot_bar(9400);
            if (robo_thread_selftest())
                kprintf("THREAD_TEST_OK\n");
            else
                kprintf("THREAD_TEST_FAIL\n");
            robo_process_report();
            if (kboot_debug(info))
                robo_process_fb_report();
            boot_bar(9600);
            if (robo_process_selftest())
                kprintf("PROC_TEST_OK\n");
            else
                kprintf("PROC_TEST_FAIL\n");
            boot_bar(9850);
            if (robo_fs_init(info)) {
                static RoboBlockDevice *root_disk;
                int imported;

                robo_fs_report();
                robo_vfs_init();
                robofs_register();
                if (!robo_vfs_selftest())
                    kprintf("VFS_TEST_FAIL\n");
                root_disk = robo_memdev_create(ROBOFS_BLOCK_SIZE, 512);
                imported = -1;
                if (root_disk && robofs_format(root_disk) == 0 &&
                    robo_vfs_mount_root("robofs", root_disk, 0) == 0)
                    imported = robo_vfs_import_initrd();
                else
                    kprintf("ROBOFS_ROOT_FAIL\n");
                if (imported != 0)
                    kprintf("VFS_IMPORT_FAIL  %d\n", imported);
                if (kboot_debug(info)) {
                    kfb_puts("RoboFS ready\n", 0xA1A1A6u);
                    robo_vfs_fb_report();
                    robofs_fb_report();
                }
                if (robo_exec_selftest(info))
                    kprintf("USER_TEST_OK\n");
                else
                    kprintf("USER_TEST_FAIL\n");
                if (kboot_debug(info))
                    robo_syscall_fb_report();
                if (robo_syscall_selftest(info))
                    kprintf("SYSCALL_TEST_OK\n");
                else
                    kprintf("SYSCALL_TEST_FAIL\n");
                if (kboot_debug(info)) {
                    robo_fd_table_dump(robo_current_process());
                    robo_fd_fb_report();
                }
                if (robo_fd_selftest())
                    kprintf("FD_TEST_OK\n");
                else
                    kprintf("FD_TEST_FAIL\n");
                if (run_user_prog("/bin/robofs-test"))
                    kprintf("USER_ROBOFS_OK\n");
                else
                    kprintf("USER_ROBOFS_FAIL\n");
                if (!robofs_selftest())
                    kprintf("ROBOFS_TEST_FAIL\n");
                robo_hw_subsystem_init(info);
                handoff = 1;
            } else {
                kprintf("USER_TEST_FAIL\n");
            }
            boot_bar(9900);
        }
    } else {
        kprintf("CTX_TEST_FAIL\n");
        kprintf("SCHED_TEST_FAIL\n");
        kprintf("THREAD_TEST_FAIL\n");
        kprintf("PROC_TEST_FAIL\n");
    }

    kprintf("idle\n");
    boot_bar(ROBO_BOOT_PROGRESS_MAX);
    kprintf("Desktop  ready\n");
    if (handoff)
        robo_init_start(info);
    for (;;) {
        robo_sched_reap();
        kfb_poll();
        __asm__ volatile ("hlt");
    }
}
