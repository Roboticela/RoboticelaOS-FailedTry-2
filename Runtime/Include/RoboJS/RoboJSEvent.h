/*
 * RoboJS event bindings (16.11, and 16.16's input path) — listeners, the event object, and the one
 * door an input event enters through.
 *
 * ## The path an input event takes
 *
 *   RoboInput → the shell's input adapter → `JsEventQueue` → the event loop (16.13) → the dispatch
 *   below → a script's listener → a DOM mutation → the host's own invalidation.
 *
 * `JsEventQueue` **posts** and does not call: a device thread, an input callback or a host frame
 * never runs a JavaScript function, which is the threading rule (`Documentation/RoboJS/Threading.md`)
 * and the reason a listener that takes ten milliseconds cannot stall an interrupt path. The queue
 * carries a fixed-shape record — a type, a target node, coordinates, a key, a modifier mask, text —
 * because a queued event must be copyable and a `JsValue` is.
 *
 * ## Listeners live on the wrapper, and their order is the order they were added
 *
 * A listener is a function value held in an array on the element wrapper, in insertion order, so
 * "multiple listeners" and "listener ordering" are facts about one array rather than about a hash
 * table's iteration. `removeEventListener` removes the first entry with that exact function value.
 *
 * ## Propagation
 *
 * Dispatch builds the path from the target's ancestors (through the DOM vtable) and invokes each
 * node's listeners for the type, then the path is discarded. The event object is created **per
 * dispatch**, not per node, so `event.target` stays the original node while a listener runs for an
 * ancestor, and `preventDefault()` records the one fact the runtime reports back to its caller —
 * which is what a scrollable host consults before scrolling.
 *
 * ## The event object
 *
 * An ordinary object with `type`, `target`, `currentTarget`, `timestamp`, `defaultPrevented`, and,
 * depending on the type, `clientX`/`clientY`/`button` for a pointer event, `key`/`repeat`/
 * `modifiers` for a keyboard event and `data` for a text input. `preventDefault` and
 * `stopPropagation` are native functions on it. This is not the DOM Event interface: no capture
 * phase, no `stopImmediatePropagation`, no `bubbles`/`cancelable` flags, and no `CustomEvent`.
 */

#ifndef ROBO_JS_EVENT_H
#define ROBO_JS_EVENT_H

#include "RoboJS/RoboJSDom.h"
#include "RoboJS/RoboJSTypes.h"
#include "RoboJS/RoboJSValue.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Pointer/keyboard modifier mask, matching RoboInput's own bit order for the six it shares. */
#define JsEventModifierShift 1u
#define JsEventModifierControl 2u
#define JsEventModifierAlt 4u
#define JsEventModifierMeta 8u
#define JsEventModifierCapsLock 16u
#define JsEventModifierNumLock 32u

/** One queued event. Everything is copied, so the queue is safe across a producer/host boundary. */
typedef struct JsEventRecord {
    /** `"click"`, `"pointerdown"`, `"keydown"`, `"input"`, `"change"`, `"focus"`, `"blur"`, … */
    char type[24];
    JsDomNodeId target;
    double clientX;
    double clientY;
    int32_t button;
    uint32_t modifiers;
    /** `"a"`, `"ArrowLeft"`, … (short, because a key name is a name). */
    char key[32];
    int repeat;
    /** Text a `textinput`-shaped event carries. */
    char data[128];
    uint64_t timestampMs;
} JsEventRecord;

/** The result of one dispatch: what the runtime tells its caller. */
typedef struct JsEventResult {
    /** Listeners invoked across the whole path. */
    int32_t listenersRun;
    /** 1 when a listener called `preventDefault()`. */
    int defaultPrevented;
    /** 1 when a listener called `stopPropagation()`. */
    int propagationStopped;
    /** 1 when the type had at least one listener anywhere on the path. */
    int handled;
} JsEventResult;

/** Install the event vocabulary (`Event`-shaped object prototype and the three node doors). Called
 *  by `JsDomInstall`, because a listener without a node to sit on is nothing. */
Result JsEventInstall(JsRuntime *runtime);

/**
 * Register a listener. Called by the binding's `addEventListener`; a host layer may also call it.
 * @return RoboOk; RoboInvalidArgument when @p target is 0, @p type is empty or @p listener is not
 *         callable.
 */
Result JsEventAddListener(JsRuntime *runtime,
                          JsDomNodeId target,
                          const char *type,
                          const JsValue *listener);

/** Remove the first listener with this exact function value. `RoboNotFound` when there is none. */
Result JsEventRemoveListener(JsRuntime *runtime,
                             JsDomNodeId target,
                             const char *type,
                             const JsValue *listener);

/** Listeners registered on @p target (all types). */
int32_t JsEventListenerCount(JsRuntime *runtime, JsDomNodeId target);

/** Total listeners across every node — the number a lifecycle test asserts went to zero. */
int32_t JsEventTotalListenerCount(JsRuntime *runtime);

/** Drop every listener on @p target (a node removed from the tree, and the destroy path). Answers
 *  how many were dropped. */
int32_t JsEventDropListeners(JsRuntime *runtime, JsDomNodeId target);

/** Drop every listener everywhere (the reset and window-destroyed paths). */
int32_t JsEventDropAll(JsRuntime *runtime);

/**
 * Dispatch @p record **now**: build the path, invoke listeners, report. This is the door
 * `JsLoopRunOne` uses for an event job, and the door a host frame uses when it wants a synchronous
 * answer (a scroll host asking "did the page take this wheel event?").
 *
 * @return RoboOk; `RoboNotSupported` when the runtime has no DOM; `RoboError` when a listener threw
 *         (the pending exception holds it, and the rest of the path is not run — one throwing
 *         listener does not drop the others' registrations, only this dispatch).
 */
Result JsEventDispatchNow(JsRuntime *runtime,
                          const JsEventRecord *record,
                          JsEventResult *out);

/**
 * Queue @p record for the next event-loop boundary. @return RoboOk, or the queue's own failure
 * (`RoboOutOfMemory` at `maxQueueLength`).
 */
Result JsEventQueue(JsRuntime *runtime, const JsEventRecord *record);

/** Fill @p record's string fields from C strings — the door a shell's input adapter uses so it does
 *  not have to know the record's field sizes. Answers `RoboInvalidArgument` for a NULL @p record. */
Result JsEventRecordInit(JsEventRecord *record, const char *type, JsDomNodeId target);

/** Events dispatched since creation or the last reset, and events whose dispatch threw. */
int32_t JsEventDispatchCount(JsRuntime *runtime);
int32_t JsEventFailureCount(JsRuntime *runtime);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_JS_EVENT_H */
