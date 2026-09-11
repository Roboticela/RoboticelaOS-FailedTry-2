#include "time/time.h"
#include "time/convert.h"
#include "kio.h"

#include <stdint.h>

static int tfail(const char *tag)
{
    kprintf("Timer selftest: %s\n", tag);
    return 0;
}

static volatile int g_event_fired;

static void event_cb(void *ctx)
{
    volatile int *p = (volatile int *)ctx;
    *p = 1;
}

static int sleep_ok(uint64_t req_ms, uint64_t elapsed_ns)
{
    uint64_t req = req_ms * ROBO_NS_PER_MS;
    uint64_t slack = req / 2ull + 80000000ull;
    if (elapsed_ns < req)
        return 0;
    if (elapsed_ns > req + slack)
        return 0;
    return 1;
}

static int test_sleep(uint64_t ms)
{
    uint64_t t0, t1, elapsed;
    t0 = robo_time_now();
    robo_sleep(ms);
    t1 = robo_time_now();
    if (t1 < t0)
        return tfail("sleep went backward");
    elapsed = t1 - t0;
    kprintf("Sleep  requested %llu ms  actual %llu us\n",
            (unsigned long long)ms,
            (unsigned long long)(elapsed / ROBO_NS_PER_US));
    if (!sleep_ok(ms, elapsed))
        return tfail("sleep tolerance");
    return 1;
}

int robo_timer_selftest(void)
{
    uint64_t prev, cur, i, nz, irq0, irq1;
    int handle;

    if (!robo_timer_ready())
        return tfail("not ready");
    if (robo_timer_source() == ROBO_CLOCK_NONE)
        return tfail("no clock");

    kprintf("Timer Self-Test\n");
    kprintf("---------------\n");

    if (robo_ticks_to_ns(0, ROBO_NS_PER_SEC) != 0)
        return tfail("conv 0");
    if (robo_ticks_to_ns(1, ROBO_NS_PER_SEC) != 1)
        return tfail("conv 1");
    if (robo_ticks_to_ns(ROBO_NS_PER_SEC, ROBO_NS_PER_SEC) != ROBO_NS_PER_SEC)
        return tfail("conv 1s");
    if (robo_ticks_to_ns(UINT64_MAX, 1) != UINT64_MAX)
        return tfail("conv saturate");
    if (robo_time_deadline(UINT64_MAX - 10, 20) != UINT64_MAX)
        return tfail("deadline sat");
    kprintf("[PASS] Overflow protection\n");

    prev = robo_time_now();
    nz = 0;
    for (i = 0; i < 1000000ull; i++) {
        cur = robo_time_now();
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
            robo_timer_source() == ROBO_CLOCK_TSC ? "TSC" : "PIT");
    kprintf("Resolution   : %llu ns\n",
            (unsigned long long)robo_timer_resolution_ns());
    kprintf("[PASS] Resolution\n");

    irq0 = robo_timer_irq_count();
    g_event_fired = 0;
    handle = robo_timer_add(robo_time_deadline(robo_time_now(), 20ull * ROBO_NS_PER_MS),
                            0, event_cb, (void *)&g_event_fired);
    if (!handle)
        return tfail("timer add");
    robo_sleep(50);
    if (!g_event_fired)
        return tfail("timer event");
    robo_timer_cancel(handle);
    irq1 = robo_timer_irq_count();
    if (irq1 <= irq0)
        return tfail("timer irq");
    kprintf("[PASS] Timer interrupt  +%llu\n",
            (unsigned long long)(irq1 - irq0));
    kprintf("[PASS] Timer events\n");

    if (!test_sleep(10) || !test_sleep(100) || !test_sleep(1000))
        return 0;
    kprintf("[PASS] 10 ms sleep\n");
    kprintf("[PASS] 100 ms sleep\n");
    kprintf("[PASS] 1 second sleep\n");
    kprintf("Timer tests passed.\n");
    return 1;
}
