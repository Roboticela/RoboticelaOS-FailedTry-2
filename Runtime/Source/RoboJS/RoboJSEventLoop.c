/*
 * RoboJS's event loop (see `RoboJSEventLoop.h`).
 *
 * ## The boundary, stated once
 *
 * `JsLoopRunOne` **drains the microtask queue first, then runs at most one job.** The order is not an
 * accident: a microtask queued by a script belongs to the task that just finished, so the boundary
 * that follows it must drain before it starts anything new. `queueMicrotask(f); setTimeout(g, 0)` from
 * one script therefore prints `f` then `g` in two boundaries, and a chain of microtasks that queue
 * more microtasks finishes inside one.
 *
 * ## Producers post, the owner runs
 *
 * Nothing in this file calls a JavaScript function from a producer's stack: `JsLoopPostTask` appends
 * to a ring and the caller's own thread runs the boundary. That is the threading rule
 * (`Documentation/RoboJS/Threading.md`) expressed as a data structure rather than a promise.
 *
 * ## Refusals are counted, not hidden
 *
 * A full queue is `RoboOutOfMemory` on the post (the job is *not* dropped silently), a microtask chain
 * past the cap is a `RangeError` diagnostic plus a cut, and `JsLoopRunUntilIdle` that hits its cycle
 * budget answers `RoboTimeout` so a caller can tell "finished" from "gave up".
 */

#include "RoboJS/RoboJSEventLoop.h"

#include "RoboJS/RoboJSException.h"
#include "RoboJS/RoboJSFunction.h"
#include "RoboJS/RoboJSRuntime.h"
#include "RoboJS/RoboJSTimer.h"

#include "RoboJSInternal.h"

#include <string.h>

const char *JsTaskKindName(JsTaskKind kind)
{
    switch (kind) {
    case RoboJsTaskScript: return "script";
    case RoboJsTaskTimer: return "timer";
    case RoboJsTaskEvent: return "event";
    case RoboJsTaskNative: return "native";
    case RoboJsTaskMicrotask: return "microtask";
    default: return "unknown";
    }
}

int JsTaskKindIsKnown(JsTaskKind kind)
{
    return (int)kind >= 0 && (int)kind <= (int)RoboJsTaskMicrotask;
}

void JsJobClear(JsJob *job)
{
    if (job == NULL) {
        return;
    }
    memset(job, 0, sizeof(*job));
    job->callback = JsValueUndefined();
    job->thisValue = JsValueUndefined();
    job->args = NULL;
    job->argCount = 0;
    job->argCapacity = 0;
}

/* ------------------------------------------------------------------ */
/* Queueing                                                            */
/* ------------------------------------------------------------------ */

/** Append a job to a ring. NULL when the ring is at its capacity — the caller counts the refusal. */
/*
 * Entering and leaving the loop are a pair, and the pair is what makes the reentrancy refusal safe:
 * the flag is set for exactly the dynamic extent of one drain, so a listener that calls a loop door
 * sees it and a caller that calls one after the drain does not.
 */
static Result JsLoopEnter(JsRuntime *runtime)
{
    if (runtime->executing || runtime->inLoop) {
        return RoboInvalidArgument;
    }
    runtime->inLoop = 1;
    return RoboOk;
}

static void JsLoopLeave(JsRuntime *runtime)
{
    runtime->inLoop = 0;
}

static JsJob *JsQueuePush(JsJob *queue, int32_t capacity, int32_t *count, int32_t *head,
                          uint32_t kind)
{
    JsJob *job;

    if (*count >= capacity) {
        return NULL;
    }
    job = &queue[(*head + *count) % capacity];
    JsJobClear(job);
    job->kind = kind;
    *count += 1;
    return job;
}

static JsJob *JsQueuePeek(JsJob *queue, int32_t count, int32_t head)
{
    return count > 0 ? &queue[head] : NULL;
}

Result JsLoopPostTask(JsRuntime *runtime,
                      JsTaskKind kind,
                      const JsValue *callback,
                      const JsValue *thisValue,
                      const JsValue *args,
                      int32_t argCount)
{
    JsJob *job;

    if (runtime == NULL || callback == NULL) {
        return RoboInvalidArgument;
    }
    if (!runtime->config.enableEventLoop) {
        return RoboNotSupported;
    }
    job = JsQueuePush(runtime->jobs,
                      runtime->jobCapacity,
                      &runtime->jobCount,
                      &runtime->jobHead,
                      (uint32_t)kind);
    if (job == NULL) {
        JsRuntimeThrow(runtime, RoboJsErrorRange, NULL, "the job queue is full");
        return RoboOutOfMemory;
    }
    job->callback = *callback;
    job->thisValue = thisValue != NULL ? *thisValue : JsValueUndefined();
    if (argCount > 0) {
        if (JsJobSetArgs(runtime->heap, &job->args, &job->argCapacity, argCount, args) != RoboOk) {
            runtime->jobCount -= 1;
            return RoboOutOfMemory;
        }
        job->argCount = argCount;
    }
    job->dueMs = JsRuntimeNowMs(runtime);
    return RoboOk;
}

Result JsLoopPostTimerJob(JsRuntime *runtime, int64_t id)
{
    JsJob *job;

    if (runtime == NULL) {
        return RoboInvalidArgument;
    }
    job = JsQueuePush(runtime->jobs,
                      runtime->jobCapacity,
                      &runtime->jobCount,
                      &runtime->jobHead,
                      (uint32_t)RoboJsTaskTimer);
    if (job == NULL) {
        return RoboOutOfMemory;
    }
    job->record.target = 0;
    job->record.timestampMs = (uint64_t)id; /* the timer's id travels in the record's clock field */
    return RoboOk;
}

Result JsLoopPostEventRecord(JsRuntime *runtime, const JsEventRecord *record)
{
    JsJob *job;

    if (runtime == NULL || record == NULL) {
        return RoboInvalidArgument;
    }
    if (!runtime->config.enableEventLoop) {
        return RoboNotSupported;
    }
    job = JsQueuePush(runtime->jobs,
                      runtime->jobCapacity,
                      &runtime->jobCount,
                      &runtime->jobHead,
                      (uint32_t)RoboJsTaskEvent);
    if (job == NULL) {
        JsRuntimeThrow(runtime, RoboJsErrorRange, NULL, "the job queue is full");
        return RoboOutOfMemory;
    }
    job->record = *record;
    return RoboOk;
}

Result JsLoopPostMicrotaskValue(JsRuntime *runtime, const JsValue *callback)
{
    JsJob *job;

    if (runtime == NULL || callback == NULL) {
        return RoboInvalidArgument;
    }
    if (!runtime->config.enableEventLoop) {
        return RoboNotSupported;
    }
    job = JsQueuePush(runtime->microtasks,
                      runtime->microtaskCapacity,
                      &runtime->microtaskCount,
                      &runtime->microtaskHead,
                      (uint32_t)RoboJsTaskMicrotask);
    if (job == NULL) {
        return RoboOutOfMemory;
    }
    job->callback = *callback;
    return RoboOk;
}

Result JsMicrotaskQueue(JsRuntime *runtime, const JsValue *callback)
{
    Result status;

    if (runtime == NULL || callback == NULL) {
        return RoboInvalidArgument;
    }
    if (!JsValueIsFunction(callback)) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "queueMicrotask needs a function");
        return RoboError;
    }
    status = JsLoopPostMicrotaskValue(runtime, callback);
    if (status != RoboOk) {
        JsRuntimeThrow(runtime, RoboJsErrorRange, NULL, "the microtask queue is full");
    }
    return status;
}

static Result JsMicrotaskQueueNative(JsRuntime *runtime,
                                     JsObject *thisObject,
                                     const JsValue *args,
                                     int32_t argCount,
                                     JsValue *out,
                                     void *userdata)
{
    (void)thisObject;
    (void)userdata;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    *out = JsValueUndefined();
    if (argCount < 1) {
        return RoboOk;
    }
    return JsMicrotaskQueue(runtime, &args[0]);
}

Result JsMicrotaskInstallGlobals(JsRuntime *runtime)
{
    if (runtime == NULL) {
        return RoboInvalidArgument;
    }
    return JsRuntimeDefineGlobalNative(runtime, "queueMicrotask", 1, JsMicrotaskQueueNative, NULL);
}

/* ------------------------------------------------------------------ */
/* Running                                                             */
/* ------------------------------------------------------------------ */

Result JsLoopDrainMicrotasks(JsRuntime *runtime, int *outRan)
{
    int32_t limit;
    int32_t drained = 0;

    if (runtime == NULL) {
        return RoboInvalidArgument;
    }
    if (outRan != NULL) {
        *outRan = 0;
    }
    limit = runtime->config.maxMicrotasksPerCycle > 0 ? runtime->config.maxMicrotasksPerCycle
                                                      : (int32_t)RoboJsDefaultMaxMicrotasks;
    while (runtime->microtaskCount > 0) {
        JsJob *job = JsQueuePeek(runtime->microtasks,
                                 runtime->microtaskCount,
                                 runtime->microtaskHead);
        JsValue callback;
        JsValue ignoredValue = JsValueUndefined();
        Result status;

        if (job == NULL) {
            break;
        }
        callback = job->callback;
        runtime->microtaskHead = (runtime->microtaskHead + 1) % runtime->microtaskCapacity;
        runtime->microtaskCount -= 1;
        runtime->microtasksRun += 1;
        drained += 1;
        if (outRan != NULL) {
            *outRan = 1;
        }
        status = JsInterpCallValue(runtime, &callback, NULL, NULL, 0, &ignoredValue);
        if (status != RoboOk) {
            /* A throwing microtask is reported and the drain stops: the remaining microtasks stay
             * queued, which is the honest answer — they were queued by work that did not finish. */
            return status;
        }
        if (drained >= limit) {
            runtime->microtaskOverflows += 1;
            JsRuntimeThrow(runtime,
                           RoboJsErrorLimit,
                           NULL,
                           "more than %d microtasks were queued in one boundary",
                           limit);
            return RoboError;
        }
    }
    return RoboOk;
}

Result JsLoopRunJob(JsRuntime *runtime, JsJob *job, int *outRan)
{
    JsValue ignored = JsValueUndefined();
    Result status;

    if (runtime == NULL || job == NULL) {
        return RoboInvalidArgument;
    }
    if (outRan != NULL) {
        *outRan = 1;
    }
    switch ((JsTaskKind)job->kind) {
    case RoboJsTaskTimer: {
        int64_t id = (int64_t)job->record.timestampMs;
        JsTimerInfo info;
        JsValue callback;
        JsValue argsBuffer[8];
        int32_t argCount;
        int32_t i;

        if (JsTimerInfoOf(runtime, id, &info) != RoboOk || !info.active) {
            /* Cleared between the post and the run: nothing to do, and that is not a failure. */
            return RoboOk;
        }
        {
            int32_t slot;
            JsTimerSlot *found = NULL;

            for (slot = 0; slot < runtime->timerCapacity; ++slot) {
                if (runtime->timers[slot].active && runtime->timers[slot].id == id) {
                    found = &runtime->timers[slot];
                    break;
                }
            }
            if (found == NULL) {
                return RoboOk;
            }
            callback = found->callback;
            argCount = found->argCount > 8 ? 8 : found->argCount;
            for (i = 0; i < argCount; ++i) {
                argsBuffer[i] = found->args[i];
            }
            found->fired += 1;
            if (found->repeating) {
                found->dueMs += found->intervalMs > 0 ? found->intervalMs : 1u;
            } else {
                found->active = 0;
                found->args = NULL;
                found->argCount = 0;
                if (runtime->timerCount > 0) {
                    runtime->timerCount -= 1;
                }
            }
        }
        status = JsInterpCallValue(runtime, &callback, NULL, argsBuffer, argCount, &ignored);
        return status;
    }
    case RoboJsTaskEvent:
        return JsEventDispatchNow(runtime, &job->record, NULL);
    case RoboJsTaskScript:
    case RoboJsTaskNative:
    case RoboJsTaskMicrotask:
    default:
        if (!JsValueIsFunction(&job->callback)) {
            return RoboOk;
        }
        return JsInterpCallValue(runtime,
                                 &job->callback,
                                 JsValueIsObject(&job->thisValue) ? job->thisValue.object : NULL,
                                 job->args,
                                 job->argCount,
                                 &ignored);
    }
}

static Result JsLoopRunOneDrain(JsRuntime *runtime, int *outRan)
{
    int microtasksRan = 0;
    Result status;

    if (runtime->stopRequested) {
        return RoboTimeout;
    }
    /* The microtask drain belongs to the task that just ended; it goes first. */
    status = JsLoopDrainMicrotasks(runtime, &microtasksRan);
    if (status != RoboOk) {
        if (outRan != NULL) {
            *outRan = 1;
        }
        return status;
    }
    if (runtime->jobCount == 0) {
        int32_t posted = JsTimerCollectDue(runtime,
                                           JsRuntimeNowMs(runtime),
                                           runtime->config.maxTimersPerCycle > 0
                                               ? runtime->config.maxTimersPerCycle
                                               : (int32_t)RoboJsDefaultMaxTimersPerCycle);

        if (posted == 0) {
            return RoboOk;
        }
    }
    {
        JsJob job = *JsQueuePeek(runtime->jobs, runtime->jobCount, runtime->jobHead);

        runtime->jobHead = (runtime->jobHead + 1) % runtime->jobCapacity;
        runtime->jobCount -= 1;
        runtime->tasksRun += 1;
        status = JsLoopRunJob(runtime, &job, outRan);
        if (status != RoboOk) {
            return status;
        }
    }
    /* A job's own microtasks run at the next boundary, which is what keeps one boundary one job. */
    if (outRan != NULL) {
        *outRan = 1;
    }
    return RoboOk;
}

Result JsLoopRunOne(JsRuntime *runtime, int *outRan)
{
    Result status;

    if (runtime == NULL) {
        return RoboInvalidArgument;
    }
    if (outRan != NULL) {
        *outRan = 0;
    }
    if (!runtime->config.enableEventLoop) {
        return RoboNotSupported;
    }
    /* **The loop is not reentrant**, and this is the door that says so: a listener that calls
     * `JsLoopRunOne` -- or a host callback that calls it while the runtime is mid-evaluation -- gets
     * `RoboInvalidArgument` rather than a task run inside a task. Every ordering statement in
     * `EventLoop.md` and `Microtasks.md` depends on it; a task that wants more work posts a task or a
     * microtask (`Threading.md` §2). */
    if (JsLoopEnter(runtime) != RoboOk) {
        return RoboInvalidArgument;
    }
    status = JsLoopRunOneDrain(runtime, outRan);
    JsLoopLeave(runtime);
    return status;
}

Result JsLoopRunUntilIdle(JsRuntime *runtime, int32_t maxCycles)
{
    int32_t limit;
    int32_t cycles = 0;
    Result outcome = RoboOk;

    if (runtime == NULL) {
        return RoboInvalidArgument;
    }
    if (!runtime->config.enableEventLoop) {
        return RoboNotSupported;
    }
    if (JsLoopEnter(runtime) != RoboOk) {
        return RoboInvalidArgument;
    }
    limit = maxCycles > 0 ? maxCycles
                          : (runtime->config.maxTasksPerCycle > 0
                                 ? runtime->config.maxTasksPerCycle
                                 : (int32_t)RoboJsDefaultMaxTasksPerCycle);
    while (JsLoopHasPendingWork(runtime)) {
        int ran = 0;
        Result status;

        if (cycles >= limit) {
            JsRuntimeThrow(runtime, RoboJsErrorLimit, NULL, "the event loop ran %d boundaries without draining", limit);
            outcome = RoboTimeout;
            break;
        }
        status = JsLoopRunOneDrain(runtime, &ran);
        cycles += 1;
        if (status == RoboTimeout) {
            outcome = status;
            break;
        }
        if (status != RoboOk) {
            outcome = status;
            break;
        }
        if (!ran && runtime->jobCount == 0 && runtime->microtaskCount == 0) {
            break;
        }
    }
    JsLoopLeave(runtime);
    return outcome;
}

int JsLoopHasPendingWork(JsRuntime *runtime)
{
    if (runtime == NULL) {
        return 0;
    }
    if (runtime->jobCount > 0 || runtime->microtaskCount > 0) {
        return 1;
    }
    return JsTimerCount(runtime) > 0;
}

void JsLoopRequestStop(JsRuntime *runtime, int stop)
{
    if (runtime == NULL) {
        return;
    }
    runtime->stopRequested = stop ? 1 : 0;
}

int32_t JsLoopPendingTaskCount(JsRuntime *runtime)
{
    return runtime != NULL ? runtime->jobCount : 0;
}

int32_t JsLoopTasksRun(JsRuntime *runtime)
{
    return runtime != NULL ? runtime->tasksRun : 0;
}

int32_t JsLoopMicrotasksRun(JsRuntime *runtime)
{
    return runtime != NULL ? runtime->microtasksRun : 0;
}

int32_t JsLoopPendingMicrotaskCount(JsRuntime *runtime)
{
    return runtime != NULL ? runtime->microtaskCount : 0;
}

int32_t JsLoopMicrotaskOverflows(JsRuntime *runtime)
{
    return runtime != NULL ? runtime->microtaskOverflows : 0;
}

int32_t JsLoopClear(JsRuntime *runtime)
{
    int32_t dropped;

    if (runtime == NULL) {
        return 0;
    }
    dropped = runtime->jobCount + runtime->microtaskCount;
    runtime->jobCount = 0;
    runtime->jobHead = 0;
    runtime->microtaskCount = 0;
    runtime->microtaskHead = 0;
    runtime->stopRequested = 0;
    return dropped;
}

Result JsLoopPump(JsRuntime *runtime, int *outRan)
{
    int32_t posted;

    if (runtime == NULL) {
        return RoboInvalidArgument;
    }
    posted = JsTimerCollectDue(runtime,
                               JsRuntimeNowMs(runtime),
                               runtime->config.maxTimersPerCycle > 0
                                   ? runtime->config.maxTimersPerCycle
                                   : (int32_t)RoboJsDefaultMaxTimersPerCycle);
    (void)posted;
    return JsLoopRunOne(runtime, outRan);
}
