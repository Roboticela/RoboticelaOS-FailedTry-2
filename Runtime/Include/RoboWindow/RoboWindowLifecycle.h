/*
 * RoboWindow lifecycle state machine (Goals 7.1 / 7.7).
 *
 * Visibility / focus / min / max / fullscreen are orthogonal state flags
 * (see RoboWindowState). Lifecycle is object existence only:
 *
 *   uninitialized → created → closing → closed
 *
 * "Visible" and "hidden" are not lifecycle enums — they are state.visible.
 *
 * Close request (CREATED → CLOSING) does not destroy the native resource.
 * Destruction (→ CLOSED) releases the native handle exactly once.
 * Close requests are irreversible at this layer (no cancel API).
 *
 * Invalid transitions return RoboInvalidArgument (deterministic).
 * Repeated close while CLOSING is reported as RoboAlreadyExists by
 * WindowRequestClose.
 */

#ifndef ROBO_WINDOW_LIFECYCLE_H
#define ROBO_WINDOW_LIFECYCLE_H

#include "RoboBase/RoboError.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum WindowLifecycle {
    RoboWindowLifecycleUninitialized = 0,
    RoboWindowLifecycleCreated = 1,
    /** Close requested; native window may still exist until destroy. */
    RoboWindowLifecycleClosing = 2,
    /** Destroyed; id is no longer live. */
    RoboWindowLifecycleClosed = 3
} WindowLifecycle;

/** Non-zero if @p life is a defined lifecycle enumerator. */
int WindowLifecycleIsKnown(WindowLifecycle life);

/**
 * Non-zero if transition from @p from → @p to is allowed.
 * Allowed edges:
 *   UNINITIALIZED → CREATED
 *   CREATED → CLOSING
 *   CREATED → CLOSED   (destroy without explicit close request)
 *   CLOSING → CLOSED
 *   CLOSED → CLOSED    (idempotent stay; treated as allowed no-op)
 */
int WindowLifecycleCanTransition(WindowLifecycle from,
                                         WindowLifecycle to);

/**
 * Validate transition. OK or RoboInvalidArgument.
 * Same-state CLOSED→CLOSED returns RoboOk.
 */
Result WindowLifecycleCheckTransition(WindowLifecycle from,
                                                     WindowLifecycle to);

const char *WindowLifecycleName(WindowLifecycle life);

/**
 * Non-zero if mutating geometry/visibility/present ops are allowed.
 * Only CREATED (not CLOSING / CLOSED / UNINITIALIZED).
 */
int WindowLifecycleAllowsMutation(WindowLifecycle life);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_LIFECYCLE_H */
