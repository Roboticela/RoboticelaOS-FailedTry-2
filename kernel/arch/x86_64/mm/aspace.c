#include "process/process.h"
#include "thread/thread.h"
#include "arch/x86_64/mm/paging_cpu.h"
#include "kio.h"

static uint64_t g_cr3_reloads;
static uint64_t g_same;
static uint64_t g_cross;
static int g_switch_log;

void robo_process_set_switch_log(int on)
{
    g_switch_log = on ? 1 : 0;
}

uint64_t robo_arch_cr3_reloads(void)
{
    return g_cr3_reloads;
}

uint64_t robo_aspace_stat_same(void)
{
    return g_same;
}

uint64_t robo_aspace_stat_cross(void)
{
    return g_cross;
}

void robo_arch_switch_address_space(RoboAddressSpace *address_space)
{
    phys_addr_t want;
    phys_addr_t have;

    if (!address_space || !address_space->cr3)
        return;
    want = address_space->cr3;
    have = robo_cpu_read_cr3();
    if (have == want)
        return;
    robo_cpu_write_cr3(want);
    g_cr3_reloads++;
}

void robo_arch_prepare_thread_switch(RoboThread *old, RoboThread *next)
{
    RoboProcess *from_p;
    RoboProcess *to_p;
    phys_addr_t expect;
    phys_addr_t actual;
    int same;

    if (!next)
        return;
    to_p = next->process;
    from_p = old ? old->process : 0;
    if (!to_p)
        robo_process_panic_relation(next, "next thread has no process");
    if (old && !from_p)
        robo_process_panic_relation(old, "current thread has no process");

    same = (from_p == to_p);
    if (same)
        g_same++;
    else
        g_cross++;

    if (!same)
        robo_arch_switch_address_space(next->address_space ?
                                       next->address_space :
                                       (to_p ? to_p->address_space : 0));

    expect = 0;
    if (next->address_space)
        expect = next->address_space->cr3;
    else if (to_p && to_p->address_space)
        expect = to_p->address_space->cr3;
    actual = robo_cpu_read_cr3();
    if (expect && actual != expect)
        robo_process_panic_aspace(to_p, next, expect, actual);

    if (next->kind == ROBO_THREAD_USER)
        robo_cpu_write_fs_base(next->user_fs_base);
    else
        robo_cpu_write_fs_base(0);

    if (!g_switch_log)
        return;

    if (same) {
        kprintf("THREAD SWITCH\n\n");
        kprintf("PID : %llu\n",
                (unsigned long long)(to_p ? to_p->id : 0));
        kprintf("From TID : %llu\n",
                (unsigned long long)(old ? old->id : 0));
        kprintf("To TID   : %llu\n",
                (unsigned long long)next->id);
        kprintf("\nAddress Space : SAME\n");
        kprintf("CR3 Reload   : NO\n");
    } else {
        kprintf("PROCESS SWITCH\n\n");
        kprintf("From:\n");
        kprintf("  PID : %llu\n",
                (unsigned long long)(from_p ? from_p->id : 0));
        kprintf("  TID : %llu\n",
                (unsigned long long)(old ? old->id : 0));
        kprintf("  CR3 : 0x%llx\n",
                (unsigned long long)(old && old->address_space ?
                                     old->address_space->cr3 : 0));
        kprintf("\nTo:\n");
        kprintf("  PID : %llu\n",
                (unsigned long long)(to_p ? to_p->id : 0));
        kprintf("  TID : %llu\n",
                (unsigned long long)next->id);
        kprintf("  CR3 : 0x%llx\n",
                (unsigned long long)expect);
        kprintf("\nAddress Space : SWITCH\n");
    }
}
