#include "arch/x86_64/context/context.h"
#include "arch/x86_64/gdt.h"
#include "mm/vmm/paging.h"
#include "scheduler/scheduler.h"
#include "kio.h"

static void HaltForever(void)
{
    for (;;)
        __asm__ volatile ("cli; hlt");
}

static int canonical(uint64_t addr)
{
    int64_t s = (int64_t)addr;
    return (s >> 47) == 0 || (s >> 47) == -1;
}

int X86IframeValid(const struct Iframe *frame)
{
    int user;
    if (!frame)
        return 0;
    user = ((frame->cs & 3ull) == 3ull);
    if (user) {
        if (frame->cs != RoboGdtUserCodeRpl)
            return 0;
        if (frame->ss != RoboGdtUserDataRpl)
            return 0;
    } else {
        if (frame->cs != RoboGdtKernelCode)
            return 0;
        if (frame->ss != RoboGdtKernelData)
            return 0;
    }
    if (!frame->rip || !canonical(frame->rip))
        return 0;
    if (!frame->rsp || !canonical(frame->rsp))
        return 0;
    if (frame->rsp & 7ull)
        return 0;
    if (user) {
        if (frame->rip < RoboUserMin || frame->rip >= RoboUserMax)
            return 0;
        if (frame->rsp < RoboUserMin || frame->rsp >= RoboUserMax)
            return 0;
    }
    return 1;
}

void X86ContextFail(struct Iframe *current,
                           struct Iframe *next,
                           uint64_t curId, uint64_t nextId,
                           const char *curState, const char *nextState)
{
    kprintf("========================================\n");
    kprintf("          KERNEL PANIC\n");
    kprintf("========================================\n\n");
    kprintf("Context Switch Failure\n\n");
    kprintf("Current Thread : %llu\n", (unsigned long long)curId);
    kprintf("Next Thread    : %llu\n", (unsigned long long)nextId);
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
    kprintf("Current State  : %s\n", curState ? curState : "?");
    kprintf("Next State     : %s\n", nextState ? nextState : "?");
    kprintf("\n========================================\n");
    HaltForever();
}

void X86ContextFailFrames(struct Iframe *current,
                                  struct Iframe *next)
{
    RoboThread *cur = CurrentThread();
    const char *cs = cur ? ThreadStateName(cur->state) : "?";
    X86ContextFail(current, next,
                          cur ? cur->id : 0,
                          0,
                          cs, "?");
}

void X86ContextReport(void)
{
    kprintf("Context Switching\n");
    kprintf("-----------------\n");
    kprintf("Total       : %llu\n", (unsigned long long)SchedSwitches());
    kprintf("Preemptive  : %llu\n", (unsigned long long)SchedPreemptions());
    kprintf("Voluntary   : %llu\n", (unsigned long long)SchedYields());
}

#ifndef CONTEXT_HOST_TEST
void X86ContextFbReport(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    KfbPuts("Ctx  sw ", mute);
    KfbPutu((unsigned long long)SchedSwitches(), white);
    KfbPuts("  prem ", mute);
    KfbPutu((unsigned long long)SchedPreemptions(), white);
    KfbPuts("  vol ", mute);
    KfbPutu((unsigned long long)SchedYields(), white);
    KfbPuts("\n", mute);
}
#endif
