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
 * Wrong thread → ROBO_PERMISSION_DENIED.
 */

#ifndef ROBO_WINDOW_THREAD_H
#define ROBO_WINDOW_THREAD_H

#include "RoboBase/RoboError.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t robo_window_thread_id_t;

#define ROBO_WINDOW_THREAD_NONE ((robo_window_thread_id_t)0u)

/** Bind the window-owner thread. tid==NONE clears enforcement. */
robo_result_t robo_window_thread_bind_owner(robo_window_thread_id_t tid);

robo_window_thread_id_t robo_window_thread_owner(void);

/**
 * Set simulated current thread for subsequent API calls on this process.
 * Tests use this to exercise wrong-thread errors.
 */
robo_result_t robo_window_thread_enter(robo_window_thread_id_t tid);

robo_window_thread_id_t robo_window_thread_current(void);

/**
 * ROBO_OK if owner unbound or current matches owner.
 * Otherwise ROBO_PERMISSION_DENIED.
 */
robo_result_t robo_window_thread_require_owner(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_THREAD_H */
