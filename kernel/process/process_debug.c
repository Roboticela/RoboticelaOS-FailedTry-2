#include "process/process.h"
#include "process/process_table.h"
#include "arch/x86_64/mm/paging_cpu.h"
#include "kio.h"

static void halt_forever(void)
{
    for (;;)
        __asm__ volatile ("cli; hlt");
}

void robo_process_debug_dump(const RoboProcess *process)
{
    RoboThread *t;
    unsigned files = 0;
    unsigned ipc = 0;

    if (!process) {
        kprintf("PROCESS  (null)\n");
        return;
    }
    if (process->files)
        files = process->files->open_count;
    if (process->ipc)
        ipc = process->ipc->object_count;

    kprintf("========================================\n");
    kprintf("PROCESS\n");
    kprintf("========================================\n\n");
    kprintf("PID        : %llu\n", (unsigned long long)process->id);
    kprintf("Name       : %s\n", process->name);
    kprintf("State      : %s\n", robo_process_state_name(process->state));
    kprintf("Parent PID : %llu\n",
            (unsigned long long)(process->parent ? process->parent->id : 0));
    kprintf("\nAddress Space\n");
    kprintf("------------\n");
    kprintf("CR3        : 0x%llx\n",
            (unsigned long long)(process->address_space ?
                                 process->address_space->cr3 : 0));
    kprintf("Shared kernel mappings : YES (PML4 slots 0 and 256)\n");
    kprintf("Private window         : 0xFFFF808000000000 (PML4 slot 257)\n");
    kprintf("\nThreads\n");
    kprintf("-------\n");
    kprintf("Count      : %llu\n", (unsigned long long)process->thread_count);
    for (t = process->threads; t; t = t->proc_next) {
        kprintf("  TID %llu    : %s\n",
                (unsigned long long)t->id,
                robo_thread_state_name(t->state));
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
    kprintf("\nCreated    : %llu\n", (unsigned long long)process->created_at);
    kprintf("========================================\n");
}

void robo_process_report(void)
{
    RoboProcess *p;

    kprintf("Processes\n");
    kprintf("---------\n");
    kprintf("Live        : %llu\n", (unsigned long long)robo_process_stat_live());
    kprintf("Created     : %llu\n", (unsigned long long)robo_process_stat_created());
    kprintf("Destroyed   : %llu\n", (unsigned long long)robo_process_stat_destroyed());
    kprintf("PID    Name          State     Threads\n");
    kprintf("--------------------------------------\n");
    for (p = robo_ptable_head(); p; p = p->next) {
        kprintf("%llu    %s  %s  %llu\n",
                (unsigned long long)p->id,
                p->name,
                robo_process_state_name(p->state),
                (unsigned long long)p->thread_count);
    }
}

void robo_process_fb_report(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    RoboProcess *p = robo_current_process();

    kfb_puts("Proc   PID ", mute);
    kfb_putu((unsigned long long)(p ? p->id : 0), white);
    kfb_puts("  live ", mute);
    kfb_putu((unsigned long long)robo_process_stat_live(), white);
    kfb_puts("  cr3 ", mute);
    kfb_puthex((unsigned long long)robo_cpu_read_cr3(), white);
    kfb_puts("\n", mute);
}

void robo_process_panic_relation(const RoboThread *thread, const char *reason)
{
    RoboProcess *cur = robo_current_process();

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
    halt_forever();
}

void robo_process_panic_aspace(const RoboProcess *process, const RoboThread *thread,
                               phys_addr_t expected, phys_addr_t actual)
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
    halt_forever();
}
