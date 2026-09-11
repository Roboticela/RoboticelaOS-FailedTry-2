#ifndef ROBO_SCHEDULER_H
#define ROBO_SCHEDULER_H

#include "thread/thread.h"
#include "scheduler/task.h"
#include <stdint.h>

struct robo_iframe;

/*
 * Phase 8: preemptive round-robin, kernel tasks, single x86_64 CPU.
 *
 * Quantum is in milliseconds of the Phase 7 monotonic clock.
 * The PIT fires at 100 Hz (10 ms); a 10 ms quantum is one tick.
 */
#define ROBO_SCHEDULER_QUANTUM_MS 10u

/* Software interrupt used for yield / sleep / exit. Not a PIC IRQ. */
#define ROBO_VEC_RESCHED 48u

typedef enum {
    ROBO_SCHED_UNINIT = 0,
    ROBO_SCHED_INITIALIZED,
    ROBO_SCHED_RUNNING,
    ROBO_SCHED_STOPPED
} robo_sched_state_t;

int robo_scheduler_init(void);
void robo_scheduler_start(void);
int robo_scheduler_ready(void);
robo_sched_state_t robo_scheduler_state(void);
RoboThread *robo_current_task(void);
RoboThread *robo_idle_task(void);
RoboThread *robo_bootstrap_task(void);

void robo_preempt_disable(void);
void robo_preempt_enable(void);
unsigned robo_preempt_depth(void);

/*
 * Timer IRQ (vector 32): quantum / preemption.
 * Yield IRQ (vector 48): voluntary reschedule.
 * Both run with IF cleared (interrupt gate). Return the iframe that
 * isr_common must restore; iretq then resumes that task.
 */
struct robo_iframe *robo_scheduler_tick(struct robo_iframe *frame);
struct robo_iframe *robo_scheduler_resched(struct robo_iframe *frame);

int robo_scheduler_sleep(uint64_t milliseconds);
void robo_sched_reap(void);

void robo_scheduler_report(void);
#ifndef SCHED_HOST_TEST
void robo_scheduler_fb_report(void);
#endif
int robo_scheduler_selftest(void);

uint64_t robo_sched_switches(void);
uint64_t robo_sched_preemptions(void);
uint64_t robo_sched_yields(void);
uint64_t robo_sched_creates(void);
uint64_t robo_sched_terminations(void);
unsigned robo_sched_runnable_count(void);
unsigned robo_sched_rr_log(uint64_t *out, unsigned max);

void robo_sched_panic(const char *msg);

#endif
