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

static int gTalk;

#define Talk(...) do { if (gTalk) kprintf(__VA_ARGS__); } while (0)

static void BootBar(unsigned p)
{
    KfbSplashSet(p);
    Talk("Loader  %u percent\n", (unsigned)(p / 100u));
}

static int RunUserProg(const char *path)
{
    RoboProcess *p;
    unsigned i;
    int ok;

    p = ProcessCreateFromElf(path);
    if (!p)
        return 0;
    for (i = 0; i < 200000u; i++) {
        if (p->state == RoboProcessTerminated)
            break;
        ThreadYield();
        SchedReap();
    }
    ok = p->state == RoboProcessTerminated && p->exitStatus == 0;
    SchedReap();
    ProcessDestroy(p);
    return ok;
}

void kmain(const RoboBootInfo *info)
{
    int handoff = 0;
    int tests;
    KserialInit();
    KfbInit(info);
    tests = KbootSelftest(info);
    gTalk = tests || KbootDebug(info);
    KlogSetTalk(gTalk);
    ExecSetQuiet(!gTalk);
    Talk("kmain\n");
    Talk("serial init\n");
    Talk("framebuffer init %u x %u\n",
            info ? info->framebufferWidth : 0,
            info ? info->framebufferHeight : 0);
    BootBar(RoboBootProgressHandoff);
    KbootReport(info);
    if (tests)
        kprintf("Kernel  selftest\n");
    BootBar(4200);

    Talk("CPU  init\n");
    if (!CpuInit()) {
        if (tests) {
            kprintf("CPU_TEST_FAIL\n");
            kprintf("PMM_TEST_FAIL\n");
            kprintf("VMM_TEST_FAIL\n");
            kprintf("HEAP_TEST_FAIL\n");
        }
        Talk("idle\n");
        BootBar(RoboBootProgressMax);
        Talk("Desktop  ready\n");
        for (;;) {
            KfbPoll();
            __asm__ volatile ("pause");
        }
    }
    if (gTalk)
        CpuReport();
    if (KbootDebug(info))
        CpuFbReport();
    BootBar(4600);
    if (tests) {
        if (CpuSelftest())
            kprintf("CPU_TEST_OK\n");
        else
            kprintf("CPU_TEST_FAIL\n");
    }
    BootBar(5000);

    Talk("PMM  bootinfo %s  ver %u\n",
            info ? "present" : "missing",
            info ? (unsigned)info->version : 0);
    if (info) {
        Talk("PMM  map 0x%llx  %llu regions\n",
                (unsigned long long)info->memoryMap,
                (unsigned long long)info->memoryMapCount);
        Talk("PMM  kernel 0x%llx +%llu  bootinfo 0x%llx +%llu\n",
                (unsigned long long)info->kernelBase,
                (unsigned long long)info->kernelSize,
                (unsigned long long)info->bootinfoAddress,
                (unsigned long long)info->bootinfoSize);
    }

    if (!PmmInit(info)) {
        if (tests) {
            kprintf("PMM_TEST_FAIL\n");
            kprintf("VMM_TEST_FAIL\n");
            kprintf("HEAP_TEST_FAIL\n");
        }
    } else {
        if (gTalk)
            PmmReport();
        if (KbootDebug(info))
            PmmFbReport();
        BootBar(5400);
        if (tests) {
            if (PmmSelftest())
                kprintf("PMM_TEST_OK\n");
            else
                kprintf("PMM_TEST_FAIL\n");
        }
        BootBar(5800);

        if (!VmmInit(info)) {
            if (tests) {
                kprintf("VMM_TEST_FAIL\n");
                kprintf("HEAP_TEST_FAIL\n");
            }
        } else {
            if (gTalk)
                VmmReport();
            if (KbootDebug(info))
                VmmFbReport();
            BootBar(6200);
            if (tests) {
                if (VmmSelftest())
                    kprintf("VMM_TEST_OK\n");
                else
                    kprintf("VMM_TEST_FAIL\n");
            }
            BootBar(6600);

            if (!HeapInit()) {
                if (tests)
                    kprintf("HEAP_TEST_FAIL\n");
            } else {
                if (gTalk)
                    HeapReport();
                if (KbootDebug(info))
                    HeapFbReport();
                BootBar(7000);
                if (tests) {
                    if (HeapSelftest())
                        kprintf("HEAP_TEST_OK\n");
                    else
                        kprintf("HEAP_TEST_FAIL\n");
                }
                BootBar(7400);
            }
        }
    }

    if (tests) {
        if (VmmReady()) {
            if (PfSelftest())
                kprintf("PF_TEST_OK\n");
            else
                kprintf("PF_TEST_FAIL\n");
        } else {
            kprintf("PF_TEST_FAIL\n");
        }
    }
    BootBar(7800);

    if (!TimerInit()) {
        if (tests)
            kprintf("TIMER_TEST_FAIL\n");
    } else {
        if (gTalk)
            TimerReport();
        if (KbootDebug(info))
            TimerFbReport();
        BootBar(8200);
        if (tests) {
            if (TimerSelftest())
                kprintf("TIMER_TEST_OK\n");
            else
                kprintf("TIMER_TEST_FAIL\n");
        }
        BootBar(8600);
    }

    if (TimerReady() && HeapReady()) {
        if (!SchedulerInit()) {
            if (tests) {
                kprintf("CTX_TEST_FAIL\n");
                kprintf("SCHED_TEST_FAIL\n");
                kprintf("THREAD_TEST_FAIL\n");
                kprintf("PROC_TEST_FAIL\n");
            }
        } else {
            if (gTalk)
                SchedulerReport();
            if (KbootDebug(info))
                SchedulerFbReport();
            SchedulerStart();
            if (gTalk)
                X86ContextReport();
            if (KbootDebug(info))
                X86ContextFbReport();
            BootBar(9000);
            if (tests) {
                if (ContextSelftest())
                    kprintf("CTX_TEST_OK\n");
                else
                    kprintf("CTX_TEST_FAIL\n");
            }
            if (KbootDebug(info))
                X86ContextFbReport();
            BootBar(9200);
            if (tests) {
                if (SchedulerSelftest())
                    kprintf("SCHED_TEST_OK\n");
                else
                    kprintf("SCHED_TEST_FAIL\n");
            }
            if (gTalk)
                ThreadReport();
            if (KbootDebug(info))
                ThreadFbReport();
            BootBar(9400);
            if (tests) {
                if (ThreadSelftest())
                    kprintf("THREAD_TEST_OK\n");
                else
                    kprintf("THREAD_TEST_FAIL\n");
            }
            if (gTalk)
                ProcessReport();
            if (KbootDebug(info))
                ProcessFbReport();
            BootBar(9600);
            if (tests) {
                if (ProcessSelftest())
                    kprintf("PROC_TEST_OK\n");
                else
                    kprintf("PROC_TEST_FAIL\n");
            }
            BootBar(9850);
            if (FsInit(info)) {
                static RoboBlockDevice *rootDisk;
                int imported;

                if (gTalk)
                    FsReport();
                VfsInit();
                RobofsRegister();
                if (tests && !VfsSelftest())
                    kprintf("VFS_TEST_FAIL\n");
                /*
                 * The root volume is sized from what it has to hold. It used to be a constant 512 blocks,
                 * which was a guess that the image outgrew: once the image passed two megabytes the walk
                 * stopped partway through and every later file -- the fonts among them -- went missing with
                 * no more than a `VFS_IMPORT_FAIL` line to say so.
                 */
                rootDisk = MemdevCreate(ROBOFS_BLOCK_SIZE,
                                               FsRootBlocks(ROBOFS_BLOCK_SIZE));
                imported = -1;
                if (rootDisk && RobofsFormat(rootDisk) == 0 &&
                    VfsMountRoot("robofs", rootDisk, 0) == 0)
                    imported = VfsImportInitrd();
                else
                    kprintf("ROBOFS_ROOT_FAIL\n");
                if (imported < 0)
                    kprintf("VFS_IMPORT_FAIL  %d\n", imported);
                else if (imported > 0)
                    kprintf("VFS_IMPORT_PARTIAL  %d\n", imported);
                if (KbootDebug(info)) {
                    KfbPuts("RoboFS ready\n", 0xA1A1A6u);
                    VfsFbReport();
                    RobofsFbReport();
                }
                if (tests) {
                    if (ExecSelftest(info))
                        kprintf("USER_TEST_OK\n");
                    else
                        kprintf("USER_TEST_FAIL\n");
                }
                if (KbootDebug(info))
                    SyscallFbReport();
                if (tests) {
                    if (SyscallSelftest(info))
                        kprintf("SYSCALL_TEST_OK\n");
                    else
                        kprintf("SYSCALL_TEST_FAIL\n");
                }
                if (KbootDebug(info)) {
                    FdTableDump(CurrentProcess());
                    FdFbReport();
                }
                if (tests) {
                    if (FdSelftest())
                        kprintf("FD_TEST_OK\n");
                    else
                        kprintf("FD_TEST_FAIL\n");
                    if (RunUserProg("/bin/robofs-test"))
                        kprintf("USER_ROBOFS_OK\n");
                    else
                        kprintf("USER_ROBOFS_FAIL\n");
                    if (!RobofsSelftest())
                        kprintf("ROBOFS_TEST_FAIL\n");
                }
                HwSubsystemInit(info);
                handoff = 1;
            } else if (tests) {
                kprintf("USER_TEST_FAIL\n");
            }
            BootBar(9900);
        }
    } else if (tests) {
        kprintf("CTX_TEST_FAIL\n");
        kprintf("SCHED_TEST_FAIL\n");
        kprintf("THREAD_TEST_FAIL\n");
        kprintf("PROC_TEST_FAIL\n");
    }

    Talk("idle\n");
    BootBar(RoboBootProgressMax);
    Talk("Desktop  ready\n");
    if (handoff)
        InitStart(info);
    for (;;) {
        SchedReap();
        KfbPoll();
        __asm__ volatile ("hlt");
    }
}
