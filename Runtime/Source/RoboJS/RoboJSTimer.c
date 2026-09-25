/*
 * RoboJS timers (see `RoboJSTimer.h`).
 *
 * A timer is a *record*, never a call: `JsTimerSet` fills a slot and answers an id, and the loop
 * (`RoboJSEventLoop.c`) is what turns a due record into a job. The table is a flat array with a
 * monotonic id, so an id never changes meaning across a firing — which is what makes a callback that
 * clears its own interval work.
 */

#include "RoboJS/RoboJSTimer.h"

#include "RoboJS/RoboJSException.h"
#include "RoboJS/RoboJSFunction.h"
#include "RoboJS/RoboJSRuntime.h"

#include "RoboJSInternal.h"

#include <string.h>

static JsTimerSlot *JsTimerFind(JsRuntime *runtime, int64_t id)
{
    int32_t i;

    if (runtime == NULL || runtime->timers == NULL) {
        return NULL;
    }
    for (i = 0; i < runtime->timerCapacity; ++i) {
        if (runtime->timers[i].active && runtime->timers[i].id == id) {
            return &runtime->timers[i];
        }
    }
    return NULL;
}

static JsTimerSlot *JsTimerAllocate(JsRuntime *runtime)
{
    int32_t i;

    for (i = 0; i < runtime->timerCapacity; ++i) {
        if (!runtime->timers[i].active) {
            return &runtime->timers[i];
        }
    }
    return NULL;
}

Result JsTimerSet(JsRuntime *runtime,
                  uint64_t delayMs,
                  const JsValue *callback,
                  const JsValue *args,
                  int32_t argCount,
                  int repeating,
                  int64_t *outId)
{
    JsTimerSlot *slot;

    if (runtime == NULL || callback == NULL) {
        return RoboInvalidArgument;
    }
    if (!JsRuntimeIsInitialized(runtime) || runtime->timers == NULL) {
        return RoboNotSupported;
    }
    if (!JsValueIsFunction(callback)) {
        /* A host passing a non-callable is a host-contract violation, which this header answers with
         * `RoboInvalidArgument` and no exception: nothing in a script threw. The script's own door,
         * `setTimeout`, does its own check and throws a `TypeError` before reaching here. */
        return RoboInvalidArgument;
    }
    slot = JsTimerAllocate(runtime);
    if (slot == NULL) {
        JsRuntimeThrow(runtime,
                       RoboJsErrorRange,
                       NULL,
                       "this runtime holds at most %d timers",
                       runtime->timerCapacity);
        return RoboError;
    }
    memset(slot, 0, sizeof(*slot));
    slot->active = 1;
    slot->repeating = repeating ? 1 : 0;
    slot->id = runtime->nextTimerId;
    runtime->nextTimerId += 1;
    slot->dueMs = JsRuntimeNowMs(runtime) + delayMs;
    slot->intervalMs = delayMs;
    slot->callback = *callback;
    slot->argCount = 0;
    if (argCount > 0) {
        if (JsJobSetArgs(runtime->heap, &slot->args, &slot->argCapacity, argCount, args) != RoboOk) {
            slot->active = 0;
            return RoboOutOfMemory;
        }
        slot->argCount = argCount;
    }
    runtime->timerCount += 1;
    if (outId != NULL) {
        *outId = slot->id;
    }
    return RoboOk;
}

Result JsTimerClear(JsRuntime *runtime, int64_t id)
{
    JsTimerSlot *slot = JsTimerFind(runtime, id);

    if (runtime == NULL) {
        return RoboInvalidArgument;
    }
    if (slot == NULL) {
        return RoboNotFound;
    }
    slot->active = 0;
    slot->args = NULL;
    slot->argCount = 0;
    slot->argCapacity = 0;
    if (runtime->timerCount > 0) {
        runtime->timerCount -= 1;
    }
    return RoboOk;
}

int32_t JsTimerCount(JsRuntime *runtime)
{
    return runtime != NULL ? runtime->timerCount : 0;
}

int32_t JsTimerDueCount(JsRuntime *runtime, uint64_t nowMs)
{
    int32_t i;
    int32_t count = 0;

    if (runtime == NULL || runtime->timers == NULL) {
        return 0;
    }
    for (i = 0; i < runtime->timerCapacity; ++i) {
        if (runtime->timers[i].active && runtime->timers[i].dueMs <= nowMs) {
            count += 1;
        }
    }
    return count;
}

Result JsTimerInfoOf(JsRuntime *runtime, int64_t id, JsTimerInfo *out)
{
    JsTimerSlot *slot = JsTimerFind(runtime, id);

    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (slot == NULL) {
        return RoboNotFound;
    }
    out->id = slot->id;
    out->dueMs = slot->dueMs;
    out->intervalMs = slot->intervalMs;
    out->repeating = slot->repeating;
    out->active = slot->active;
    out->fired = slot->fired;
    return RoboOk;
}

int32_t JsTimerCancelAll(JsRuntime *runtime)
{
    int32_t i;
    int32_t cancelled = 0;

    if (runtime == NULL || runtime->timers == NULL) {
        return 0;
    }
    for (i = 0; i < runtime->timerCapacity; ++i) {
        if (runtime->timers[i].active) {
            runtime->timers[i].active = 0;
            runtime->timers[i].args = NULL;
            runtime->timers[i].argCount = 0;
            runtime->timers[i].argCapacity = 0;
            cancelled += 1;
        }
    }
    runtime->timerCount = 0;
    return cancelled;
}

int32_t JsTimerCollectDue(JsRuntime *runtime, uint64_t nowMs, int32_t maxFirings)
{
    int32_t i;
    int32_t posted = 0;

    if (runtime == NULL || runtime->timers == NULL) {
        return 0;
    }
    for (i = 0; i < runtime->timerCapacity && posted < maxFirings; ++i) {
        JsTimerSlot *slot = &runtime->timers[i];

        if (slot->active && slot->dueMs <= nowMs) {
            if (JsLoopPostTimerJob(runtime, slot->id) != RoboOk) {
                break;
            }
            posted += 1;
        }
    }
    return posted;
}

/* ------------------------------------------------------------------ */
/* The three globals                                                   */
/* ------------------------------------------------------------------ */

static Result JsTimerSetNative(JsRuntime *runtime,
                               JsObject *thisObject,
                               const JsValue *args,
                               int32_t argCount,
                               JsValue *out,
                               void *userdata)
{
    uint64_t delay = 0;
    int64_t id = 0;
    Result status;

    (void)thisObject;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (argCount < 1 || !JsValueIsFunction(&args[0])) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "setTimeout needs a function");
        return RoboError;
    }
    if (argCount >= 2) {
        double requested = JsValueToNumber(&args[1]);

        if (requested == requested && requested > 0) {
            delay = requested > 1.0e9 ? (uint64_t)1.0e9 : (uint64_t)requested;
        }
    }
    status = JsTimerSet(runtime,
                        delay,
                        &args[0],
                        argCount > 2 ? args + 2 : NULL,
                        argCount > 2 ? argCount - 2 : 0,
                        (int)(intptr_t)userdata,
                        &id);
    if (status != RoboOk) {
        return status;
    }
    *out = JsValueNumber((double)id);
    return RoboOk;
}

static Result JsTimerClearNative(JsRuntime *runtime,
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
    {
        int64_t id = (int64_t)JsValueToNumber(&args[0]);

        /* Clearing an unknown id is a successful no-op in the language, and it is the answer a
         * callback that cancels a timer which already fired depends on. */
        JsTimerClear(runtime, id);
    }
    return RoboOk;
}

Result JsTimerInstallGlobals(JsRuntime *runtime)
{
    if (runtime == NULL) {
        return RoboInvalidArgument;
    }
    if (JsRuntimeDefineGlobalNative(runtime, "setTimeout", 2, JsTimerSetNative, NULL) != RoboOk ||
        JsRuntimeDefineGlobalNative(runtime, "setInterval", 2, JsTimerSetNative,
                                    (void *)(intptr_t)1) != RoboOk ||
        JsRuntimeDefineGlobalNative(runtime, "clearTimeout", 1, JsTimerClearNative, NULL) != RoboOk ||
        JsRuntimeDefineGlobalNative(runtime, "clearInterval", 1, JsTimerClearNative, NULL) != RoboOk) {
        return RoboOutOfMemory;
    }
    return RoboOk;
}
