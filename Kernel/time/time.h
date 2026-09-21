#ifndef ROBO_TIME_H
#define ROBO_TIME_H

#include <stdint.h>

#define RoboTimerHz 100u
#define RoboTimerMax 32

enum ClockSource {
    RoboClockNone = 0,
    RoboClockTsc  = 1,
    RoboClockPit  = 2
};

typedef void (*TimerFn)(void *context);
typedef void (*SchedTickFn)(void);

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
uint64_t TimeNow(void);

/** Same as TimeNow(). */
uint64_t TimeNs(void);

/** Monotonic microseconds (now / 1000). */
uint64_t TimeUs(void);

/** Monotonic milliseconds (now / 1000000). */
uint64_t TimeMs(void);

/**
 * Block until at least `milliseconds` of monotonic time have elapsed.
 *
 * Stage 1 (no scheduler): waits on the PIT interrupt with HLT, then
 * finishes any remainder against the TSC/PIT clock. Does not busy-loop
 * for the full duration.
 *
 * Stage 2 (scheduler running): the current task is marked SLEEPING, a
 * one-shot timer wakes it, and the scheduler runs another task.
 *
 * Context:
 *     Process/kernel thread context only. Do not call from IRQ.
 */
void RoboSleep(uint64_t milliseconds);

int TimerInit(void);
int TimerReady(void);
void TimerReport(void);
#ifndef TIME_HOST_TEST
void TimerFbReport(void);
#endif
int TimerSelftest(void);

enum ClockSource TimerSource(void);
int TimerTscInvariant(void);
int TimerRdtscp(void);
uint64_t TimerFrequencyHz(void);
uint64_t TimerResolutionNs(void);
uint64_t TimerIrqCount(void);

/**
 * One-shot (periodNs == 0) or periodic deadline.
 * Returns a handle > 0, or 0 on failure.
 * Callback runs in interrupt context: no sleep, no allocate, no block.
 */
int TimerAdd(uint64_t deadlineNs, uint64_t periodNs,
                   TimerFn callback, void *context);
int TimerCancel(int handle);

/** Future scheduler: called from the PIT IRQ after expired events. */
void TimerSetSchedTick(SchedTickFn fn);

/** PIT IRQ0 hook. Architecture interrupt path only. */
void TimerIrq(void);

#endif
