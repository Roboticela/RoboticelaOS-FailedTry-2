#include "scheduler/sched_internal.h"
#include "arch/x86_64/exception.h"
#include "arch/x86_64/context/context_switch.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/tss.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/syscall.h"
#include "mm/heap/heap.h"
#include "mm/vmm/vmm.h"
#include "time/time.h"
#include "time/convert.h"
#include "kio.h"
#include "thread/thread.h"
#include "process/process.h"

extern void Isr48(void);
extern char kstack[];
extern char kstackEnd[];

RoboThread *gCurrent;
RoboThread *gIdle;
RoboThread *gBootstrap;
RoboThread *gAll;
Runqueue gReady;
SchedState gSchedState;
unsigned gPreemptCount;

static uint64_t gNextId = 2;
static int gInSched;
static RoboThread *gReap;
static uint64_t gSwitches;
static uint64_t gPreemptions;
static uint64_t gYields;
static uint64_t gCreates;
static uint64_t gTerminations;

#define RoboRrLog 64
static uint64_t gRr[RoboRrLog];
static unsigned gRrI;
static unsigned gRrN;

static void SchedHalt(void)
{
    for (;;)
        __asm__ volatile ("cli; hlt");
}

uint64_t SchedLock(void)
{
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

void SchedUnlock(uint64_t flags)
{
    __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory");
}

static void SchedPanicInit(const char *msg)
{
    kprintf("========================================\n");
    kprintf("          KERNEL PANIC\n");
    kprintf("========================================\n\n");
    kprintf("Scheduler initialization failed\n\n");
    kprintf("%s\n", msg ? msg : "unknown");
    kprintf("\n========================================\n");
    SchedHalt();
}

void SchedPanic(const char *msg)
{
    kprintf("========================================\n");
    kprintf("          KERNEL PANIC\n");
    kprintf("========================================\n\n");
    kprintf("%s\n", msg ? msg : "scheduler panic");
    kprintf("\n========================================\n");
    SchedHalt();
}

void SchedPanicCorrupt(const char *detail, RoboThread *cur, RoboThread *next)
{
    kprintf("========================================\n");
    kprintf("          KERNEL PANIC\n");
    kprintf("========================================\n\n");
    kprintf("Scheduler corruption\n\n");
    kprintf("Current Thread : %llu %s\n",
            (unsigned long long)(cur ? cur->id : 0),
            cur ? cur->name : "?");
    kprintf("Next Thread    : %llu %s\n",
            (unsigned long long)(next ? next->id : 0),
            next ? next->name : "?");
    kprintf("State        : %u\n", (unsigned)gSchedState);
    kprintf("Detail       : %s\n", detail ? detail : "unknown");
    kprintf("\n========================================\n");
    SchedHalt();
}

uint64_t SchedQuantumNs(void)
{
    return (uint64_t)RoboSchedulerQuantumMs * RoboNsPerMs;
}

static int IdInUse(uint64_t id)
{
    RoboThread *t;
    for (t = gAll; t; t = t->allNext) {
        if (t->id == id)
            return 1;
    }
    return 0;
}

uint64_t SchedAllocId(void)
{
    uint64_t start = gNextId;
    uint64_t id;
    for (;;) {
        id = gNextId++;
        if (gNextId < 2)
            gNextId = 2;
        if (id >= 2 && !IdInUse(id))
            return id;
        if (gNextId == start)
            SchedPanic("thread ID space exhausted");
    }
}

void SchedAllAdd(RoboThread *thread)
{
    if (!thread)
        return;
    thread->allNext = gAll;
    gAll = thread;
}

void SchedNoteCreate(void)
{
    gCreates++;
}

void SchedNoteTerm(void)
{
    gTerminations++;
}

void SchedReapLater(RoboThread *thread)
{
    if (!thread || thread->reapQueued)
        return;
    thread->reapQueued = 1;
    thread->reapNext = gReap;
    gReap = thread;
}

static void AllRemove(RoboThread *thread)
{
    RoboThread **p;
    for (p = &gAll; *p; p = &(*p)->allNext) {
        if (*p == thread) {
            *p = thread->allNext;
            thread->allNext = 0;
            return;
        }
    }
}

void SchedReap(void)
{
    RoboThread *t, *next;
    uint64_t flags;

    flags = SchedLock();
    t = gReap;
    gReap = 0;
    SchedUnlock(flags);

    while (t) {
        next = t->reapNext;
        t->reapNext = 0;
        if (t == gCurrent || t == gIdle || t == gBootstrap) {
            t = next;
            continue;
        }
        AllRemove(t);
        ThreadStackFree(t);
        kfree(t);
        t = next;
    }
}

static void account(RoboThread *t, uint64_t now)
{
    if (!t || !t->lastStartTime)
        return;
    if (now >= t->lastStartTime)
        t->runtime += now - t->lastStartTime;
}

static void RrNote(uint64_t id)
{
    gRr[gRrI] = id;
    gRrI = (gRrI + 1u) % RoboRrLog;
    if (gRrN < RoboRrLog)
        gRrN++;
}

static struct Iframe *PickAndSwitch(struct Iframe *frame, int force)
{
    RoboThread *old;
    RoboThread *next;
    uint64_t now;
    int wasRunning;

    if (gSchedState != RoboSchedRunning || !frame || !gCurrent)
        return frame;
    if (gInSched)
        return frame;
    if (gPreemptCount)
        return frame;

    gInSched = 1;
    old = gCurrent;
    old->context = frame;
    now = TimeNow();
    account(old, now);

    if (!force && now < old->sliceEndNs) {
        gInSched = 0;
        return frame;
    }

    wasRunning = (old->state == RoboThreadRunning);

    if (wasRunning) {
        if (!ThreadTransition(old, RoboThreadReady))
            SchedPanicCorrupt("RUNNING -> READY", old, 0);
        if (old != gIdle)
            RqEnqueue(&gReady, old);
    }

    next = RqDequeue(&gReady);
    if (!next)
        next = gIdle;
    if (!next)
        SchedPanicCorrupt("no next thread", old, 0);

    if (next == old) {
        if (!ThreadTransition(old, RoboThreadRunning))
            old->state = RoboThreadRunning;
        old->lastStartTime = now;
        old->sliceEndNs = now + SchedQuantumNs();
        gInSched = 0;
        return frame;
    }

    if (!next->context || !X86IframeValid(next->context))
        X86ContextFail(old->context, next->context,
                              old->id, next->id,
                              ThreadStateName(old->state),
                              ThreadStateName(next->state));
    if (next->state != RoboThreadReady && next != gIdle)
        SchedPanicCorrupt("next not READY", old, next);

    gSwitches++;
    if (force && wasRunning)
        gYields++;
    else if (!force)
        gPreemptions++;
    RrNote(next->id);

    if (old->state == RoboThreadTerminated) {
        if (!(old->kind == RoboThreadUser && old->process &&
              old->process->state != RoboProcessTerminated &&
              !old->joinConsumed))
            SchedReapLater(old);
    }

    ArchPrepareThreadSwitch(old, next);

    gCurrent = next;
    if (CurrentProcess() != next->process)
        ProcessPanicRelation(next,
            "Thread is executing with an inconsistent process association.");
    if (!ThreadTransition(next, RoboThreadRunning)) {
        if (next == gIdle && next->state == RoboThreadReady)
            next->state = RoboThreadRunning;
        else
            SchedPanicCorrupt("READY -> RUNNING", old, next);
    }
    next->lastStartTime = now;
    next->sliceEndNs = now + SchedQuantumNs();
    /* Kernel stacks only. IST1 / double-fault stack is not touched. */
    TssSetRsp0(next->StackTop);
    SyscallKstack = next->StackTop;
    gInSched = 0;
    /*
     * gCurrent is already next, so a restored thread sees itself.
     * The old iframe was stored in old->context before this switch.
     */
    return X86ContextSwitch(old->context, next->context);
}

struct Iframe *SchedulerTick(struct Iframe *frame)
{
    return PickAndSwitch(frame, 0);
}

struct Iframe *SchedulerResched(struct Iframe *frame)
{
    return PickAndSwitch(frame, 1);
}

RoboThread *CurrentTask(void)
{
    return gCurrent;
}

RoboThread *IdleTask(void)
{
    return gIdle;
}

RoboThread *BootstrapTask(void)
{
    return gBootstrap;
}

SchedState SchedulerState(void)
{
    return gSchedState;
}

int SchedulerReady(void)
{
    return gSchedState == RoboSchedInitialized ||
           gSchedState == RoboSchedRunning;
}

void PreemptDisable(void)
{
    uint64_t flags = SchedLock();
    gPreemptCount++;
    SchedUnlock(flags);
}

void PreemptEnable(void)
{
    uint64_t flags = SchedLock();
    if (gPreemptCount)
        gPreemptCount--;
    SchedUnlock(flags);
}

unsigned PreemptDepth(void)
{
    return gPreemptCount;
}

uint64_t SchedSwitches(void) { return gSwitches; }
uint64_t SchedPreemptions(void) { return gPreemptions; }
uint64_t SchedYields(void) { return gYields; }
uint64_t SchedCreates(void) { return gCreates; }
uint64_t SchedTerminations(void) { return gTerminations; }

unsigned SchedRunnableCount(void)
{
    unsigned n = RqCount(&gReady);
    if (gCurrent && gCurrent->state == RoboThreadRunning && gCurrent != gIdle)
        n++;
    return n;
}

static int AdoptBootstrap(void)
{
    RoboThread *t;

    t = ThreadAdoptBootstrap(kstack, (uint64_t)(uintptr_t)kstackEnd,
                                    (size_t)((uintptr_t)kstackEnd - (uintptr_t)kstack));
    if (!t)
        return 0;
    SchedAllAdd(t);
    gBootstrap = t;
    gCurrent = t;
    return 1;
}

int SchedulerInit(void)
{
    if (gSchedState != RoboSchedUninit)
        SchedPanicInit("scheduler_init called twice");
    if (!TimerReady() || !HeapReady() || !VmmReady())
        SchedPanicInit("timer, heap, or VMM not ready");

    if (!ThreadSubsystemInit())
        SchedPanicInit("thread subsystem failed");
    if (!ProcessSubsystemInit())
        SchedPanicInit("process subsystem failed");

    RqInit(&gReady);
    gNextId = 2;
    gPreemptCount = 0;
    gInSched = 0;
    gReap = 0;
    gAll = 0;

    if (!AdoptBootstrap())
        SchedPanicInit("bootstrap thread allocation failed");
    kprintf("[ OK ] Bootstrap thread\n");

    gSchedState = RoboSchedInitialized;

    gIdle = ThreadCreate("idle", IdleEntry, 0);
    if (!gIdle)
        SchedPanicInit("idle thread creation failed");

    {
        uint64_t flags = SchedLock();
        RqRemove(&gReady, gIdle);
        gIdle->id = RoboThreadIdIdle;
        if (gIdle->state != RoboThreadReady)
            gIdle->state = RoboThreadReady;
        SchedUnlock(flags);
    }
    kprintf("[ OK ] Idle thread\n");
    kprintf("[ OK ] Scheduler integration\n");

    IdtSet(RoboVecResched, Isr48, 0);
    if (!IdtGatePresent(RoboVecResched))
        SchedPanicInit("reschedule vector missing");

    gSchedState = RoboSchedInitialized;
    kprintf("Scheduler init  quantum %u ms  bootstrap 0 idle 1\n",
            (unsigned)RoboSchedulerQuantumMs);
    return 1;
}

void SchedulerStart(void)
{
    uint64_t flags;
    uint64_t now;

    if (gSchedState == RoboSchedRunning)
        SchedPanic("scheduler_start called twice");
    if (gSchedState != RoboSchedInitialized)
        SchedPanic("scheduler_start before init");
    if (!gCurrent || !gIdle)
        SchedPanic("scheduler_start missing threads");

    flags = SchedLock();
    now = TimeNow();
    gCurrent->lastStartTime = now;
    gCurrent->sliceEndNs = now + SchedQuantumNs();
    if (gCurrent->state != RoboThreadRunning)
        gCurrent->state = RoboThreadRunning;
    gSchedState = RoboSchedRunning;
    TssSetRsp0(gCurrent->StackTop);
    SyscallKstack = gCurrent->StackTop;
    SchedUnlock(flags);
    kprintf("Scheduler start  current %llu %s\n",
            (unsigned long long)gCurrent->id, gCurrent->name);
}

void SchedulerReport(void)
{
    RoboThread *t;
    const char *st = "UNINIT";
    if (gSchedState == RoboSchedInitialized)
        st = "INITIALIZED";
    else if (gSchedState == RoboSchedRunning)
        st = "RUNNING";
    else if (gSchedState == RoboSchedStopped)
        st = "STOPPED";

    kprintf("Scheduler\n");
    kprintf("---------\n");
    kprintf("State       : %s\n", st);
    kprintf("Current     : Thread %llu %s\n",
            (unsigned long long)(gCurrent ? gCurrent->id : 0),
            gCurrent ? gCurrent->name : "-");
    kprintf("Runnable    : %u\n", SchedRunnableCount());
    kprintf("Quantum     : %u ms\n", (unsigned)RoboSchedulerQuantumMs);
    kprintf("Switches    : %llu\n", (unsigned long long)gSwitches);
    ThreadReport();
    kprintf("ID    Name       State      Runtime\n");
    kprintf("-----------------------------------\n");
    for (t = gAll; t; t = t->allNext) {
        kprintf("%llu    %s  %s  %llu\n",
                (unsigned long long)t->id,
                t->name,
                ThreadStateName(t->state),
                (unsigned long long)t->runtime);
    }
}

#ifndef SCHED_HOST_TEST
void SchedulerFbReport(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    KfbPuts("Sched  q ", mute);
    KfbPutu((unsigned long long)RoboSchedulerQuantumMs, white);
    KfbPuts(" ms  sw ", mute);
    KfbPutu((unsigned long long)gSwitches, white);
    KfbPuts("  prem ", mute);
    KfbPutu((unsigned long long)gPreemptions, white);
    KfbPuts("\n", mute);
}
#endif

unsigned SchedRrLog(uint64_t *out, unsigned max)
{
    unsigned i, n;
    if (!out || !max)
        return 0;
    n = gRrN < max ? gRrN : max;
    for (i = 0; i < n; i++) {
        unsigned idx;
        if (gRrN < RoboRrLog)
            idx = i;
        else
            idx = (gRrI + i) % RoboRrLog;
        out[i] = gRr[idx];
    }
    return n;
}
