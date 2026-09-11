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
    ROBO_THREAD_NEW = 0,
    ROBO_THREAD_READY,
    ROBO_THREAD_RUNNING,
    ROBO_THREAD_BLOCKED,
    ROBO_THREAD_SLEEPING,
    ROBO_THREAD_TERMINATED
} robo_thread_state_t;

typedef enum {
    ROBO_THREAD_KERNEL = 0,
    ROBO_THREAD_USER = 1
} robo_thread_kind_t;

static inline int robo_thread_state_valid(robo_thread_state_t state)
{
    return (unsigned)state <= (unsigned)ROBO_THREAD_TERMINATED;
}

static inline int robo_thread_transition_allowed(robo_thread_state_t from,
                                                 robo_thread_state_t to)
{
    if (!robo_thread_state_valid(from) || !robo_thread_state_valid(to))
        return 0;
    if (from == to)
        return 1;
    switch (from) {
    case ROBO_THREAD_NEW:
        return to == ROBO_THREAD_READY;
    case ROBO_THREAD_READY:
        return to == ROBO_THREAD_RUNNING;
    case ROBO_THREAD_RUNNING:
        return to == ROBO_THREAD_READY ||
               to == ROBO_THREAD_BLOCKED ||
               to == ROBO_THREAD_SLEEPING ||
               to == ROBO_THREAD_TERMINATED;
    case ROBO_THREAD_BLOCKED:
        return to == ROBO_THREAD_READY;
    case ROBO_THREAD_SLEEPING:
        return to == ROBO_THREAD_READY;
    case ROBO_THREAD_TERMINATED:
        return 0;
    default:
        return 0;
    }
}

static inline const char *robo_thread_state_name(robo_thread_state_t state)
{
    switch (state) {
    case ROBO_THREAD_NEW:        return "NEW";
    case ROBO_THREAD_READY:      return "READY";
    case ROBO_THREAD_RUNNING:    return "RUNNING";
    case ROBO_THREAD_BLOCKED:    return "BLOCKED";
    case ROBO_THREAD_SLEEPING:   return "SLEEPING";
    case ROBO_THREAD_TERMINATED: return "TERMINATED";
    default:                     return "?";
    }
}

#endif
