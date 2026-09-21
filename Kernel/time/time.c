#include "time/time.h"
#include "time/convert.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/timer/tsc.h"
#include "arch/x86_64/timer/pit.h"
#include "scheduler/scheduler.h"
#include "kio.h"

extern void (*irqStubTable[16])(void);

#define RoboTscHzMin 10000000ull
#define RoboTscHzMax 15000000000ull

struct TimerSlot {
    int used;
    uint64_t deadline;
    uint64_t period;
    TimerFn callback;
    void *context;
};

static int gReady;
static enum ClockSource gSource;
static struct TscInfo gTsc;
static uint64_t gHz;
static uint64_t gEpochTsc;
static uint64_t gLastNs;
static uint64_t gIrqCount;
static uint64_t gResNs;
static SchedTickFn GSchedTick;
static struct TimerSlot gTimers[RoboTimerMax];

static void TimerPanic(void)
{
    kprintf("========================================\n");
    kprintf("          KERNEL PANIC\n");
    kprintf("========================================\n\n");
    kprintf("Timer initialization failed\n\n");
    kprintf("No reliable monotonic clock source available.\n");
    kprintf("\n========================================\n");
    for (;;)
        __asm__ volatile ("cli; hlt");
}

static uint64_t PitNowNs(void)
{
    uint64_t irqs, ticks, ns;
    uint32_t div;
    uint16_t rem;
    uint64_t flags;

    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    irqs = gIrqCount;
    rem = PitLatch0();
    div = PitDivisor();
    __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory");

    if (div < 1u)
        div = 1u;
    if (rem > div)
        rem = (uint16_t)div;
    ticks = irqs * (uint64_t)div + (uint64_t)(div - rem);
    ns = TicksToNs(ticks, RoboPitHz);
    if (ns < gLastNs)
        ns = gLastNs;
    gLastNs = ns;
    return ns;
}

static uint64_t TscNowNs(void)
{
    uint64_t t, ns;
    t = X86TscRead();
    if (t < gEpochTsc)
        t = gEpochTsc;
    ns = TicksToNs(t - gEpochTsc, gHz);
    if (ns < gLastNs)
        ns = gLastNs;
    gLastNs = ns;
    return ns;
}

uint64_t TimeNow(void)
{
    if (!gReady)
        return 0;
    if (gSource == RoboClockTsc)
        return TscNowNs();
    return PitNowNs();
}

uint64_t TimeNs(void)
{
    return TimeNow();
}

uint64_t TimeUs(void)
{
    return TimeNow() / RoboNsPerUs;
}

uint64_t TimeMs(void)
{
    return TimeNow() / RoboNsPerMs;
}

enum ClockSource TimerSource(void)
{
    return gSource;
}

int TimerTscInvariant(void)
{
    return gTsc.invariant;
}

int TimerRdtscp(void)
{
    return gTsc.rdtscp;
}

uint64_t TimerFrequencyHz(void)
{
    return gHz;
}

uint64_t TimerResolutionNs(void)
{
    return gResNs;
}

uint64_t TimerIrqCount(void)
{
    return gIrqCount;
}

int TimerReady(void)
{
    return gReady;
}

void TimerSetSchedTick(SchedTickFn fn)
{
    GSchedTick = fn;
}

static void FireExpired(uint64_t now)
{
    int i;
    for (i = 0; i < (int)RoboTimerMax; i++) {
        struct TimerSlot *s = &gTimers[i];
        if (!s->used || now < s->deadline)
            continue;
        if (s->callback)
            s->callback(s->context);
        if (s->period) {
            s->deadline = TimeDeadline(s->deadline, s->period);
            if (s->deadline <= now)
                s->deadline = TimeDeadline(now, s->period);
        } else {
            s->used = 0;
        }
    }
}

void TimerIrq(void)
{
    gIrqCount++;
    if (gReady)
        FireExpired(TimeNow());
    if (GSchedTick)
        GSchedTick();
}

int TimerAdd(uint64_t deadlineNs, uint64_t periodNs,
                   TimerFn callback, void *context)
{
    int i;
    uint64_t flags;
    if (!callback)
        return 0;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    for (i = 0; i < (int)RoboTimerMax; i++) {
        if (gTimers[i].used)
            continue;
        gTimers[i].used = 1;
        gTimers[i].deadline = deadlineNs;
        gTimers[i].period = periodNs;
        gTimers[i].callback = callback;
        gTimers[i].context = context;
        __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory");
        return i + 1;
    }
    __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory");
    return 0;
}

int TimerCancel(int handle)
{
    uint64_t flags;
    int idx;
    if (handle < 1 || handle > (int)RoboTimerMax)
        return 0;
    idx = handle - 1;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    gTimers[idx].used = 0;
    gTimers[idx].callback = 0;
    gTimers[idx].context = 0;
    __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory");
    return 1;
}

static void SleepWake(void *ctx)
{
    volatile int *done = (volatile int *)ctx;
    *done = 1;
}

void RoboSleep(uint64_t milliseconds)
{
    uint64_t deadline, now;
    volatile int done = 0;
    int handle;

    if (!gReady || milliseconds == 0)
        return;
    if (SchedulerSleep(milliseconds))
        return;
    deadline = TimeDeadline(TimeNow(), milliseconds * RoboNsPerMs);
    handle = TimerAdd(deadline, 0, SleepWake, (void *)&done);
    for (;;) {
        now = TimeNow();
        if (now >= deadline || done)
            break;
        if (deadline - now > 500000ull)
            __asm__ volatile ("hlt");
        else
            __asm__ volatile ("pause");
    }
    if (handle)
        TimerCancel(handle);
}

static uint64_t MeasureResolution(void)
{
    uint64_t prev, cur, min = UINT64_MAX;
    int i, seen = 0;
    prev = TimeNow();
    for (i = 0; i < 100000; i++) {
        cur = TimeNow();
        if (cur > prev) {
            uint64_t d = cur - prev;
            if (d < min)
                min = d;
            seen = 1;
            if (seen && i > 64)
                break;
        }
        prev = cur;
    }
    if (!seen || min == UINT64_MAX)
        return 1;
    return min;
}

static int HzSane(uint64_t hz)
{
    return hz >= RoboTscHzMin && hz <= RoboTscHzMax;
}

static void InstallIrqs(void)
{
    unsigned i;
    for (i = 0; i < 16; i++)
        IdtSet(RoboIrqBase + i, irqStubTable[i], 0);
}

int TimerInit(void)
{
    uint64_t cal;
    unsigned i;

    if (gReady)
        return 1;

    for (i = 0; i < RoboTimerMax; i++)
        gTimers[i].used = 0;

    TscProbe(&gTsc);
    InstallIrqs();
    PicInit();
    PitProgram(RoboTimerHz);
    PicUnmask(RoboPicIrqPit);
    __asm__ volatile ("sti" ::: "memory");

    gSource = RoboClockNone;
    gHz = 0;
    gLastNs = 0;
    gIrqCount = 0;

    if (gTsc.present) {
        /* ~30 ms PIT channel-2 one-shot. Documented calibration wait. */
        cal = PitCalibrateTsc(35795u);
        if (HzSane(cal)) {
            gHz = cal;
            gSource = RoboClockTsc;
            kprintf("Timer  TSC calibrated against PIT\n");
        } else if (HzSane(gTsc.hz)) {
            gHz = gTsc.hz;
            gSource = RoboClockTsc;
            kprintf("Timer  TSC frequency from CPUID 0x15\n");
        } else {
            kprintf("Timer  TSC present but frequency unknown\n");
        }
    } else {
        kprintf("Timer  TSC unavailable\n");
    }

    if (gSource == RoboClockTsc) {
        gEpochTsc = X86TscRead();
        gTsc.hz = gHz;
    } else {
        gSource = RoboClockPit;
        gHz = RoboPitHz;
        kprintf("Timer  using PIT fallback\n");
    }

    gReady = 1;
    gLastNs = 0;
    (void)TimeNow();
    gResNs = MeasureResolution();
    if (!gReady || gSource == RoboClockNone)
        TimerPanic();
    return 1;
}

void TimerReport(void)
{
    const char *src = "none";
    if (gSource == RoboClockTsc)
        src = "TSC";
    else if (gSource == RoboClockPit)
        src = "PIT";
    kprintf("Timer initialized\n");
    kprintf("Timer\n");
    kprintf("-----\n");
    kprintf("Clock Source : %s\n", src);
    kprintf("Invariant    : %s\n", gTsc.invariant ? "YES" : "NO");
    kprintf("RDTSCP       : %s\n", gTsc.rdtscp ? "YES" : "NO");
    kprintf("Frequency    : %llu Hz\n", (unsigned long long)gHz);
    kprintf("Resolution   : %llu ns\n", (unsigned long long)gResNs);
}

#ifndef TIME_HOST_TEST
void TimerFbReport(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    if (!gReady)
        return;
    KfbPuts("Timer  ", mute);
    KfbPuts(gSource == RoboClockTsc ? "TSC " : "PIT ", white);
    KfbPutu((unsigned long long)(gHz / 1000000ull), white);
    KfbPuts(" MHz  res ", mute);
    KfbPutu((unsigned long long)gResNs, white);
    KfbPuts(" ns\n", mute);
}
#endif
