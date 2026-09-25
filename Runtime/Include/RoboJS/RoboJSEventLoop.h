/*
 * RoboJS event loop, tasks and microtasks (16.13, 16.14) — where a callback runs.
 *
 * ## One loop, one task at a time, and microtasks between
 *
 * `JsLoopRunOne` performs one boundary, and the order *inside* it is the whole contract: it **drains
 * the microtask queue to empty** first — those microtasks belong to the task that has just ended —
 * and only then runs **at most one** job. A job is the oldest queued task, or a due timer that has
 * been collected into the queue; a due timer is collected only when the queue is empty, so a queued
 * task always runs before a timer that became due while it was waiting. A microtask queued by another
 * microtask runs in that same drain, and a job's *own* microtasks run at the next boundary, which is
 * what keeps one boundary one job. That ordering is what makes the ordering test in 16.14 —
 * `queueMicrotask(f); setTimeout(g, 0)` from one script prints the microtask first — a fact about the
 * loop rather than a race.
 *
 * The drain is capped at `maxMicrotasksPerCycle`. A chain that re-queues itself forever is cut at the
 * cap with a `RangeError` diagnostic and the loop is left usable; "runs forever" is not an option for
 * a loop that shares a thread with an operating system.
 *
 * ## Nothing calls a JavaScript function from a native callback
 *
 * A device thread, an input event or a timer does not call the callback: it **posts** a job
 * (`JsLoopPostTask`) and the thread that owns the runtime runs it. That is the rule
 * `Documentation/RoboJS/Threading.md` states, and it is why the queue is the only thing a producer
 * touches.
 *
 * ## Task kinds exist to be readable, not to be dispatched differently
 *
 * A script, a timer callback, a DOM event and a native callback are all "call this function with
 * these arguments". The kind is kept so a dump can say what is pending and so `maxTimersPerCycle`
 * has something to count; it does not change how a job runs, and a caller cannot invent a kind that
 * runs differently.
 */

#ifndef ROBO_JS_EVENT_LOOP_H
#define ROBO_JS_EVENT_LOOP_H

#include "RoboJS/RoboJSTypes.h"
#include "RoboJS/RoboJSValue.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Job kinds. */
typedef enum JsTaskKind {
    RoboJsTaskScript = 0,  /* a script evaluation a caller asked for from inside the loop */
    RoboJsTaskTimer = 1,   /* a `setTimeout`/`setInterval` callback */
    RoboJsTaskEvent = 2,   /* a DOM/input event dispatch (16.11) */
    RoboJsTaskNative = 3,  /* anything a binding posted */
    RoboJsTaskMicrotask = 4
} JsTaskKind;

const char *JsTaskKindName(JsTaskKind kind);
int JsTaskKindIsKnown(JsTaskKind kind);

/**
 * Post a job: @p callback runs at the next boundary, with @p thisValue as `this` and @p args.
 *
 * @return RoboOk; `RoboNotSupported` when the runtime has no loop; `RoboOutOfMemory` when the queue
 *         is at `maxQueueLength` (a counted refusal — the job is not silently dropped, and the
 *         caller can decide to retry after a boundary).
 */
Result JsLoopPostTask(JsRuntime *runtime,
                      JsTaskKind kind,
                      const JsValue *callback,
                      const JsValue *thisValue,
                      const JsValue *args,
                      int32_t argCount);

/** `queueMicrotask(f)`'s door: the job runs after the current one, before the next. */
Result JsMicrotaskQueue(JsRuntime *runtime, const JsValue *callback);

/** Install `queueMicrotask` as a global. Called by `JsRuntimeInstallGlobals`; idempotent. */
Result JsMicrotaskInstallGlobals(JsRuntime *runtime);

/**
 * Run one boundary: the next ready job, then the microtask drain.
 *
 * @param outRan  set to 1 when a job ran, 0 when there was nothing ready (the loop is idle).
 *
 * @return RoboOk in both cases; `RoboNotSupported` when the runtime has no loop; `RoboError` when the
 *         job threw (the runtime's pending exception holds the value) — the loop itself remains
 *         usable, which is what "an uncaught error does not stop the desktop" means.
 */
Result JsLoopRunOne(JsRuntime *runtime, int *outRan);

/** Drain the microtask queue to empty, bounded by `maxMicrotasksPerCycle`. */
Result JsLoopDrainMicrotasks(JsRuntime *runtime, int *outRan);

/**
 * Run boundaries until nothing is pending (or @p maxCycles of them have run; 0 → the configured
 * `maxTasksPerCycle`). Answers `RoboTimeout` when the cap was reached with work still pending, so a
 * caller can tell "finished" from "gave up".
 */
Result JsLoopRunUntilIdle(JsRuntime *runtime, int32_t maxCycles);

/** 1 when there is a job or a microtask waiting. */
int JsLoopHasPendingWork(JsRuntime *runtime);

/**
 * Ask the loop to stop after the current boundary. @p stop 0 clears the request. A stopped loop
 * refuses a new boundary with `RoboTimeout` until it is cleared — a caller's "the window is closing"
 * without a flag of its own.
 */
void JsLoopRequestStop(JsRuntime *runtime, int stop);

/** Jobs waiting (excluding microtasks). */
int32_t JsLoopPendingTaskCount(JsRuntime *runtime);
/** Jobs run since creation or the last reset. */
int32_t JsLoopTasksRun(JsRuntime *runtime);
/** Microtasks drained since creation or the last reset. */
int32_t JsLoopMicrotasksRun(JsRuntime *runtime);
/** Microtasks waiting. */
int32_t JsLoopPendingMicrotaskCount(JsRuntime *runtime);
/** Times the microtask cap was reached (each one is a diagnostic and a cut chain). */
int32_t JsLoopMicrotaskOverflows(JsRuntime *runtime);

/** Empty both queues (the reset path, and the window-destroyed path in 16.16). Answers how many
 *  jobs were dropped, so a caller can assert nothing survives a destroy. */
int32_t JsLoopClear(JsRuntime *runtime);

/** Collect due timers into the queue and run one boundary — the door a host frame calls once per
 *  frame so a timer fires without a caller-managed pump. Equivalent to
 *  `JsTimerCollectDue` followed by `JsLoopRunOne`. */
Result JsLoopPump(JsRuntime *runtime, int *outRan);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_JS_EVENT_LOOP_H */
