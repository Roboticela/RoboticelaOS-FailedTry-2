#ifndef ROBO_THREAD_STATE_H
#define ROBO_THREAD_STATE_H

#include <stdint.h>

/*
 * Thread states (Phase 9)
 *
 * NEW         allocated, not yet in the scheduler
 * READY       runnable, waiting for CPU; the only state on the ready queue
 * RUNNING     owns the CPU (exactly one per CPU; Phase 9 has one CPU)
 * BLOCKED     waiting for an external wake; not scheduled
 * SLEEPING    waiting for a timer deadline; not scheduled
 * TERMINATED  finished; never scheduled again
 *
 * Valid transitions:
 *   NEW       -> READY
 *   READY     -> RUNNING
 *   RUNNING   -> READY | BLOCKED | SLEEPING | TERMINATED
 *   BLOCKED   -> READY
 *   SLEEPING  -> READY
 *
 * TERMINATED has no successor. Invalid transitions are rejected.
 */

typedef enum {
    RoboThreadNew = 0,
    RoboThreadReady,
    RoboThreadRunning,
    RoboThreadBlocked,
    RoboThreadSleeping,
    RoboThreadTerminated
} ThreadState;

typedef enum {
    RoboThreadKernel = 0,
    RoboThreadUser = 1
} ThreadKind;

static inline int ThreadStateValid(ThreadState state)
{
    return (unsigned)state <= (unsigned)RoboThreadTerminated;
}

static inline int ThreadTransitionAllowed(ThreadState from,
                                                 ThreadState to)
{
    if (!ThreadStateValid(from) || !ThreadStateValid(to))
        return 0;
    if (from == to)
        return 1;
    switch (from) {
    case RoboThreadNew:
        return to == RoboThreadReady;
    case RoboThreadReady:
        return to == RoboThreadRunning;
    case RoboThreadRunning:
        return to == RoboThreadReady ||
               to == RoboThreadBlocked ||
               to == RoboThreadSleeping ||
               to == RoboThreadTerminated;
    case RoboThreadBlocked:
        return to == RoboThreadReady;
    case RoboThreadSleeping:
        return to == RoboThreadReady;
    case RoboThreadTerminated:
        return 0;
    default:
        return 0;
    }
}

static inline const char *ThreadStateName(ThreadState state)
{
    switch (state) {
    case RoboThreadNew:        return "NEW";
    case RoboThreadReady:      return "READY";
    case RoboThreadRunning:    return "RUNNING";
    case RoboThreadBlocked:    return "BLOCKED";
    case RoboThreadSleeping:   return "SLEEPING";
    case RoboThreadTerminated: return "TERMINATED";
    default:                     return "?";
    }
}

#endif
