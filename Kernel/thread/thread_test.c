#include "thread/thread.h"
#include "scheduler/scheduler.h"
#include "arch/x86_64/scheduler/context_switch.h"
#include "mm/vmm/vmm.h"
#include "time/time.h"
#include "kio.h"

static volatile uint64_t gPa, gPb;
static volatile uint64_t gSpin;
static volatile int gSlept;
static volatile uint64_t gSleepRuntime;
static volatile int gBlockedFlag;
static volatile int gWokeFlag;
static volatile int gTermRet;
static RoboThread *gBlocker;
static RoboThread *gSleeper;

static int tfail(const char *tag)
{
    kprintf("Thread selftest: %s\n", tag);
    return 0;
}

static void WorkerA(void *arg)
{
    (void)arg;
    for (;;)
        gPa++;
}

static void WorkerB(void *arg)
{
    (void)arg;
    for (;;)
        gPb++;
}

static void spinner(void *arg)
{
    (void)arg;
    for (;;)
        gSpin++;
}

static void sleeper(void *arg)
{
    RoboThread *self = CurrentThread();
    (void)arg;
    gSleeper = self;
    RoboSleep(100);
    if (self)
        gSleepRuntime = self->runtime;
    gSlept = 1;
    for (;;)
        ThreadYield();
}

static void blocker(void *arg)
{
    (void)arg;
    gBlocker = CurrentThread();
    gBlockedFlag = 1;
    ThreadBlock();
    gWokeFlag = 1;
    for (;;)
        ThreadYield();
}

static void TermReturn(void *arg)
{
    (void)arg;
    gTermRet = 1;
}

static int TestTransitions(void)
{
    RoboThread t;

    ThreadZero(&t);
    t.state = RoboThreadNew;
    if (!ThreadTransition(&t, RoboThreadReady) || t.state != RoboThreadReady)
        return tfail("NEW -> READY");
    kprintf("[PASS] NEW -> READY\n");

    if (!ThreadTransition(&t, RoboThreadRunning) || t.state != RoboThreadRunning)
        return tfail("READY -> RUNNING");
    kprintf("[PASS] READY -> RUNNING\n");

    if (!ThreadTransition(&t, RoboThreadReady) || t.state != RoboThreadReady)
        return tfail("RUNNING -> READY");
    kprintf("[PASS] RUNNING -> READY\n");

    if (!ThreadTransition(&t, RoboThreadRunning))
        return tfail("READY -> RUNNING (2)");
    if (!ThreadTransition(&t, RoboThreadBlocked) || t.state != RoboThreadBlocked)
        return tfail("RUNNING -> BLOCKED");
    kprintf("[PASS] RUNNING -> BLOCKED\n");

    if (!ThreadTransition(&t, RoboThreadReady) || t.state != RoboThreadReady)
        return tfail("BLOCKED -> READY");
    kprintf("[PASS] BLOCKED -> READY\n");

    if (!ThreadTransition(&t, RoboThreadRunning))
        return tfail("READY -> RUNNING (3)");
    if (!ThreadTransition(&t, RoboThreadSleeping) || t.state != RoboThreadSleeping)
        return tfail("RUNNING -> SLEEPING");
    kprintf("[PASS] RUNNING -> SLEEPING\n");

    if (!ThreadTransition(&t, RoboThreadReady) || t.state != RoboThreadReady)
        return tfail("SLEEPING -> READY");
    kprintf("[PASS] SLEEPING -> READY\n");

    if (!ThreadTransition(&t, RoboThreadRunning))
        return tfail("READY -> RUNNING (4)");
    if (!ThreadTransition(&t, RoboThreadTerminated) ||
        t.state != RoboThreadTerminated)
        return tfail("RUNNING -> TERMINATED");
    kprintf("[PASS] RUNNING -> TERMINATED\n");

    if (ThreadTransition(&t, RoboThreadReady) ||
        ThreadTransition(&t, RoboThreadRunning))
        return tfail("rejected TERMINATED transition");
    kprintf("[PASS] Invalid TERMINATED transitions rejected\n");
    return 1;
}

int ThreadSelftest(void)
{
    RoboThread *wa, *wb, *sp, *sl, *bl, *tr;
    RoboThread *cur, *boot, *idle;
    PhysAddr kcr3;
    uint64_t spin0;
    int passes = 0;

    kprintf("Thread Self-Test\n");
    kprintf("----------------\n");

    if (SchedulerState() != RoboSchedRunning)
        return tfail("scheduler not running");

    cur = CurrentThread();
    boot = BootstrapTask();
    idle = IdleTask();
    if (!cur || cur != boot || GetThreadId(boot) != RoboThreadIdBootstrap)
        return tfail("current thread");
    kprintf("[PASS] Current thread\n");
    passes++;

    if (!idle || GetThreadId(idle) != RoboThreadIdIdle)
        return tfail("idle id");
    kprintf("[PASS] Idle thread identity\n");
    passes++;

    if (!TestTransitions())
        return 0;
    passes++;

    wa = ThreadCreate("worker-a", WorkerA, 0);
    if (!wa)
        return tfail("create worker-a");
    kprintf("[PASS] Thread object created\n");
    if (GetThreadId(wa) == RoboThreadIdBootstrap ||
        GetThreadId(wa) == RoboThreadIdIdle)
        return tfail("id assigned");
    kprintf("[PASS] Thread ID assigned\n");
    if (MakeThreadState(wa) != RoboThreadReady)
        return tfail("initial state");
    kprintf("[PASS] Initial state correct\n");
    if (!wa->kernelStack || !wa->kernelStackSize)
        return tfail("stack allocated");
    kprintf("[PASS] Stack allocated\n");
    if (!wa->context || !ThreadIp(wa))
        return tfail("cpu context");
    kprintf("[PASS] CPU context initialized\n");
    if (wa->addressSpace != KernelAddressSpace())
        return tfail("address space");
    kprintf("[PASS] Address space bound\n");
    if (!ThreadIsValid(wa))
        return tfail("valid");
    passes += 6;

    wb = ThreadCreate("worker-b", WorkerB, 0);
    if (!wb)
        return tfail("create worker-b");

    ThreadDump(wa);
    gPa = 0;
    gPb = 0;
    RoboSleep(200);
    if (gPa == 0 || gPb == 0)
        return tfail("preemption");
    kprintf("[PASS] Timer preemption\n");
    kprintf("Thread A counter: %llu\n", (unsigned long long)gPa);
    kprintf("Thread B counter: %llu\n", (unsigned long long)gPb);
    passes++;

    kcr3 = VmmGetCr3();
    if (!wa->addressSpace || !wb->addressSpace || !boot->addressSpace)
        return tfail("aspace ptr");
    if (wa->addressSpace->cr3 != kcr3 ||
        wb->addressSpace->cr3 != kcr3 ||
        boot->addressSpace->cr3 != kcr3)
        return tfail("shared kernel aspace");
    if (wa->addressSpace != wb->addressSpace)
        return tfail("aspace identity");
    kprintf("[PASS] Shared kernel address space\n");
    passes++;

    if (wa->kernelStack == wb->kernelStack ||
        wa->StackTop == wb->StackTop)
        return tfail("shared kernel stack");
    kprintf("[PASS] Stack isolation\n");
    passes++;

    gSpin = 0;
    gSlept = 0;
    sl = ThreadCreate("sleeper", sleeper, 0);
    sp = ThreadCreate("spinner", spinner, 0);
    if (!sl || !sp)
        return tfail("sleep create");
    spin0 = gSpin;
    RoboSleep(250);
    if (!gSlept)
        return tfail("sleeper did not wake");
    if (gSpin <= spin0)
        return tfail("spinner stalled during sleep");
    kprintf("[PASS] Sleeping\n");
    kprintf("[PASS] Wake-up\n");
    passes += 2;

    gBlockedFlag = 0;
    gWokeFlag = 0;
    bl = ThreadCreate("blocker", blocker, 0);
    if (!bl)
        return tfail("block create");
    RoboSleep(250);
    if (!gBlockedFlag || !gBlocker)
        return tfail("blocker never ran");
    if (MakeThreadState(gBlocker) != RoboThreadBlocked)
        return tfail("not blocked");
    if (!ThreadWake(gBlocker))
        return tfail("wake");
    RoboSleep(250);
    if (!gWokeFlag)
        return tfail("block wake");
    kprintf("[PASS] Blocking\n");
    kprintf("[PASS] Blocked thread wake\n");
    passes += 2;

    gTermRet = 0;
    tr = ThreadCreate("term-ret", TermReturn, 0);
    if (!tr)
        return tfail("term create");
    RoboSleep(250);
    SchedReap();
    if (!gTermRet)
        return tfail("term return");
    kprintf("[PASS] Termination\n");
    passes++;

    (void)sp;
    (void)sl;
    (void)bl;
    (void)tr;
    kprintf("Thread selftest %d checks\n", passes);
    return 1;
}
