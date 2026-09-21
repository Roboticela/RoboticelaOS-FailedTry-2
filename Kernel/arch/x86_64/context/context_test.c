#include "arch/x86_64/context/context_switch.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/tss.h"
#include "scheduler/scheduler.h"
#include "thread/thread.h"
#include "time/time.h"
#include "time/convert.h"
#include "kio.h"

#define CTX_AB_TARGET     100000ull
#define CTX_STRESS_N      10u
#define CTX_STRESS_TARGET 10000ull

static volatile uint64_t counterA;
static volatile uint64_t counterB;
static volatile uint64_t gRegOk;
static volatile uint64_t gRegBad;
static volatile uint64_t gRegPartner;
static volatile uint64_t gLocalA;
static volatile uint64_t gLocalB;
static volatile uint64_t gStackAAddr;
static volatile uint64_t gStackBAddr;
static volatile int gStackFail;
static volatile int gStage;
static volatile int gEntries;
static volatile int gStageBack;
static volatile uint64_t gStagePartner;
static volatile uint64_t gStress[CTX_STRESS_N];
static volatile uint64_t gYieldN;
static volatile int gRegDone;
static volatile int gRegpDone;
static volatile int gADone;
static volatile int gBDone;
static volatile int gRipDone;

static int tfail(const char *tag)
{
    kprintf("Context selftest: %s\n", tag);
    return 0;
}

static void stretch(void)
{
    uint64_t t = TimeNow();
    while (TimeNow() - t < (RoboNsPerMs / 2ull))
        __asm__ volatile ("pause");
}

static void CountLoop(volatile uint64_t *counter, uint64_t target)
{
    uint64_t i;
    for (i = 0; i < target; i++) {
        (*counter)++;
        if ((i & 255ull) == 0)
            stretch();
    }
}

static int WaitCounters(volatile uint64_t *vals, const uint64_t *targets,
                         unsigned n, uint64_t timeoutMs)
{
    uint64_t start = TimeMs();
    unsigned i;
    for (;;) {
        int done = 1;
        for (i = 0; i < n; i++) {
            if (vals[i] < targets[i])
                done = 0;
        }
        if (done)
            return 1;
        if (TimeMs() - start > timeoutMs)
            return 0;
        RoboSleep(10);
    }
}

static void ThreadA(void *arg)
{
    (void)arg;
    CountLoop(&counterA, CTX_AB_TARGET);
    gADone = 1;
}

static void ThreadB(void *arg)
{
    (void)arg;
    CountLoop(&counterB, CTX_AB_TARGET);
    gBDone = 1;
}

static void RegThread(void *arg)
{
    unsigned n;
    (void)arg;
    for (n = 0; n < 40u; n++) {
        RegPreserveIters(&gRegOk, &gRegBad, 1024ull);
        stretch();
    }
    gRegDone = 1;
}

static void RegPartner(void *arg)
{
    uint64_t i;
    (void)arg;
    for (i = 0; i < 40000ull; i++) {
        gRegPartner++;
        if ((i & 255ull) == 0)
            stretch();
    }
    gRegpDone = 1;
}

static void StackA(void *arg)
{
    uint64_t local = 0xA11AA11AA11AA11Aull;
    (void)arg;
    gStackAAddr = (uint64_t)(uintptr_t)&local;
    while (gLocalA < 30000ull) {
        if (local != 0xA11AA11AA11AA11Aull) {
            gStackFail = 1;
            return;
        }
        gLocalA++;
        if ((gLocalA & 255ull) == 0)
            stretch();
    }
}

static void StackB(void *arg)
{
    uint64_t local = 0xB22BB22BB22BB22Bull;
    (void)arg;
    gStackBAddr = (uint64_t)(uintptr_t)&local;
    while (gLocalB < 30000ull) {
        if (local != 0xB22BB22BB22BB22Bull) {
            gStackFail = 1;
            return;
        }
        gLocalB++;
        if ((gLocalB & 255ull) == 0)
            stretch();
    }
}

static void BusyMs(uint64_t ms)
{
    uint64_t t = TimeNow();
    while (TimeNow() - t < ms * RoboNsPerMs)
        __asm__ volatile ("pause");
}

static void staged(void *arg)
{
    (void)arg;
    gEntries++;
    gStage = 1;
    BusyMs(20);
    gStage = 2;
    BusyMs(20);
    gStage = 3;
    BusyMs(20);
    gStage = 4;
    gRipDone = 1;
}

static void StagePartner(void *arg)
{
    (void)arg;
    while (gStage < 4) {
        gStagePartner++;
        stretch();
    }
}

static void StressEntry(void *arg)
{
    uintptr_t i = (uintptr_t)arg;
    CountLoop(&gStress[i], CTX_STRESS_TARGET);
}

static void yielder(void *arg)
{
    unsigned i;
    (void)arg;
    for (i = 0; i < 16u; i++) {
        gYieldN++;
        ThreadYield();
    }
}

static int WaitFlag(volatile int *done, uint64_t timeoutMs)
{
    uint64_t start = TimeMs();
    while (!*done) {
        if (TimeMs() - start > timeoutMs)
            return 0;
        RoboSleep(10);
    }
    return 1;
}

int ContextSelftest(void)
{
    RoboThread *tr, *tp, *sa, *sb, *st, *sp;
    RoboThread *stress[CTX_STRESS_N];
    RoboThread *ya, *yb;
    uint64_t sw0, prem0, y0;
    uint64_t sw1, prem1;
    uint64_t abSw, abPrem;
    uint64_t targets[CTX_STRESS_N];
    unsigned i;
    int lastStage;
    int passes = 0;
    uint64_t ist1Before;

    kprintf("========================================\n");
    kprintf("      CONTEXT SWITCH TEST\n");
    kprintf("========================================\n\n");

    if (SchedulerState() != RoboSchedRunning)
        return tfail("scheduler not running");

    ist1Before = TssIst1();
    if (!ist1Before)
        return tfail("IST1 missing");

    /* Register preservation */
    gRegOk = 0;
    gRegBad = 0;
    gRegPartner = 0;
    gRegDone = 0;
    gRegpDone = 0;
    tr = ThreadCreate("ctx-reg", RegThread, 0);
    tp = ThreadCreate("ctx-regp", RegPartner, 0);
    if (!tr || !tp)
        return tfail("reg create");
    if (!WaitFlag(&gRegDone, 8000) || !WaitFlag(&gRegpDone, 4000))
        return tfail("reg timeout");
    if (gRegBad)
        return tfail("register corruption");
    if (gRegOk < 100)
        return tfail("register loop did not run");
    SchedReap();
    kprintf("[PASS] Register preservation\n");
    passes++;

    /* Stack isolation */
    gLocalA = 0;
    gLocalB = 0;
    gStackFail = 0;
    gStackAAddr = 0;
    gStackBAddr = 0;
    sa = ThreadCreate("ctx-sa", StackA, 0);
    sb = ThreadCreate("ctx-sb", StackB, 0);
    if (!sa || !sb)
        return tfail("stack create");
    {
        uint64_t start = TimeMs();
        while (gLocalA < 30000ull || gLocalB < 30000ull) {
            if (TimeMs() - start > 8000ull)
                return tfail("stack timeout");
            RoboSleep(10);
        }
    }
    if (gStackFail)
        return tfail("stack isolation");
    if (!gStackAAddr || !gStackBAddr || gStackAAddr == gStackBAddr)
        return tfail("shared stack");
    SchedReap();
    kprintf("[PASS] Stack switching\n");
    passes++;

    /* RIP continues at the next stage, not thread_entry */
    gStage = 0;
    gEntries = 0;
    gStageBack = 0;
    gStagePartner = 0;
    gRipDone = 0;
    st = ThreadCreate("ctx-rip", staged, 0);
    sp = ThreadCreate("ctx-ripp", StagePartner, 0);
    if (!st || !sp)
        return tfail("rip create");
    lastStage = 0;
    {
        uint64_t start = TimeMs();
        while (gStage < 4) {
            if (gStage < lastStage) {
                gStageBack = 1;
                break;
            }
            if (gStage > lastStage)
                lastStage = gStage;
            if (TimeMs() - start > 8000ull)
                return tfail("rip timeout");
            RoboSleep(5);
        }
    }
    if (gStageBack)
        return tfail("rip restarted");
    if (gStage != 4)
        return tfail("rip incomplete");
    if (gEntries != 1)
        return tfail("rip re-entered entry");
    if (!WaitFlag(&gRipDone, 2000))
        return tfail("rip term");
    SchedReap();
    kprintf("[PASS] RIP restoration\n");
    passes++;

    /* Mandatory A/B counter test — no yield in the loop */
    counterA = 0;
    counterB = 0;
    gADone = 0;
    gBDone = 0;
    sw0 = SchedSwitches();
    prem0 = SchedPreemptions();
    y0 = SchedYields();
    if (!ThreadCreate("ctx-a", ThreadA, 0) ||
        !ThreadCreate("ctx-b", ThreadB, 0))
        return tfail("A/B create");

    kprintf("Thread A target : %llu\n", (unsigned long long)CTX_AB_TARGET);
    kprintf("Thread B target : %llu\n", (unsigned long long)CTX_AB_TARGET);

    {
        uint64_t start = TimeMs();
        while (counterA < CTX_AB_TARGET || counterB < CTX_AB_TARGET) {
            if (TimeMs() - start > 20000ull)
                return tfail("A/B timeout");
            RoboSleep(10);
        }
    }
    if (!WaitFlag(&gADone, 2000) || !WaitFlag(&gBDone, 2000))
        return tfail("A/B term");
    RoboSleep(20);

    sw1 = SchedSwitches();
    prem1 = SchedPreemptions();
    abSw = sw1 - sw0;
    abPrem = prem1 - prem0;

    if (counterA != CTX_AB_TARGET)
        return tfail("A count");
    if (counterB != CTX_AB_TARGET)
        return tfail("B count");
    if (abPrem < 2)
        return tfail("no timer preemption");
    if (abSw < 4)
        return tfail("too few context switches");
    if (!gADone || !gBDone)
        return tfail("A/B not terminated");
    if (SchedYields() < y0)
        return tfail("yield counter wrapped");
    (void)y0;

    kprintf("\n");
    kprintf("Thread A result : %llu\n", (unsigned long long)counterA);
    kprintf("Thread B result : %llu\n", (unsigned long long)counterB);
    kprintf("\n");
    kprintf("Context switches : %llu\n", (unsigned long long)abSw);
    kprintf("Preemptions      : %llu\n", (unsigned long long)abPrem);
    kprintf("\n");
    kprintf("A state          : TERMINATED\n");
    kprintf("B state          : TERMINATED\n");
    kprintf("\n");
    kprintf("[PASS] Context switching\n");
    kprintf("========================================\n");
    SchedReap();
    passes++;

    /* Voluntary yield uses the same int $48 / iframe path */
    gYieldN = 0;
    y0 = SchedYields();
    ya = ThreadCreate("ctx-ya", yielder, 0);
    yb = ThreadCreate("ctx-yb", yielder, 0);
    if (!ya || !yb)
        return tfail("yield create");
    {
        uint64_t start = TimeMs();
        while (gYieldN < 32ull) {
            if (TimeMs() - start > 4000ull)
                return tfail("yield timeout");
            RoboSleep(5);
        }
    }
    if (SchedYields() <= y0)
        return tfail("voluntary switch");
    SchedReap();
    kprintf("[PASS] Voluntary switching\n");
    passes++;

    /* 10-thread stress, twice */
    for (i = 0; i < 2u; i++) {
        unsigned t;
        for (t = 0; t < CTX_STRESS_N; t++)
            gStress[t] = 0;
        for (t = 0; t < CTX_STRESS_N; t++) {
            stress[t] = ThreadCreate("ctx-n", StressEntry, (void *)(uintptr_t)t);
            if (!stress[t])
                return tfail("stress create");
            targets[t] = CTX_STRESS_TARGET;
        }
        if (!WaitCounters(gStress, targets, CTX_STRESS_N, 20000ull))
            return tfail("stress timeout");
        for (t = 0; t < CTX_STRESS_N; t++) {
            if (gStress[t] != CTX_STRESS_TARGET)
                return tfail("stress count");
        }
        RoboSleep(20);
        SchedReap();
    }
    kprintf("[PASS] Stress test\n");
    passes++;

    {
        if (TssIst1() != ist1Before)
            return tfail("IST1 mutated");
    }
    kprintf("[PASS] Double-fault IST preserved\n");
    passes++;

    X86ContextReport();
    kprintf("Context selftest %d checks\n", passes);
    (void)sp;
    return 1;
}
