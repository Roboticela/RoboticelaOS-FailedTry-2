/*
 * RoboWindow owner-thread contract (Goal 7.2).
 *
 * Without OS threads yet, ownership is modeled as process-local tokens:
 *   bind(owner) → set window-owner thread id
 *   enter(tid)  → set the simulated "current" thread for this fiber
 *
 * Mutating window APIs require current == owner when owner is bound.
 * Unbound owner (0) disables enforcement (default after init) for
 * backward-compatible single-threaded use.
 *
 * Wrong thread → RoboPermissionDenied.
 */

#ifndef ROBO_WINDOW_THREAD_H
#define ROBO_WINDOW_THREAD_H

#include "RoboBase/RoboError.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t WindowThreadId;

#define RoboWindowThreadNone ((WindowThreadId)0u)

/** Bind the window-owner thread. tid==NONE clears enforcement. */
Result WindowThreadBindOwner(WindowThreadId tid);

WindowThreadId WindowThreadOwner(void);

/**
 * Set simulated current thread for subsequent API calls on this process.
 * Tests use this to exercise wrong-thread errors.
 */
Result WindowThreadEnter(WindowThreadId tid);

WindowThreadId WindowThreadCurrent(void);

/**
 * RoboOk if owner unbound or current matches owner.
 * Otherwise RoboPermissionDenied.
 */
Result WindowThreadRequireOwner(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_THREAD_H */
