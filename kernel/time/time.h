#ifndef ROBO_TIME_H
#define ROBO_TIME_H

#include <stdint.h>

#define ROBO_TIMER_HZ 100u
#define ROBO_TIMER_MAX 32

enum robo_clock_source {
    ROBO_CLOCK_NONE = 0,
    ROBO_CLOCK_TSC  = 1,
    ROBO_CLOCK_PIT  = 2
};

typedef void (*robo_timer_fn)(void *context);
typedef void (*robo_sched_tick_fn)(void);

/**
 * Return monotonic kernel time.
 *
 * Unit:
 *     nanoseconds since timer initialization (epoch 0).
 *
 * Semantics:
 *     - monotonic elapsed machine time
 *     - never intentionally moves backward
 *     - independent of RTC, NTP, timezone, and calendar time
 *
 * Context:
 *     Safe in normal kernel and interrupt context.
 *     Does not allocate, block, or take a lock (single-CPU).
 *
 * SMP:
 *     Not synchronized across CPUs. Single bootstrap CPU only.
 */
uint64_t robo_time_now(void);

/** Same as robo_time_now(). */
uint64_t robo_time_ns(void);

/** Monotonic microseconds (now / 1000). */
uint64_t robo_time_us(void);

/** Monotonic milliseconds (now / 1000000). */
uint64_t robo_time_ms(void);

/**
 * Block until at least `milliseconds` of monotonic time have elapsed.
 *
 * Stage 1 (no scheduler): waits on the PIT interrupt with HLT, then
 * finishes any remainder against the TSC/PIT clock. Does not busy-loop
 * for the full duration.
 *
 * Stage 2 (future scheduler): register a deadline via robo_timer_add()
 * and block the task; the timer interrupt will make it runnable.
 *
 * Context:
 *     Process/kernel thread context only. Do not call from IRQ.
 */
void robo_sleep(uint64_t milliseconds);

int robo_timer_init(void);
int robo_timer_ready(void);
void robo_timer_report(void);
#ifndef TIME_HOST_TEST
void robo_timer_fb_report(void);
#endif
int robo_timer_selftest(void);

enum robo_clock_source robo_timer_source(void);
int robo_timer_tsc_invariant(void);
int robo_timer_rdtscp(void);
uint64_t robo_timer_frequency_hz(void);
uint64_t robo_timer_resolution_ns(void);
uint64_t robo_timer_irq_count(void);

/**
 * One-shot (period_ns == 0) or periodic deadline.
 * Returns a handle > 0, or 0 on failure.
 * Callback runs in interrupt context: no sleep, no allocate, no block.
 */
int robo_timer_add(uint64_t deadline_ns, uint64_t period_ns,
                   robo_timer_fn callback, void *context);
int robo_timer_cancel(int handle);

/** Future scheduler: called from the PIT IRQ after expired events. */
void robo_timer_set_sched_tick(robo_sched_tick_fn fn);

/** PIT IRQ0 hook. Architecture interrupt path only. */
void robo_timer_irq(void);

#endif
