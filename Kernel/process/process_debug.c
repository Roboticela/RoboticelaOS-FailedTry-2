#include "process/process.h"
#include "process/process_table.h"
#include "arch/x86_64/mm/paging_cpu.h"
#include "kio.h"

static void HaltForever(void)
{
    for (;;)
        __asm__ volatile ("cli; hlt");
}

void ProcessDebugDump(const RoboProcess *process)
{
    RoboThread *t;
    unsigned files = 0;
    unsigned ipc = 0;

    if (!process) {
        kprintf("PROCESS  (null)\n");
        return;
    }
    if (process->fdTable)
        files = process->fdTable->openCount;
    if (process->ipc)
        ipc = process->ipc->objectCount;

    kprintf("========================================\n");
    kprintf("PROCESS\n");
    kprintf("========================================\n\n");
    kprintf("PID        : %llu\n", (unsigned long long)process->id);
    kprintf("Name       : %s\n", process->name);
    kprintf("State      : %s\n", ProcessStateName(process->state));
    kprintf("Parent PID : %llu\n",
            (unsigned long long)(process->parent ? process->parent->id : 0));
    kprintf("\nAddress Space\n");
    kprintf("------------\n");
    kprintf("CR3        : 0x%llx\n",
            (unsigned long long)(process->addressSpace ?
                                 process->addressSpace->cr3 : 0));
    kprintf("Shared kernel mappings : YES (PML4 slots 0 and 256)\n");
    kprintf("Private window         : 0xFFFF808000000000 (PML4 slot 257)\n");
    kprintf("\nThreads\n");
    kprintf("-------\n");
    kprintf("Count      : %llu\n", (unsigned long long)process->threadCount);
    for (t = process->threads; t; t = t->procNext) {
        kprintf("  TID %llu    : %s\n",
                (unsigned long long)t->id,
                ThreadStateName(t->state));
    }
    kprintf("\nResources\n");
    kprintf("---------\n");
    kprintf("Files      : %u\n", files);
    kprintf("IPC        : %u\n", ipc);
    kprintf("Environment: %s\n", process->environment ? "initialized" : "none");
    kprintf("Credentials: UID %llu / GID %llu\n",
            (unsigned long long)(process->credentials ?
                                 process->credentials->uid : 0),
            (unsigned long long)(process->credentials ?
                                 process->credentials->gid : 0));
    kprintf("\nCreated    : %llu\n", (unsigned long long)process->createdAt);
    kprintf("========================================\n");
}

void ProcessReport(void)
{
    RoboProcess *p;

    kprintf("Processes\n");
    kprintf("---------\n");
    kprintf("Live        : %llu\n", (unsigned long long)ProcessStatLive());
    kprintf("Created     : %llu\n", (unsigned long long)ProcessStatCreated());
    kprintf("Destroyed   : %llu\n", (unsigned long long)ProcessStatDestroyed());
    kprintf("PID    Name          State     Threads\n");
    kprintf("--------------------------------------\n");
    for (p = PtableHead(); p; p = p->next) {
        kprintf("%llu    %s  %s  %llu\n",
                (unsigned long long)p->id,
                p->name,
                ProcessStateName(p->state),
                (unsigned long long)p->threadCount);
    }
}

void ProcessFbReport(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    RoboProcess *p = CurrentProcess();

    KfbPuts("Proc   PID ", mute);
    KfbPutu((unsigned long long)(p ? p->id : 0), white);
    KfbPuts("  live ", mute);
    KfbPutu((unsigned long long)ProcessStatLive(), white);
    KfbPuts("  cr3 ", mute);
    KfbPuthex((unsigned long long)CpuReadCr3(), white);
    KfbPuts("\n", mute);
}

void ProcessPanicRelation(const RoboThread *thread, const char *reason)
{
    RoboProcess *cur = CurrentProcess();

    kprintf("========================================\n");
    kprintf("          KERNEL PANIC\n");
    kprintf("========================================\n\n");
    kprintf("Invalid Thread/Process Relationship\n\n");
    kprintf("TID      : %llu\n",
            (unsigned long long)(thread ? thread->id : 0));
    kprintf("PID      : %llu\n",
            (unsigned long long)(thread && thread->process ?
                                 thread->process->id : 0));
    kprintf("Thread Process : 0x%llx\n",
            (unsigned long long)(uintptr_t)(thread ? thread->process : 0));
    kprintf("Current Process: 0x%llx\n",
            (unsigned long long)(uintptr_t)cur);
    kprintf("\nReason:\n");
    kprintf("%s\n", reason ? reason :
            "Thread is executing with an inconsistent process association.");
    kprintf("\n========================================\n");
    HaltForever();
}

void ProcessPanicAspace(const RoboProcess *process, const RoboThread *thread,
                               PhysAddr expected, PhysAddr actual)
{
    kprintf("========================================\n");
    kprintf("          KERNEL PANIC\n");
    kprintf("========================================\n\n");
    kprintf("Address Space Mismatch\n\n");
    kprintf("PID        : %llu\n",
            (unsigned long long)(process ? process->id : 0));
    kprintf("TID        : %llu\n",
            (unsigned long long)(thread ? thread->id : 0));
    kprintf("\nExpected CR3 : 0x%llx\n", (unsigned long long)expected);
    kprintf("Actual CR3   : 0x%llx\n", (unsigned long long)actual);
    kprintf("\nThe active address space does not belong to the current process.\n");
    kprintf("\n========================================\n");
    HaltForever();
}
