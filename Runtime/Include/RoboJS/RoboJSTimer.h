/*
 * RoboJS timers (Phase 16.12) — `setTimeout`, `setInterval`, and the clear doors.
 *
 * ## A timer is a *due* record, never a callback that ran
 *
 * `JsTimerSet` records a due time and answers an id; it does not call anything. The callback runs
 * when the event loop (16.13) reaches a boundary whose clock has passed the due time, which is the
 * property the timer suite proves with `setTimeout(f, 0)` scheduled from a script: the script's later
 * statements still run first, because the callback is a task and the script is still running.
 *
 * ## The clock is the caller's
 *
 * In deterministic mode the clock is a counter that only `JsRuntimeAdvanceClock` moves, so a test
 * asserts "nothing fires at 999 ms, the interval fires at 1000 ms" without sleeping. Otherwise the
 * configuration's `clock` is used, and with no clock installed the runtime's own counter advances by
 * one per evaluation — deliberately *not* wall time, so no observable behavior in this build depends
 * on how long anything took.
 *
 * ## A repeating timer is one record, not a new one per firing
 *
 * An interval keeps its id across firings and its due time moves forward by its interval; a callback
 * that clears its own id during a firing is therefore honoured, which is the case a per-firing record
 * gets wrong. `maxTimersPerCycle` bounds one boundary's firings and `maxTimerCount` bounds the table;
 * both are counted refusals rather than overruns.
 *
 * ## Ids are runtime-local and never reused
 *
 * `JsTimerSet` answers 1, 2, 3, … from a monotonic counter that a reset rewinds. An id from one
 * runtime means nothing in another, which is why `JsTimerClear` on a foreign id answers
 * `RoboNotFound` rather than clearing somebody's timer through a shared table.
 */

#ifndef ROBO_JS_TIMER_H
#define ROBO_JS_TIMER_H

#include "RoboBase/RoboError.h"
#include "RoboJS/RoboJSTypes.h"
#include "RoboJS/RoboJSValue.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One timer, as a caller reads it back. */
typedef struct JsTimerInfo {
    int64_t id;
    uint64_t dueMs;      /* when it is next due, on the runtime's clock */
    uint64_t intervalMs; /* 0 for a one-shot */
    int repeating;
    int active;
    /** How many times its callback has run. */
    int32_t fired;
} JsTimerInfo;

/**
 * Schedule @p callback (a function value) to run after @p delayMs.
 *
 * @param args/argCount  extra arguments the callback receives after none; may be NULL/0.
 * @param repeating      0 for a one-shot, 1 for an interval.
 * @param outId          the runtime-local id; may be NULL.
 *
 * @return RoboOk; RoboInvalidArgument when @p callback is not callable; `RoboNotSupported` when the
 *         runtime has no timer table; `RoboOutOfMemory` when the table is at `maxTimerCount`.
 */
Result JsTimerSet(JsRuntime *runtime,
                  uint64_t delayMs,
                  const JsValue *callback,
                  const JsValue *args,
                  int32_t argCount,
                  int repeating,
                  int64_t *outId);

/** Cancel @p id. RoboNotFound when no timer has it (including a timer that already fired once). */
Result JsTimerClear(JsRuntime *runtime, int64_t id);

/** Number of live timers (one-shot and repeating). */
int32_t JsTimerCount(JsRuntime *runtime);

/** Number of live timers whose due time is at or before @p nowMs. */
int32_t JsTimerDueCount(JsRuntime *runtime, uint64_t nowMs);

/** Read @p id's record. RoboNotFound when no timer has it. */
Result JsTimerInfoOf(JsRuntime *runtime, int64_t id, JsTimerInfo *out);

/** Drop every timer (the reset path, and the window-destroyed path in 16.16). Answers the number
 *  cancelled, so a caller can assert that a destroyed runtime took its timers with it. */
int32_t JsTimerCancelAll(JsRuntime *runtime);

/**
 * Fire every timer due at or before @p nowMs, up to @p maxFirings, **as jobs posted to the event
 * loop** — this door exists for the loop to call, and it posts rather than calling the callback
 * itself so ordering stays the loop's business.
 *
 * @return the number of callbacks posted.
 */
int32_t JsTimerCollectDue(JsRuntime *runtime, uint64_t nowMs, int32_t maxFirings);

/** Install `setTimeout`, `clearTimeout`, `setInterval` and `clearInterval` as globals. Called by
 *  `JsRuntimeInstallGlobals` when the configuration enabled timers; idempotent. */
Result JsTimerInstallGlobals(JsRuntime *runtime);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_JS_TIMER_H */
