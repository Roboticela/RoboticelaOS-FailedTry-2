/*
 * RoboWindow native event loop (Goal 7.9).
 *
 * Architecture (Chromium MessagePump is technical reference only):
 *
 *   Native OS events
 *          ↓
 *   Backend process_events / wait_events
 *          ↓
 *   observe_* translation (close / size / move / focus / visibility)
 *          ↓
 *   RoboWindow event queue
 *          ↓
 *   Application poll
 *
 * One authoritative loop per process; owner-thread affinity.
 * Keyboard / mouse / pointer intentionally deferred.
 */

#ifndef ROBO_WINDOW_EVENT_LOOP_H
#define ROBO_WINDOW_EVENT_LOOP_H

#include "RoboBase/RoboError.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Timeout: wait forever (until wakeup or native event). */
#define ROBO_WINDOW_EVENT_WAIT_FOREVER ((uint32_t)0xFFFFFFFFu)

/**
 * Mark the event loop as started (idempotent).
 * Does not block. Owner-thread required when bound.
 */
robo_result_t robo_window_event_loop_start(void);

/**
 * Request stop. Wakes a blocked wait. Safe to call more than once.
 * Does not destroy windows.
 */
robo_result_t robo_window_event_loop_request_stop(void);

/** Non-zero after start until stop (or shutdown). */
int robo_window_event_loop_is_running(void);

/**
 * Non-blocking: drain native backend messages into RoboWindow observe_* /
 * queue. Same as process_events with reentrancy guard.
 * Nested call while already pumping → ROBO_BUSY.
 */
robo_result_t robo_window_event_loop_poll(void);

/**
 * One iteration:
 *   - if timeout_ms == 0: poll only
 *   - else: wait up to timeout (or FOREVER), then drain
 * Stop requested → ROBO_OK without waiting.
 * Nested → ROBO_BUSY.
 */
robo_result_t robo_window_event_loop_run_once(uint32_t timeout_ms);

/**
 * Run until request_stop or shutdown.
 * Each iteration: wait(FOREVER) then drain. Owner thread only.
 */
robo_result_t robo_window_event_loop_run(void);

/**
 * Wake a blocked wait_events / run_once / run.
 * May be called while pumping (no-op aside from stop flag effects).
 * Backend wakeup optional; always sets an in-process wake flag.
 */
robo_result_t robo_window_event_loop_wakeup(void);

/**
 * Non-zero while native pump / run_once is on the stack (reentrancy guard).
 */
int robo_window_event_loop_is_pumping(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_EVENT_LOOP_H */
