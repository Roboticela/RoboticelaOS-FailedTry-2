#ifndef ROBO_SCHEDULER_H
#define ROBO_SCHEDULER_H

#include "thread/thread.h"
#include "scheduler/task.h"
#include <stdint.h>

struct Iframe;

/*
 * Phase 8: preemptive round-robin, kernel tasks, single x86_64 CPU.
 *
 * Quantum is in milliseconds of the Phase 7 monotonic clock.
 * The PIT fires at 100 Hz (10 ms); a 10 ms quantum is one tick.
 */
#define RoboSchedulerQuantumMs 10u

/* Software interrupt used for yield / sleep / exit. Not a PIC IRQ. */
#define RoboVecResched 48u

typedef enum {
    RoboSchedUninit = 0,
    RoboSchedInitialized,
    RoboSchedRunning,
    RoboSchedStopped
} SchedState;

int SchedulerInit(void);
void SchedulerStart(void);
int SchedulerReady(void);
SchedState SchedulerState(void);
RoboThread *CurrentTask(void);
RoboThread *IdleTask(void);
RoboThread *BootstrapTask(void);

void PreemptDisable(void);
void PreemptEnable(void);
unsigned PreemptDepth(void);

/*
 * Timer IRQ (vector 32): quantum / preemption.
 * Yield IRQ (vector 48): voluntary reschedule.
 * Both run with IF cleared (interrupt gate). Return the iframe that
 * isrCommon must restore; iretq then resumes that task.
 */
struct Iframe *SchedulerTick(struct Iframe *frame);
struct Iframe *SchedulerResched(struct Iframe *frame);

int SchedulerSleep(uint64_t milliseconds);
void SchedReap(void);

void SchedulerReport(void);
#ifndef SCHED_HOST_TEST
void SchedulerFbReport(void);
#endif
int SchedulerSelftest(void);

uint64_t SchedSwitches(void);
uint64_t SchedPreemptions(void);
uint64_t SchedYields(void);
uint64_t SchedCreates(void);
uint64_t SchedTerminations(void);
unsigned SchedRunnableCount(void);
unsigned SchedRrLog(uint64_t *out, unsigned max);

void SchedPanic(const char *msg);

#endif
