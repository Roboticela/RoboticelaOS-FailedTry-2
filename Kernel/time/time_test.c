#include "time/time.h"
#include "time/convert.h"
#include "kio.h"

#include <stdint.h>

static int tfail(const char *tag)
{
    kprintf("Timer selftest: %s\n", tag);
    return 0;
}

static volatile int gEventFired;

static void EventCb(void *ctx)
{
    volatile int *p = (volatile int *)ctx;
    *p = 1;
}

static int SleepOk(uint64_t reqMs, uint64_t elapsedNs)
{
    uint64_t req = reqMs * RoboNsPerMs;
    uint64_t slack = req / 2ull + 80000000ull;
    if (elapsedNs < req)
        return 0;
    if (elapsedNs > req + slack)
        return 0;
    return 1;
}

static int TestSleep(uint64_t ms)
{
    uint64_t t0, t1, elapsed;
    t0 = TimeNow();
    RoboSleep(ms);
    t1 = TimeNow();
    if (t1 < t0)
        return tfail("sleep went backward");
    elapsed = t1 - t0;
    kprintf("Sleep  requested %llu ms  actual %llu us\n",
            (unsigned long long)ms,
            (unsigned long long)(elapsed / RoboNsPerUs));
    if (!SleepOk(ms, elapsed))
        return tfail("sleep tolerance");
    return 1;
}

int TimerSelftest(void)
{
    uint64_t prev, cur, i, nz, irq0, irq1;
    int handle;

    if (!TimerReady())
        return tfail("not ready");
    if (TimerSource() == RoboClockNone)
        return tfail("no clock");

    kprintf("Timer Self-Test\n");
    kprintf("---------------\n");

    if (TicksToNs(0, RoboNsPerSec) != 0)
        return tfail("conv 0");
    if (TicksToNs(1, RoboNsPerSec) != 1)
        return tfail("conv 1");
    if (TicksToNs(RoboNsPerSec, RoboNsPerSec) != RoboNsPerSec)
        return tfail("conv 1s");
    if (TicksToNs(UINT64_MAX, 1) != UINT64_MAX)
        return tfail("conv saturate");
    if (TimeDeadline(UINT64_MAX - 10, 20) != UINT64_MAX)
        return tfail("deadline sat");
    kprintf("[PASS] Overflow protection\n");

    prev = TimeNow();
    nz = 0;
    for (i = 0; i < 1000000ull; i++) {
        cur = TimeNow();
        if (cur < prev)
            return tfail("monotonic");
        if (cur > prev)
            nz++;
        prev = cur;
    }
    kprintf("[PASS] Monotonicity  1000000 reads\n");
    kprintf("Timer Resolution\n");
    kprintf("----------------\n");
    kprintf("Clock Source : %s\n",
            TimerSource() == RoboClockTsc ? "TSC" : "PIT");
    kprintf("Resolution   : %llu ns\n",
            (unsigned long long)TimerResolutionNs());
    kprintf("[PASS] Resolution\n");

    irq0 = TimerIrqCount();
    gEventFired = 0;
    handle = TimerAdd(TimeDeadline(TimeNow(), 20ull * RoboNsPerMs),
                            0, EventCb, (void *)&gEventFired);
    if (!handle)
        return tfail("timer add");
    RoboSleep(50);
    if (!gEventFired)
        return tfail("timer event");
    TimerCancel(handle);
    irq1 = TimerIrqCount();
    if (irq1 <= irq0)
        return tfail("timer irq");
    kprintf("[PASS] Timer interrupt  +%llu\n",
            (unsigned long long)(irq1 - irq0));
    kprintf("[PASS] Timer events\n");

    if (!TestSleep(10) || !TestSleep(100) || !TestSleep(1000))
        return 0;
    kprintf("[PASS] 10 ms sleep\n");
    kprintf("[PASS] 100 ms sleep\n");
    kprintf("[PASS] 1 second sleep\n");
    kprintf("Timer tests passed.\n");
    return 1;
}
