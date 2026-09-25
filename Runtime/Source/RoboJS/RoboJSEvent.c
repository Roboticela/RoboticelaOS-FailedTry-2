/*
 * RoboJS's events (see `RoboJSEvent.h`).
 *
 * ## The event object is created per dispatch, and the flags live beside it
 *
 * `event.target` must stay the node the event started at while a listener runs for an ancestor, so
 * the object is created once, its `currentTarget` is rewritten per node, and `preventDefault()`'s
 * answer is recorded in a small heap-allocated record the object points at
 * (`JsObject.hostData`). A nested dispatch allocates its own record, so an outer event's
 * `defaultPrevented` cannot be flipped by an inner one — which is the bug the shared-flag version of
 * this file would have had.
 *
 * ## Listener order is array order
 *
 * The runtime's listener table is scanned linearly in registration order, so "the listener added
 * first runs first" is a fact about the table rather than about a hash. `removeEventListener` removes
 * the first entry with that exact function *value*, which is what makes a duplicate registration
 * removable.
 *
 * ## What dispatch does not do
 *
 * No capture phase, no `stopImmediatePropagation`, no `bubbles`/`cancelable`, no listener options
 * (`once`, `passive`). `stopPropagation` stops the walk; a listener that throws ends the dispatch and
 * leaves the pending exception, because swallowing it would hide a script error behind a click.
 */

#include "RoboJS/RoboJSEvent.h"

#include "RoboJS/RoboJSException.h"
#include "RoboJS/RoboJSFunction.h"
#include "RoboJS/RoboJSObject.h"
#include "RoboJS/RoboJSRuntime.h"

#include "RoboJSInternal.h"

#include <stdio.h>
#include <string.h>

/** The dispatch flags an event object points at (see the header comment). */
typedef struct JsEventFlags {
    int defaultPrevented;
    int propagationStopped;
    int propagationImmediate;
    JsDomNodeId target;
} JsEventFlags;

Result JsEventRecordInit(JsEventRecord *record, const char *type, JsDomNodeId target)
{
    if (record == NULL || type == NULL) {
        return RoboInvalidArgument;
    }
    memset(record, 0, sizeof(*record));
    snprintf(record->type, sizeof(record->type), "%s", type);
    record->target = target;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Listener storage                                                    */
/* ------------------------------------------------------------------ */

static int JsEventTypeEquals(const char *a, const char *b)
{
    return a != NULL && b != NULL && strcmp(a, b) == 0;
}

static Result JsEventGrowListeners(JsRuntime *runtime)
{
    int32_t capacity = runtime->listenerCapacity > 0 ? runtime->listenerCapacity * 2 : 256;
    JsListener *fresh = (JsListener *)RoboCalloc((size_t)capacity, sizeof(JsListener));

    if (fresh == NULL) {
        return RoboOutOfMemory;
    }
    if (runtime->listeners != NULL) {
        memcpy(fresh, runtime->listeners, (size_t)runtime->listenerCount * sizeof(JsListener));
        RoboFree(runtime->listeners);
    }
    runtime->listeners = fresh;
    runtime->listenerCapacity = capacity;
    return RoboOk;
}

Result JsEventAddListener(JsRuntime *runtime,
                          JsDomNodeId target,
                          const char *type,
                          const JsValue *listener)
{
    JsListener *slot;

    if (runtime == NULL || type == NULL || listener == NULL || target == 0) {
        return RoboInvalidArgument;
    }
    if (!JsValueIsFunction(listener)) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "a listener must be a function");
        return RoboError;
    }
    if (runtime->listenerCount >= runtime->listenerCapacity) {
        if (JsEventGrowListeners(runtime) != RoboOk) {
            JsRuntimeThrow(runtime, RoboJsErrorRange, NULL, "this runtime is out of listener slots");
            return RoboOutOfMemory;
        }
    }
    slot = &runtime->listeners[runtime->listenerCount];
    memset(slot, 0, sizeof(*slot));
    slot->target = target;
    snprintf(slot->type, sizeof(slot->type), "%s", type);
    slot->callback = *listener;
    slot->order = runtime->nextListenerOrder;
    runtime->nextListenerOrder += 1;
    runtime->listenerCount += 1;
    return RoboOk;
}

Result JsEventRemoveListener(JsRuntime *runtime,
                             JsDomNodeId target,
                             const char *type,
                             const JsValue *listener)
{
    int32_t i;

    if (runtime == NULL || type == NULL || listener == NULL) {
        return RoboInvalidArgument;
    }
    for (i = 0; i < runtime->listenerCount; ++i) {
        JsListener *slot = &runtime->listeners[i];

        if (slot->target == target && JsEventTypeEquals(slot->type, type) &&
            JsValueStrictEquals(&slot->callback, listener)) {
            int32_t move;

            for (move = i; move + 1 < runtime->listenerCount; ++move) {
                runtime->listeners[move] = runtime->listeners[move + 1];
            }
            runtime->listenerCount -= 1;
            return RoboOk;
        }
    }
    return RoboNotFound;
}

int32_t JsEventListenerCount(JsRuntime *runtime, JsDomNodeId target)
{
    int32_t i;
    int32_t count = 0;

    if (runtime == NULL) {
        return 0;
    }
    for (i = 0; i < runtime->listenerCount; ++i) {
        if (runtime->listeners[i].target == target) {
            count += 1;
        }
    }
    return count;
}

int32_t JsEventTotalListenerCount(JsRuntime *runtime)
{
    return runtime != NULL ? runtime->listenerCount : 0;
}

int32_t JsEventDropListeners(JsRuntime *runtime, JsDomNodeId target)
{
    int32_t dropped = 0;
    int32_t i = 0;

    if (runtime == NULL) {
        return 0;
    }
    while (i < runtime->listenerCount) {
        if (runtime->listeners[i].target == target) {
            int32_t move;

            for (move = i; move + 1 < runtime->listenerCount; ++move) {
                runtime->listeners[move] = runtime->listeners[move + 1];
            }
            runtime->listenerCount -= 1;
            dropped += 1;
            continue;
        }
        i += 1;
    }
    return dropped;
}

int32_t JsEventDropAll(JsRuntime *runtime)
{
    int32_t dropped;

    if (runtime == NULL) {
        return 0;
    }
    dropped = runtime->listenerCount;
    runtime->listenerCount = 0;
    runtime->nextListenerOrder = 0;
    return dropped;
}

/* ------------------------------------------------------------------ */
/* The event object                                                    */
/* ------------------------------------------------------------------ */

static JsEventFlags *JsEventFlagsOf(JsObject *event)
{
    if (event == NULL || event->hostData == NULL) {
        return NULL;
    }
    return (JsEventFlags *)event->hostData;
}

static Result JsEventPreventDefaultNative(JsRuntime *runtime,
                                          JsObject *thisObject,
                                          const JsValue *args,
                                          int32_t argCount,
                                          JsValue *out,
                                          void *userdata)
{
    JsEventFlags *flags = JsEventFlagsOf(thisObject);

    (void)runtime;
    (void)args;
    (void)argCount;
    (void)userdata;
    if (flags != NULL) {
        flags->defaultPrevented = 1;
    }
    if (out != NULL) {
        *out = JsValueUndefined();
    }
    return RoboOk;
}

static Result JsEventStopPropagationNative(JsRuntime *runtime,
                                           JsObject *thisObject,
                                           const JsValue *args,
                                           int32_t argCount,
                                           JsValue *out,
                                           void *userdata)
{
    JsEventFlags *flags = JsEventFlagsOf(thisObject);

    (void)runtime;
    (void)args;
    (void)argCount;
    (void)userdata;
    if (flags != NULL) {
        flags->propagationStopped = 1;
    }
    if (out != NULL) {
        *out = JsValueUndefined();
    }
    return RoboOk;
}

Result JsEventInstallPrototype(JsRuntime *runtime)
{
    if (runtime == NULL) {
        return RoboInvalidArgument;
    }
    if (runtime->eventPrototype != NULL) {
        return RoboOk;
    }
    runtime->eventPrototype = JsObjectCreate(runtime->heap, runtime->objectPrototype);
    if (runtime->eventPrototype == NULL) {
        return RoboOutOfMemory;
    }
    if (JsObjectDefineNative(runtime,
                             runtime->eventPrototype,
                             "preventDefault",
                             0,
                             JsEventPreventDefaultNative,
                             NULL) != RoboOk ||
        JsObjectDefineNative(runtime,
                             runtime->eventPrototype,
                             "stopPropagation",
                             0,
                             JsEventStopPropagationNative,
                             NULL) != RoboOk) {
        return RoboOutOfMemory;
    }
    return RoboOk;
}

Result JsEventInstall(JsRuntime *runtime)
{
    return JsEventInstallPrototype(runtime);
}

static JsObject *JsEventCreateObject(JsRuntime *runtime,
                                     const JsEventRecord *record,
                                     JsEventFlags *flags)
{
    JsObject *event = JsObjectCreateHost(runtime->heap,
                                         runtime->eventPrototype,
                                         1u,
                                         (uint64_t)record->target,
                                         flags);

    if (event == NULL) {
        return NULL;
    }
    if (JsObjectSetString(runtime->heap, event, "type", record->type) != RoboOk ||
        JsObjectSetNumber(runtime->heap, event, "timestamp", (double)record->timestampMs) != RoboOk ||
        JsObjectSetBool(runtime->heap, event, "defaultPrevented", 0) != RoboOk ||
        JsObjectSetNumber(runtime->heap, event, "clientX", record->clientX) != RoboOk ||
        JsObjectSetNumber(runtime->heap, event, "clientY", record->clientY) != RoboOk ||
        JsObjectSetNumber(runtime->heap, event, "button", (double)record->button) != RoboOk ||
        JsObjectSetNumber(runtime->heap, event, "modifiers", (double)record->modifiers) != RoboOk ||
        JsObjectSetBool(runtime->heap, event, "repeat", record->repeat) != RoboOk) {
        return NULL;
    }
    if (record->key[0] != '\0') {
        JsObjectSetString(runtime->heap, event, "key", record->key);
    }
    if (record->data[0] != '\0') {
        JsObjectSetString(runtime->heap, event, "data", record->data);
    }
    {
        JsValue target = JsValueObject(JsDomWrapperForNode(runtime, record->target));

        JsObjectSet(runtime->heap, event, "target", &target);
    }
    return event;
}

/* ------------------------------------------------------------------ */
/* Dispatch                                                            */
/* ------------------------------------------------------------------ */

Result JsEventDispatchNow(JsRuntime *runtime,
                          const JsEventRecord *record,
                          JsEventResult *outResult)
{
    JsEventFlags *flags;
    JsObject *event;
    JsDomNodeId path[32];
    int32_t pathLength = 0;
    int32_t i;
    JsDomNodeId cursor;
    Result status = RoboOk;

    if (outResult != NULL) {
        memset(outResult, 0, sizeof(*outResult));
    }
    if (runtime == NULL || record == NULL) {
        return RoboInvalidArgument;
    }
    if (!runtime->domInstalled || runtime->domHost.parent == NULL) {
        return RoboNotSupported;
    }
    flags = (JsEventFlags *)JsHeapAlloc(runtime->heap, sizeof(JsEventFlags));
    if (flags == NULL) {
        return RoboOutOfMemory;
    }
    memset(flags, 0, sizeof(*flags));
    flags->target = record->target;
    event = JsEventCreateObject(runtime, record, flags);
    if (event == NULL) {
        return RoboOutOfMemory;
    }
    cursor = record->target;
    while (cursor != 0 && pathLength < 32) {
        JsDomNodeId parent;

        path[pathLength] = cursor;
        pathLength += 1;
        parent = runtime->domHost.parent(runtime->domHost.userdata, cursor);
        if (parent == cursor) {
            break;
        }
        cursor = parent;
    }
    runtime->eventDispatches += 1;
    for (i = 0; i < pathLength; ++i) {
        int32_t listener;
        JsValue currentTarget;
        Result targetStatus = RoboOk;

        JsValue eventValue = JsValueObject(event);

        currentTarget = JsValueObject(JsDomWrapperForNode(runtime, path[i]));
        JsObjectSet(runtime->heap, event, "currentTarget", &currentTarget);
        for (listener = 0; listener < runtime->listenerCount; ++listener) {
            JsListener *slot = &runtime->listeners[listener];
            JsValue ignored = JsValueUndefined();
            Result callStatus;

            if (slot->target != path[i] || !JsEventTypeEquals(slot->type, record->type)) {
                continue;
            }
            if (outResult != NULL) {
                outResult->listenersRun += 1;
                outResult->handled = 1;
            }
            /* `this` is the node the listener sits on and the one argument is the event, which is
             * the calling convention every other door in this module follows. */
            callStatus = JsInterpCallValue(runtime,
                                           &slot->callback,
                                           currentTarget.object,
                                           &eventValue,
                                           1,
                                           &ignored);
            if (callStatus != RoboOk) {
                /* The pending exception is the caller's to report: a listener that threw stops the
                 * walk and does not get quietly skipped. */
                targetStatus = callStatus;
                break;
            }
            if (flags->propagationStopped || flags->propagationImmediate) {
                break;
            }
            if (flags->defaultPrevented && outResult != NULL) {
                outResult->defaultPrevented = 1;
            }
        }
        if (targetStatus != RoboOk) {
            runtime->eventFailures += 1;
            if (outResult != NULL) {
                outResult->defaultPrevented = flags->defaultPrevented;
                outResult->propagationStopped = flags->propagationStopped;
            }
            /* The object's own flag follows the record, so a caller reading `event.defaultPrevented`
             * after a throwing listener still sees the truth. */
            JsObjectSetBool(runtime->heap, event, "defaultPrevented", flags->defaultPrevented);
            return targetStatus;
        }
        if (flags->propagationStopped || flags->propagationImmediate) {
            break;
        }
    }
    if (outResult != NULL) {
        outResult->defaultPrevented = flags->defaultPrevented;
        outResult->propagationStopped = flags->propagationStopped;
    }
    JsObjectSetBool(runtime->heap, event, "defaultPrevented", flags->defaultPrevented);
    return status;
}

Result JsEventQueue(JsRuntime *runtime, const JsEventRecord *record)
{
    if (runtime == NULL || record == NULL) {
        return RoboInvalidArgument;
    }
    return JsLoopPostEventRecord(runtime, record);
}

int32_t JsEventDispatchCount(JsRuntime *runtime)
{
    return runtime != NULL ? runtime->eventDispatches : 0;
}

int32_t JsEventFailureCount(JsRuntime *runtime)
{
    return runtime != NULL ? runtime->eventFailures : 0;
}
