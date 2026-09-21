#ifndef ROBO_PROCESS_STATE_H
#define ROBO_PROCESS_STATE_H

#include <stdint.h>

/*
 * Process states (separate from thread states).
 *
 * NEW         allocated, not yet running any thread
 * RUNNING     at least one thread is READY or RUNNING
 * SLEEPING    all live threads are SLEEPING
 * BLOCKED     all live threads are BLOCKED
 * STOPPED     explicitly stopped; threads not scheduled
 * TERMINATED  exited; no runnable threads
 *
 * Valid transitions:
 *   NEW        -> RUNNING | TERMINATED
 *   RUNNING    -> SLEEPING | BLOCKED | STOPPED | TERMINATED
 *   SLEEPING   -> RUNNING | TERMINATED
 *   BLOCKED    -> RUNNING | TERMINATED
 *   STOPPED    -> RUNNING | TERMINATED
 *   TERMINATED -> (none)
 */

typedef enum {
    RoboProcessNew = 0,
    RoboProcessRunning,
    RoboProcessSleeping,
    RoboProcessBlocked,
    RoboProcessStopped,
    RoboProcessTerminated
} ProcessState;

static inline int ProcessStateValid(ProcessState state)
{
    return (unsigned)state <= (unsigned)RoboProcessTerminated;
}

static inline int ProcessTransitionAllowed(ProcessState from,
                                                  ProcessState to)
{
    if (!ProcessStateValid(from) || !ProcessStateValid(to))
        return 0;
    if (from == to)
        return 1;
    switch (from) {
    case RoboProcessNew:
        return to == RoboProcessRunning || to == RoboProcessTerminated;
    case RoboProcessRunning:
        return to == RoboProcessSleeping ||
               to == RoboProcessBlocked ||
               to == RoboProcessStopped ||
               to == RoboProcessTerminated;
    case RoboProcessSleeping:
    case RoboProcessBlocked:
    case RoboProcessStopped:
        return to == RoboProcessRunning || to == RoboProcessTerminated;
    case RoboProcessTerminated:
        return 0;
    default:
        return 0;
    }
}

static inline const char *ProcessStateName(ProcessState state)
{
    switch (state) {
    case RoboProcessNew:        return "NEW";
    case RoboProcessRunning:    return "RUNNING";
    case RoboProcessSleeping:   return "SLEEPING";
    case RoboProcessBlocked:    return "BLOCKED";
    case RoboProcessStopped:    return "STOPPED";
    case RoboProcessTerminated: return "TERMINATED";
    default:                      return "?";
    }
}

#endif
