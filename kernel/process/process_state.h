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
    ROBO_PROCESS_NEW = 0,
    ROBO_PROCESS_RUNNING,
    ROBO_PROCESS_SLEEPING,
    ROBO_PROCESS_BLOCKED,
    ROBO_PROCESS_STOPPED,
    ROBO_PROCESS_TERMINATED
} robo_process_state_t;

static inline int robo_process_state_valid(robo_process_state_t state)
{
    return (unsigned)state <= (unsigned)ROBO_PROCESS_TERMINATED;
}

static inline int robo_process_transition_allowed(robo_process_state_t from,
                                                  robo_process_state_t to)
{
    if (!robo_process_state_valid(from) || !robo_process_state_valid(to))
        return 0;
    if (from == to)
        return 1;
    switch (from) {
    case ROBO_PROCESS_NEW:
        return to == ROBO_PROCESS_RUNNING || to == ROBO_PROCESS_TERMINATED;
    case ROBO_PROCESS_RUNNING:
        return to == ROBO_PROCESS_SLEEPING ||
               to == ROBO_PROCESS_BLOCKED ||
               to == ROBO_PROCESS_STOPPED ||
               to == ROBO_PROCESS_TERMINATED;
    case ROBO_PROCESS_SLEEPING:
    case ROBO_PROCESS_BLOCKED:
    case ROBO_PROCESS_STOPPED:
        return to == ROBO_PROCESS_RUNNING || to == ROBO_PROCESS_TERMINATED;
    case ROBO_PROCESS_TERMINATED:
        return 0;
    default:
        return 0;
    }
}

static inline const char *robo_process_state_name(robo_process_state_t state)
{
    switch (state) {
    case ROBO_PROCESS_NEW:        return "NEW";
    case ROBO_PROCESS_RUNNING:    return "RUNNING";
    case ROBO_PROCESS_SLEEPING:   return "SLEEPING";
    case ROBO_PROCESS_BLOCKED:    return "BLOCKED";
    case ROBO_PROCESS_STOPPED:    return "STOPPED";
    case ROBO_PROCESS_TERMINATED: return "TERMINATED";
    default:                      return "?";
    }
}

#endif
