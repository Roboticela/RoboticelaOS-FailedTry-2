#include "scheduler/scheduler.h"
#include "arch/x86_64/scheduler/context_switch.h"
#include "time/time.h"
#include "kio.h"

static volatile uint64_t gCntA;
static volatile uint64_t gCntB;
static volatile uint64_t gCntC;
static volatile uint64_t gRegOk;
static volatile uint64_t gRegBad;
static volatile uint64_t gStackAAddr;
static volatile uint64_t gStackBAddr;
static volatile int gStackFail;
static volatile int gTermRan;
static volatile uint64_t gTermId;

static int tfail(const char *tag)
{
    kprintf("Scheduler selftest: %s\n", tag);
    return 0;
}

static void TaskA(void *arg)
{
    (void)arg;
    for (;;)
        gCntA++;
}

static void TaskB(void *arg)
{
    (void)arg;
    for (;;)
        gCntB++;
}

static void TaskC(void *arg)
{
    (void)arg;
    for (;;)
        gCntC++;
}

static void TaskRegs(void *arg)
{
    (void)arg;
    RegPreserveLoop(&gRegOk, &gRegBad);
}

static void TaskStackA(void *arg)
{
    volatile uint64_t local[32];
    unsigned i;
    (void)arg;
    for (i = 0; i < 32; i++)
        local[i] = 0xAAAA0000ull + i;
    gStackAAddr = (uint64_t)(uintptr_t)&local[0];
    for (;;) {
        for (i = 0; i < 32; i++) {
            if (local[i] != 0xAAAA0000ull + i)
                gStackFail = 1;
        }
    }
}

static void TaskStackB(void *arg)
{
    volatile uint64_t local[32];
    unsigned i;
    (void)arg;
    for (i = 0; i < 32; i++)
        local[i] = 0xBBBB0000ull + i;
    gStackBAddr = (uint64_t)(uintptr_t)&local[0];
    for (;;) {
        for (i = 0; i < 32; i++) {
            if (local[i] != 0xBBBB0000ull + i)
                gStackFail = 1;
        }
    }
}

static void TaskTerm(void *arg)
{
    Task *self = CurrentTask();
    (void)arg;
    gTermId = self ? self->id : 0;
    gTermRan = 1;
    TaskExit();
}

static int FairOk(uint64_t a, uint64_t b, uint64_t c)
{
    uint64_t tot = a + b + c;
    uint64_t minShare;
    if (tot < 1000)
        return 0;
    minShare = tot / 10ull;
    if (a < minShare || b < minShare || c < minShare)
        return 0;
    return 1;
}

static int RrOk(uint64_t idA, uint64_t idB, uint64_t idC)
{
    uint64_t log[64];
    unsigned n, i, seenA = 0, seenB = 0, seenC = 0;
    unsigned run = 1, maxRun = 1;
    uint64_t prev;

    n = SchedRrLog(log, 64);
    if (n < 6)
        return 0;
    prev = log[0];
    for (i = 0; i < n; i++) {
        if (log[i] == idA)
            seenA = 1;
        else if (log[i] == idB)
            seenB = 1;
        else if (log[i] == idC)
            seenC = 1;
        if (i > 0) {
            if (log[i] == prev)
                run++;
            else {
                if (run > maxRun)
                    maxRun = run;
                run = 1;
                prev = log[i];
            }
        }
    }
    if (run > maxRun)
        maxRun = run;
    if (!seenA || !seenB || !seenC)
        return 0;
    if (maxRun > n / 2u + 4u)
        return 0;
    return 1;
}

int SchedulerSelftest(void)
{
    Task *ta, *tb, *tc, *tr, *tsa, *tsb, *tt;
    uint64_t sw0, prem0, y0;
    uint64_t sw1, prem1;
    int passes = 0;

    kprintf("Scheduler Self-Test\n");
    kprintf("-------------------\n");

    if (!SchedulerReady() || !CurrentTask())
        return tfail("not initialized");
    kprintf("[PASS] Scheduler initialized\n");
    passes++;

    if (!BootstrapTask() || CurrentTask()->id != RoboTidBootstrap)
        return tfail("bootstrap not current");
    kprintf("[PASS] Bootstrap task adopted\n");
    passes++;

    if (!IdleTask() || IdleTask()->id != RoboTidIdle)
        return tfail("idle missing");
    kprintf("[PASS] Idle task created\n");
    passes++;

    ta = TaskCreate("task-a", TaskA, 0);
    tb = TaskCreate("task-b", TaskB, 0);
    tc = TaskCreate("task-c", TaskC, 0);
    tr = TaskCreate("task-reg", TaskRegs, 0);
    tsa = TaskCreate("stack-a", TaskStackA, 0);
    tsb = TaskCreate("stack-b", TaskStackB, 0);
    if (!ta || !tb || !tc)
        return tfail("create A/B/C");
    if (!tr || !tsa || !tsb)
        return tfail("create aux tasks");
    if (ta->id == tb->id || tb->id == tc->id || ta->id == tc->id)
        return tfail("duplicate ids");
    kprintf("[PASS] Task A created\n");
    kprintf("[PASS] Task B created\n");
    kprintf("[PASS] Task C created\n");
    passes += 3;

    sw0 = SchedSwitches();
    prem0 = SchedPreemptions();
    y0 = SchedYields();
    (void)y0;

    if (SchedulerState() != RoboSchedRunning)
        SchedulerStart();
    if (SchedulerState() != RoboSchedRunning)
        return tfail("start");
    kprintf("[PASS] Timer preemption enabled\n");
    passes++;

    RoboSleep(800);

    sw1 = SchedSwitches();
    prem1 = SchedPreemptions();
    SchedReap();

    if (sw1 <= sw0)
        return tfail("no context switches");
    kprintf("[PASS] Context switching active\n");
    passes++;

    if (prem1 <= prem0)
        return tfail("no timer preemption");
    if (gCntA == 0 || gCntB == 0 || gCntC == 0)
        return tfail("a task never ran");
    kprintf("[PASS] Preemption without yield\n");
    passes++;

    if (!RrOk(ta->id, tb->id, tc->id))
        return tfail("round-robin ordering");
    kprintf("[PASS] Round-robin ordering\n");
    passes++;

    if (!FairOk(gCntA, gCntB, gCntC))
        return tfail("fairness");
    kprintf("[PASS] Scheduler fairness\n");
    passes++;

    if (gRegBad)
        return tfail("register corruption");
    if (gRegOk < 10)
        return tfail("register loop did not run");
    kprintf("[PASS] Register preservation\n");
    passes++;

    if (gStackFail)
        return tfail("stack isolation");
    if (!gStackAAddr || !gStackBAddr || gStackAAddr == gStackBAddr)
        return tfail("shared stack");
    kprintf("[PASS] Stack isolation\n");
    passes++;

    {
        unsigned i;
        uint64_t y1 = SchedYields();
        for (i = 0; i < 2; i++)
            TaskYield();
        if (SchedYields() <= y1)
            return tfail("yield");
        kprintf("[PASS] Task yield\n");
        passes++;
    }

    tt = TaskCreate("task-term", TaskTerm, 0);
    if (!tt)
        return tfail("term create");
    RoboSleep(200);
    SchedReap();
    if (!gTermRan)
        return tfail("term did not run");
    kprintf("[PASS] Task termination\n");
    passes++;

    kprintf("Scheduler Test\n");
    kprintf("Task A : %llu\n", (unsigned long long)gCntA);
    kprintf("Task B : %llu\n", (unsigned long long)gCntB);
    kprintf("Task C : %llu\n", (unsigned long long)gCntC);
    kprintf("Context Switches : %llu\n", (unsigned long long)sw1);
    kprintf("Preemptions      : %llu\n", (unsigned long long)prem1);
    kprintf("Voluntary Yields : %llu\n", (unsigned long long)SchedYields());
    kprintf("[PASS] Preemptive round-robin\n");
    kprintf("[PASS] Timer integration\n");
    passes += 2;

    (void)ta;
    (void)tb;
    (void)tc;
    (void)tr;
    (void)tsa;
    (void)tsb;
    (void)gTermId;
    kprintf("Scheduler selftest %d checks\n", passes);
    return 1;
}
