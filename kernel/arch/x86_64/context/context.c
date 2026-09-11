#include "arch/x86_64/context/context.h"
#include "arch/x86_64/gdt.h"
#include "mm/vmm/paging.h"
#include "scheduler/scheduler.h"
#include "kio.h"

static void halt_forever(void)
{
    for (;;)
        __asm__ volatile ("cli; hlt");
}

static int canonical(uint64_t addr)
{
    int64_t s = (int64_t)addr;
    return (s >> 47) == 0 || (s >> 47) == -1;
}

int robo_x86_iframe_valid(const struct robo_iframe *frame)
{
    int user;
    if (!frame)
        return 0;
    user = ((frame->cs & 3ull) == 3ull);
    if (user) {
        if (frame->cs != ROBO_GDT_USER_CODE_RPL)
            return 0;
        if (frame->ss != ROBO_GDT_USER_DATA_RPL)
            return 0;
    } else {
        if (frame->cs != ROBO_GDT_KERNEL_CODE)
            return 0;
        if (frame->ss != ROBO_GDT_KERNEL_DATA)
            return 0;
    }
    if (!frame->rip || !canonical(frame->rip))
        return 0;
    if (!frame->rsp || !canonical(frame->rsp))
        return 0;
    if (frame->rsp & 7ull)
        return 0;
    if (user) {
        if (frame->rip < ROBO_USER_MIN || frame->rip >= ROBO_USER_MAX)
            return 0;
        if (frame->rsp < ROBO_USER_MIN || frame->rsp >= ROBO_USER_MAX)
            return 0;
    }
    return 1;
}

void robo_x86_context_fail(struct robo_iframe *current,
                           struct robo_iframe *next,
                           uint64_t cur_id, uint64_t next_id,
                           const char *cur_state, const char *next_state)
{
    kprintf("========================================\n");
    kprintf("          KERNEL PANIC\n");
    kprintf("========================================\n\n");
    kprintf("Context Switch Failure\n\n");
    kprintf("Current Thread : %llu\n", (unsigned long long)cur_id);
    kprintf("Next Thread    : %llu\n", (unsigned long long)next_id);
    kprintf("\n");
    kprintf("Current RIP    : 0x%llx\n",
            (unsigned long long)(current ? current->rip : 0));
    kprintf("Current RSP    : 0x%llx\n",
            (unsigned long long)(current ? current->rsp : 0));
    kprintf("\n");
    kprintf("Next RIP       : 0x%llx\n",
            (unsigned long long)(next ? next->rip : 0));
    kprintf("Next RSP       : 0x%llx\n",
            (unsigned long long)(next ? next->rsp : 0));
    kprintf("\n");
    kprintf("Current State  : %s\n", cur_state ? cur_state : "?");
    kprintf("Next State     : %s\n", next_state ? next_state : "?");
    kprintf("\n========================================\n");
    halt_forever();
}

void robo_x86_context_fail_frames(struct robo_iframe *current,
                                  struct robo_iframe *next)
{
    RoboThread *cur = robo_current_thread();
    const char *cs = cur ? robo_thread_state_name(cur->state) : "?";
    robo_x86_context_fail(current, next,
                          cur ? cur->id : 0,
                          0,
                          cs, "?");
}

void robo_x86_context_report(void)
{
    kprintf("Context Switching\n");
    kprintf("-----------------\n");
    kprintf("Total       : %llu\n", (unsigned long long)robo_sched_switches());
    kprintf("Preemptive  : %llu\n", (unsigned long long)robo_sched_preemptions());
    kprintf("Voluntary   : %llu\n", (unsigned long long)robo_sched_yields());
}

#ifndef CONTEXT_HOST_TEST
void robo_x86_context_fb_report(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    kfb_puts("Ctx  sw ", mute);
    kfb_putu((unsigned long long)robo_sched_switches(), white);
    kfb_puts("  prem ", mute);
    kfb_putu((unsigned long long)robo_sched_preemptions(), white);
    kfb_puts("  vol ", mute);
    kfb_putu((unsigned long long)robo_sched_yields(), white);
    kfb_puts("\n", mute);
}
#endif
