/*
 * RoboWindow lifecycle state machine (Goal 7.1).
 *
 * Visibility / focus / min / max / fullscreen are orthogonal state flags
 * (see RoboWindowState). Lifecycle is object existence only:
 *
 *   uninitialized → created → closing → closed
 *
 * Invalid transitions return ROBO_INVALID_ARGUMENT (deterministic).
 */

#ifndef ROBO_WINDOW_LIFECYCLE_H
#define ROBO_WINDOW_LIFECYCLE_H

#include "RoboBase/RoboError.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum robo_window_lifecycle {
    ROBO_WINDOW_LIFECYCLE_UNINITIALIZED = 0,
    ROBO_WINDOW_LIFECYCLE_CREATED = 1,
    ROBO_WINDOW_LIFECYCLE_CLOSING = 2,
    ROBO_WINDOW_LIFECYCLE_CLOSED = 3
} robo_window_lifecycle_t;

/** Non-zero if @p life is a defined lifecycle enumerator. */
int robo_window_lifecycle_is_known(robo_window_lifecycle_t life);

/**
 * Non-zero if transition from @p from → @p to is allowed.
 * Allowed edges:
 *   UNINITIALIZED → CREATED
 *   CREATED → CLOSING
 *   CREATED → CLOSED   (destroy without explicit close request)
 *   CLOSING → CLOSED
 *   CLOSED → CLOSED    (idempotent stay; treated as allowed no-op)
 */
int robo_window_lifecycle_can_transition(robo_window_lifecycle_t from,
                                         robo_window_lifecycle_t to);

/**
 * Validate transition. OK or ROBO_INVALID_ARGUMENT.
 * Same-state CLOSED→CLOSED returns ROBO_OK.
 */
robo_result_t robo_window_lifecycle_check_transition(robo_window_lifecycle_t from,
                                                     robo_window_lifecycle_t to);

const char *robo_window_lifecycle_name(robo_window_lifecycle_t life);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_LIFECYCLE_H */
