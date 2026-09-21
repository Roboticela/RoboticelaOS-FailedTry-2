/*
 * RoboWindow native event loop (Goal 7.9).
 *
 * Architecture (Chromium MessagePump is technical reference only):
 *
 *   Native OS events
 *          ↓
 *   Backend ProcessEvents / WaitEvents
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
#define RoboWindowEventWaitForever ((uint32_t)0xFFFFFFFFu)

/**
 * Mark the event loop as started (idempotent).
 * Does not block. Owner-thread required when bound.
 */
Result WindowEventLoopStart(void);

/**
 * Request stop. Wakes a blocked wait. Safe to call more than once.
 * Does not destroy windows.
 */
Result WindowEventLoopRequestStop(void);

/** Non-zero after start until stop (or shutdown). */
int WindowEventLoopIsRunning(void);

/**
 * Non-blocking: drain native backend messages into RoboWindow observe_* /
 * queue. Same as ProcessEvents with reentrancy guard.
 * Nested call while already pumping → RoboBusy.
 */
Result WindowEventLoopPoll(void);

/**
 * One iteration:
 *   - if timeoutMs == 0: poll only
 *   - else: wait up to timeout (or FOREVER), then drain
 * Stop requested → RoboOk without waiting.
 * Nested → RoboBusy.
 */
Result WindowEventLoopRunOnce(uint32_t timeoutMs);

/**
 * Run until request_stop or shutdown.
 * Each iteration: wait(FOREVER) then drain. Owner thread only.
 */
Result WindowEventLoopRun(void);

/**
 * Wake a blocked WaitEvents / run_once / run.
 * May be called while pumping (no-op aside from stop flag effects).
 * Backend wakeup optional; always sets an in-process wake flag.
 */
Result WindowEventLoopWakeup(void);

/**
 * Non-zero while native pump / run_once is on the stack (reentrancy guard).
 */
int WindowEventLoopIsPumping(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_EVENT_LOOP_H */
