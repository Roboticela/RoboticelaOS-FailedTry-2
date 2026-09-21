#include "thread/thread.h"
#include "process/process.h"
#include "scheduler/sched_internal.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/exception.h"
#include "arch/x86_64/context/context_switch.h"
#include "mm/pmm.h"
#include "mm/vmm/vmm.h"
#include "mm/heap/heap.h"
#include "mm/page.h"
#include "time/time.h"
#include "time/convert.h"
#include "kio.h"

#define RoboKstackSlotPages \
    (RoboThreadKstackGuardPages + RoboThreadKstackPages)

static uint64_t gStackSlot;
static RoboAddressSpace gKernelAspace;
static int gThreadReady;

static void BytesZero(void *dst, size_t n)
{
    uint8_t *p = (uint8_t *)dst;
    while (n--)
        *p++ = 0;
}

RoboAddressSpace *KernelAddressSpace(void)
{
    return &gKernelAspace;
}

int ThreadSubsystemInit(void)
{
    gKernelAspace.cr3 = VmmGetCr3();
    gKernelAspace.cloned = 0;
    gKernelAspace.maps = 0;
    gStackSlot = 0;
    gThreadReady = 1;
    (void)gThreadReady;
    kprintf("Thread Subsystem\n");
    kprintf("----------------\n");
    kprintf("[ OK ] Initialized\n");
    kprintf("[ OK ] Thread ID allocator\n");
    kprintf("[ OK ] Thread stacks\n");
    return 1;
}

void ThreadZero(RoboThread *thread)
{
    if (thread)
        BytesZero(thread, sizeof(*thread));
}

void ThreadSetName(RoboThread *thread, const char *name)
{
    unsigned i;
    if (!thread)
        return;
    if (!name)
        name = "thread";
    for (i = 0; i + 1u < RoboThreadNameMax && name[i]; i++)
        thread->name[i] = name[i];
    thread->name[i] = 0;
}

ThreadId GetThreadId(const RoboThread *thread)
{
    return thread ? thread->id : (ThreadId)-1;
}

ThreadState MakeThreadState(const RoboThread *thread)
{
    return thread ? thread->state : RoboThreadTerminated;
}

uint64_t ThreadIp(const RoboThread *thread)
{
    return thread ? ThreadContextIp(thread->context) : 0;
}

uint64_t ThreadSp(const RoboThread *thread)
{
    if (!thread)
        return 0;
    if (thread->context)
        return ThreadContextSp(thread->context);
    return thread->StackTop;
}

int ThreadTransition(RoboThread *thread, ThreadState next)
{
    if (!thread)
        return 0;
    if (!ThreadTransitionAllowed(thread->state, next))
        return 0;
    thread->state = next;
    return 1;
}

int ThreadIsValid(const RoboThread *thread)
{
    if (!thread)
        return 0;
    if (!ThreadStateValid(thread->state))
        return 0;
    if (thread->state == RoboThreadNew)
        return 1;
    if (!thread->kernelStack || !thread->kernelStackSize)
        return 0;
    if (thread->state != RoboThreadTerminated && !thread->addressSpace)
        return 0;
    if (thread->id == RoboThreadIdBootstrap)
        return 1;
    if (thread->state == RoboThreadReady || thread->state == RoboThreadRunning) {
        if (!thread->context)
            return 0;
    }
    return 1;
}

int ThreadStackAlloc(RoboThread *thread)
{
    unsigned i;
    VirtAddr guard, base, va;
    PhysAddr pa;

    if (!thread)
        return 0;

    guard = RoboVmmKstackBase + gStackSlot * RoboKstackSlotPages * RoboPageSize;
    gStackSlot++;
    base = guard + RoboPageSize;

    for (i = 0; i < RoboThreadKstackPages; i++) {
        va = base + (VirtAddr)i * RoboPageSize;
        pa = PageAlloc();
        if (pa == RoboInvalidPhysAddr)
            goto fail;
        if (!VmmMap(va, pa, RoboVmRead | RoboVmWrite | RoboVmKernel)) {
            PageFree(pa);
            goto fail;
        }
    }

    thread->guardPage = guard;
    thread->kernelStack = (uint64_t)base;
    thread->kernelStackSize = RoboThreadKstackSize;
    thread->stack = (uint64_t)base;
    thread->stackSize = 0;
    thread->StackTop = (uint64_t)base + RoboThreadKstackSize;
    thread->ownsStack = 1;
    return 1;

fail:
    while (i > 0) {
        PhysAddr got = RoboInvalidPhysAddr;
        i--;
        va = base + (VirtAddr)i * RoboPageSize;
        if (VmmUnmap(va, &got) && got != RoboInvalidPhysAddr)
            PageFree(got);
    }
    return 0;
}

void ThreadStackSetupContext(RoboThread *thread)
{
    ThreadContext *f;
    uint8_t *top;

    if (!thread || !thread->StackTop)
        return;
    top = (uint8_t *)(uintptr_t)thread->StackTop;
    f = (ThreadContext *)(top - sizeof(*f));
    BytesZero(f, sizeof(*f));
    f->rip = (uint64_t)(uintptr_t)ThreadTrampoline;
    f->cs = RoboGdtKernelCode;
    f->rflags = 0x202ull;
    f->rsp = (uint64_t)(uintptr_t)f;
    f->ss = RoboGdtKernelData;
    thread->context = f;
}

void ThreadStackFree(RoboThread *thread)
{
    unsigned i;
    VirtAddr va;
    PhysAddr pa;

    if (!thread || !thread->ownsStack || !thread->kernelStack)
        return;
    for (i = 0; i < RoboThreadKstackPages; i++) {
        va = (VirtAddr)thread->kernelStack + (VirtAddr)i * RoboPageSize;
        pa = RoboInvalidPhysAddr;
        if (VmmUnmap(va, &pa) && pa != RoboInvalidPhysAddr)
            PageFree(pa);
    }
    thread->kernelStack = 0;
    thread->stack = 0;
    thread->ownsStack = 0;
    thread->context = 0;
}

void ThreadBootstrap(void)
{
    RoboThread *t = CurrentThread();
    if (t && t->entry)
        t->entry(t->argument);
    ThreadExit();
    SchedPanic("thread_exit returned");
}

void IdleEntry(void *arg)
{
    (void)arg;
    for (;;) {
        SchedReap();
        KfbPoll();
        __asm__ volatile ("hlt");
    }
}

RoboThread *ThreadAdoptBootstrap(void *stackBase, uint64_t StackTop,
                                        size_t stackSize)
{
    RoboThread *t;

    t = (RoboThread *)kmalloc(sizeof(*t));
    if (!t)
        return 0;
    ThreadZero(t);
    ThreadSetName(t, "bootstrap");
    t->id = RoboThreadIdBootstrap;
    t->state = RoboThreadRunning;
    t->kernelStack = (uint64_t)(uintptr_t)stackBase;
    t->kernelStackSize = (uint64_t)stackSize;
    t->stack = t->kernelStack;
    t->stackSize = 0;
    t->StackTop = StackTop;
    t->ownsStack = 0;
    t->addressSpace = KernelAddressSpace();
    t->createdAt = TimeNow();
    t->lastStartTime = t->createdAt;
    t->sliceEndNs = t->lastStartTime + SchedQuantumNs();
    if (KernelProcess() &&
        !ProcessAddThread(KernelProcess(), t)) {
        kfree(t);
        return 0;
    }
    return t;
}

RoboProcess *ThreadProcess(const RoboThread *thread)
{
    return thread ? thread->process : 0;
}

RoboThread *ThreadCreate(const char *name,
                               ThreadEntry entry,
                               void *argument)
{
    RoboProcess *p;

    p = CurrentProcess();
    if (!p)
        p = KernelProcess();
    return ThreadCreateInProcess(p, name, entry, argument);
}

RoboThread *ThreadCreateInProcess(RoboProcess *process,
                                          const char *name,
                                          ThreadEntry entry,
                                          void *argument)
{
    RoboThread *t;
    uint64_t flags;

    if (gSchedState == RoboSchedUninit || !entry || !process)
        return 0;

    t = (RoboThread *)kmalloc(sizeof(*t));
    if (!t)
        return 0;
    ThreadZero(t);
    ThreadSetName(t, name);
    t->entry = entry;
    t->argument = argument;
    t->state = RoboThreadNew;
    t->addressSpace = process->addressSpace;
    t->createdAt = TimeNow();

    if (!ThreadStackAlloc(t)) {
        kfree(t);
        return 0;
    }
    ThreadStackSetupContext(t);
    if (!t->context) {
        ThreadStackFree(t);
        kfree(t);
        return 0;
    }

    flags = SchedLock();
    t->id = SchedAllocId();
    if (!ThreadTransition(t, RoboThreadReady)) {
        SchedUnlock(flags);
        ThreadStackFree(t);
        kfree(t);
        return 0;
    }
    if (!ProcessAddThread(process, t)) {
        SchedUnlock(flags);
        ThreadStackFree(t);
        kfree(t);
        return 0;
    }
    SchedAllAdd(t);
    RqEnqueue(&gReady, t);
    SchedNoteCreate();
    SchedUnlock(flags);
    return t;
}

RoboThread *ThreadCreateUser(RoboProcess *process,
                                    const char *name,
                                    uint64_t userRip, uint64_t userRsp,
                                    uint64_t userRdi)
{
    RoboThread *t;
    ThreadContext *f;
    uint64_t flags;

    if (gSchedState == RoboSchedUninit || !process || !userRip || !userRsp)
        return 0;

    t = (RoboThread *)kmalloc(sizeof(*t));
    if (!t)
        return 0;
    ThreadZero(t);
    ThreadSetName(t, name);
    t->entry = 0;
    t->argument = 0;
    t->state = RoboThreadNew;
    t->kind = RoboThreadUser;
    t->addressSpace = process->addressSpace;
    t->createdAt = TimeNow();

    if (!ThreadStackAlloc(t)) {
        kfree(t);
        return 0;
    }
    ThreadStackSetupContext(t);
    if (!t->context) {
        ThreadStackFree(t);
        kfree(t);
        return 0;
    }

    t->stack = userRsp;
    t->stackSize = RoboUserStackSize;
    f = t->context;
    f->rip = userRip;
    f->cs = RoboGdtUserCodeRpl;
    f->ss = RoboGdtUserDataRpl;
    f->rsp = userRsp;
    f->rflags = 0x202ull;
    f->rdi = userRdi;
    f->rsi = 0;
    f->rdx = 0;
    f->rcx = 0;
    f->rax = 0;
    f->vector = 0;
    f->error = 0;

    flags = SchedLock();
    t->id = SchedAllocId();
    if (!ThreadTransition(t, RoboThreadReady)) {
        SchedUnlock(flags);
        ThreadStackFree(t);
        kfree(t);
        return 0;
    }
    if (!ProcessAddThread(process, t)) {
        SchedUnlock(flags);
        ThreadStackFree(t);
        kfree(t);
        return 0;
    }
    SchedAllAdd(t);
    RqEnqueue(&gReady, t);
    SchedNoteCreate();
    SchedUnlock(flags);
    return t;
}

void ThreadKillCurrent(void)
{
    RoboThread *t;
    uint64_t flags;

    flags = SchedLock();
    t = gCurrent;
    if (!t || t == gIdle) {
        SchedUnlock(flags);
        return;
    }
    if (t->sleepTimer)
        TimerCancel(t->sleepTimer);
    t->sleepTimer = 0;
    t->wakeDeadline = 0;
    if (!ThreadTransition(t, RoboThreadTerminated))
        t->state = RoboThreadTerminated;
    if (t->process) {
        if (t->process->state != RoboProcessTerminated &&
            t->process->exitStatus == 0)
            t->process->exitStatus = RoboProcessExitFault;
        ProcessRemoveThread(t->process, t);
    }
    SchedNoteTerm();
    SchedUnlock(flags);
}

void ThreadYield(void)
{
    if (gSchedState != RoboSchedRunning)
        return;
    if (gPreemptCount)
        return;
    __asm__ volatile ("int $48" ::: "memory");
}

void ThreadExit(void)
{
    RoboThread *t;
    RoboThread *joiner = 0;
    uint64_t flags;

    flags = SchedLock();
    t = gCurrent;
    if (!t) {
        SchedUnlock(flags);
        SchedPanic("exit with no current thread");
    }
    if (t == gIdle) {
        SchedUnlock(flags);
        SchedPanic("idle thread attempted to exit");
    }
    if (t->sleepTimer)
        TimerCancel(t->sleepTimer);
    t->sleepTimer = 0;
    t->wakeDeadline = 0;
    if (!ThreadTransition(t, RoboThreadTerminated))
        t->state = RoboThreadTerminated;
    t->joinPosted = 1;
    joiner = t->joinWaiter;
    t->joinWaiter = 0;
    if (t->process)
        ProcessRemoveThread(t->process, t);
    SchedNoteTerm();
    SchedUnlock(flags);
    if (joiner)
        ThreadWake(joiner);
    __asm__ volatile ("int $48" ::: "memory");
    SchedPanic("terminated thread resumed");
}

void ThreadBlock(void)
{
    RoboThread *t;
    uint64_t flags;

    if (gSchedState != RoboSchedRunning)
        return;
    flags = SchedLock();
    t = gCurrent;
    if (!t || t == gIdle) {
        SchedUnlock(flags);
        return;
    }
    if (!ThreadTransition(t, RoboThreadBlocked)) {
        SchedUnlock(flags);
        return;
    }
    SchedUnlock(flags);
    __asm__ volatile ("int $48" ::: "memory");
}

int ThreadWake(RoboThread *thread)
{
    uint64_t flags;
    if (!thread)
        return 0;
    flags = SchedLock();
    if (thread->state != RoboThreadBlocked) {
        SchedUnlock(flags);
        return 0;
    }
    if (!ThreadTransition(thread, RoboThreadReady)) {
        SchedUnlock(flags);
        return 0;
    }
    if (!RqContains(&gReady, thread))
        RqEnqueue(&gReady, thread);
    SchedUnlock(flags);
    return 1;
}

static void SleepWake(void *ctx)
{
    RoboThread *t = (RoboThread *)ctx;
    if (!t)
        return;
    t->sleepTimer = 0;
    t->wakeDeadline = 0;
    if (t->state != RoboThreadSleeping)
        return;
    if (!ThreadTransition(t, RoboThreadReady))
        return;
    if (!RqContains(&gReady, t))
        RqEnqueue(&gReady, t);
}

int ThreadSleepUntil(uint64_t deadlineNs)
{
    RoboThread *t;
    uint64_t flags;
    int handle;

    if (gSchedState != RoboSchedRunning)
        return 0;
    t = gCurrent;
    if (!t || t == gIdle)
        return 0;

    flags = SchedLock();
    if (!ThreadTransition(t, RoboThreadSleeping)) {
        SchedUnlock(flags);
        return 0;
    }
    t->wakeDeadline = deadlineNs;
    handle = TimerAdd(deadlineNs, 0, SleepWake, t);
    t->sleepTimer = handle;
    SchedUnlock(flags);
    if (!handle) {
        flags = SchedLock();
        if (t->state == RoboThreadSleeping) {
            ThreadTransition(t, RoboThreadReady);
            if (t == gCurrent)
                ThreadTransition(t, RoboThreadRunning);
        }
        t->wakeDeadline = 0;
        SchedUnlock(flags);
        return 0;
    }
    __asm__ volatile ("int $48" ::: "memory");
    return 1;
}

int SchedulerSleep(uint64_t milliseconds)
{
    uint64_t deadline;
    if (gSchedState != RoboSchedRunning || milliseconds == 0)
        return 0;
    deadline = TimeDeadline(TimeNow(), milliseconds * RoboNsPerMs);
    return ThreadSleepUntil(deadline);
}

RoboThread *CurrentThread(void)
{
    return gCurrent;
}

RoboThread *ThreadAll(void)
{
    return gAll;
}

RoboThread *ThreadById(uint64_t id)
{
    RoboThread *t;
    for (t = gAll; t; t = t->allNext) {
        if (t->id == id)
            return t;
    }
    return 0;
}

void ThreadDump(const RoboThread *thread)
{
    if (!thread) {
        kprintf("Thread  (null)\n");
        return;
    }
    kprintf("Thread\n");
    kprintf("------\n");
    kprintf("ID              : %llu\n", (unsigned long long)thread->id);
    kprintf("Name            : %s\n", thread->name);
    kprintf("State           : %s\n", ThreadStateName(thread->state));
    kprintf("RIP             : 0x%llx\n", (unsigned long long)ThreadIp(thread));
    kprintf("RSP             : 0x%llx\n", (unsigned long long)ThreadSp(thread));
    kprintf("Kernel Stack    : 0x%llx\n", (unsigned long long)thread->kernelStack);
    kprintf("Address Space   : 0x%llx\n",
            (unsigned long long)(thread->addressSpace ? thread->addressSpace->cr3 : 0));
    kprintf("Process         : %llu\n",
            (unsigned long long)(thread->process ? thread->process->id : 0));
}

void ThreadReport(void)
{
    RoboThread *t;

    kprintf("Threads\n");
    kprintf("-------\n");
    kprintf("ID    Name          State\n");
    kprintf("--------------------------\n");
    for (t = gAll; t; t = t->allNext) {
        kprintf("%llu    %s  %s\n",
                (unsigned long long)t->id,
                t->name,
                ThreadStateName(t->state));
    }
}

#ifndef THREAD_HOST_TEST
void ThreadFbReport(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    RoboThread *t = CurrentThread();
    KfbPuts("Thread  ", mute);
    if (!t) {
        KfbPuts("none\n", mute);
        return;
    }
    KfbPutu((unsigned long long)t->id, white);
    KfbPuts(" ", mute);
    KfbPuts(t->name, white);
    KfbPuts("  ", mute);
    KfbPuts(ThreadStateName(t->state), white);
    KfbPuts("\n", mute);
}
#endif
