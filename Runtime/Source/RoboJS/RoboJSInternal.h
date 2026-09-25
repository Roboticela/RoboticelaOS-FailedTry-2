/*
 * RoboJS's private shared surface: the runtime struct, and the handful of doors between the module's
 * files that the public headers deliberately do not state.
 *
 * ## Two kinds of allocation, and why they are separated
 *
 * Everything a *script* creates — values, objects, environments, AST nodes, strings — lives in the
 * runtime's `JsHeap` and is released as a whole by `JsHeapReset`. Everything the *runtime* itself
 * owns — its job queue, its timer table, its listener table, its wrapper table and its diagnostics
 * ring — is allocated with `RoboMem` and freed exactly once, in `JsRuntimeDestroy`. That split is
 * why a reset can be cheap (free the heap, clear five indices) without leaking a queue, and why a
 * queue's capacity is a configuration bound rather than something a script can move.
 *
 * ## What is here and nothing more
 *
 * `JsRuntimeExceptionSlot` is the one place the pending exception lives; the interpreter, the
 * function door and the native bindings all reach it through this header rather than each holding a
 * flag. `JsInterpCallValue` is the single call path — a user function and a native binding both
 * arrive here — which is what keeps "the call depth limit" and "a native failure becomes the pending
 * exception" in one place instead of two.
 */

#ifndef ROBO_JS_INTERNAL_H
#define ROBO_JS_INTERNAL_H

#include "RoboJS/RoboJSDom.h"
#include "RoboJS/RoboJSEvent.h"
#include "RoboJS/RoboJSRuntime.h"

#include "RoboMem/RoboMem.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Diagnose ring size: the last N failures, because a diagnostic nobody can read is not one. */
#define JsDiagRingCapacity 32

/** Jobs the queue holds. Bounded so a producer loop cannot grow it without limit. */
typedef struct JsJob {
    uint32_t kind; /* JsTaskKind */
    JsValue callback;
    JsValue thisValue;
    JsValue *args;
    int32_t argCount;
    int32_t argCapacity;
    uint64_t dueMs;
    /** An event job carries its record rather than a callback: see `JsEventDispatchNow`. */
    JsEventRecord record;
} JsJob;

/** One timer slot. */
typedef struct JsTimerSlot {
    int active;
    int repeating;
    int64_t id;
    uint64_t dueMs;
    uint64_t intervalMs;
    JsValue callback;
    JsValue *args;
    int32_t argCount;
    int32_t argCapacity;
    int32_t fired;
} JsTimerSlot;

/** One listener registration. */
typedef struct JsListener {
    JsDomNodeId target;
    char type[24];
    JsValue callback;
    int32_t order;
} JsListener;

/** One interned DOM wrapper. */
typedef struct JsWrapper {
    JsDomNodeId node;
    JsObject *object;
} JsWrapper;

/** The runtime, in full. Every field is owned by the runtime and released by `JsRuntimeDestroy`. */
struct JsRuntime {
    JsRuntimeConfig config;

    JsHeap *heap;

    JsObject *objectPrototype;
    JsObject *arrayPrototype;
    JsObject *functionPrototype;
    JsObject *errorPrototype;
    JsObject *stringPrototype;
    JsObject *globalObject;
    JsEnv *globalEnv;

    /** The pending exception: `hasException` says whether @c exception is meaningful. */
    int hasException;
    JsValue exception;

    /**
     * Where the script is, updated by the interpreter at every statement and expression. An error the
     * engine raises with no span of its own (`new Error("x")`, a native door complaining) is located
     * with it, so `catch (e) { e.line }` answers rather than being absent — the same fields the parser
     * and the step budget already fill. `hasCurrentSpan` is 0 before a script or between them.
     */
    JsSpan currentSpan;
    int hasCurrentSpan;

    /** A script is running: a re-entrant evaluate is `RoboBusy`, not a corrupted environment. */
    int executing;
    /**
     * Set while a loop door is draining. Two things this refuses, and it is the second that needs its
     * own flag: a `JsLoopRunOne` **inside a listener** (which the interpreter's own `executing` guard
     * cannot see, because a task runs through the call door rather than through `JsRuntimeEvaluate`),
     * and a `JsLoopRunUntilIdle` inside a `JsLoopRunOne`. A nested drain would run a task inside a
     * task, and then "microtasks run at every task boundary" would describe something that never
     * happened -- so the refusal is what makes the ordering statements true.
     */
    int inLoop;
    int initialized;

    int32_t steps;
    int32_t maxDepthReached;
    int32_t callDepth;
    int32_t stepLimit;
    int32_t evaluationCount;

    /* Clock. */
    int deterministic;
    uint64_t nowMs;
    uint64_t hostTick;

    /* Jobs. */
    JsJob *jobs;
    int32_t jobCapacity;
    int32_t jobCount;
    int32_t jobHead;
    int32_t tasksRun;
    int stopRequested;

    /* Microtasks. */
    JsJob *microtasks;
    int32_t microtaskCapacity;
    int32_t microtaskCount;
    int32_t microtaskHead;
    int32_t microtasksRun;
    int32_t microtaskOverflows;

    /* Timers. */
    JsTimerSlot *timers;
    int32_t timerCapacity;
    int32_t timerCount;
    int64_t nextTimerId;

    /* Listeners. */
    JsListener *listeners;
    int32_t listenerCapacity;
    int32_t listenerCount;
    int32_t nextListenerOrder;
    int32_t eventDispatches;
    int32_t eventFailures;

    /* DOM. */
    JsDomHost domHost;
    int domInstalled;
    int32_t domHostKind;
    /** The computed-property doors a node wrapper and a style object each point at. They live here
     *  rather than in the wrapper's state because `JsHostOps` is a two-field struct a wrapper holds a
     *  *pointer* to, and one per runtime is what keeps them from being allocated per node. */
    JsHostOps nodeOps;
    JsHostOps styleOps;
    JsWrapper *wrappers;
    int32_t wrapperCapacity;
    int32_t wrapperCount;
    JsObject *documentObject;
    JsObject *nodePrototype;
    JsObject *elementPrototype;
    JsObject *documentPrototype;
    JsObject *stylePrototype;
    JsObject *eventPrototype;
    /** The shell/document integration's own state pointer (the RoboHTML adapter's records). */
    void *domUserdataOwned;

    /* Diagnostics. */
    JsDiag *diagnostics;
    int32_t diagnosticCapacity;
    int32_t diagnosticCount;

    /* Console. */
    JsConsoleFn console;
    void *consoleUserdata;

    /* DOM-adapter counters, kept here so the adapter needs no second lifetime. */
    struct {
        int32_t created;
        int32_t pendingCreated;
        int32_t materialized;
        int32_t textNodesCreated;
        int32_t appends;
        int32_t attributeWrites;
        int32_t textWrites;
        int32_t refusals;
    } counters;
};

/* ------------------------------------------------------------------ */
/* Doors shared between this module's files                            */
/* ------------------------------------------------------------------ */

/** The runtime's pending-exception slot. Never NULL for a live runtime. */
JsValue *JsRuntimeExceptionSlot(JsRuntime *runtime);

/** The single call path: a user function runs its body, a native one runs its binding. See
 *  `RoboJSInternal.h`'s comment. */
Result JsInterpCallValue(JsRuntime *runtime,
                         const JsValue *callable,
                         JsObject *thisObject,
                         const JsValue *args,
                         int32_t argCount,
                         JsValue *out);

/** Consume one execution step. Answers `RoboError` (with a limit exception set) when the budget is
 *  exhausted, `RoboOk` otherwise. */
Result JsRuntimeSpendStep(JsRuntime *runtime, const JsSpan *span);

/** Push/pop a call frame's depth. A push past `maxCallDepth` answers `RoboError` with a limit
 *  exception set and leaves the depth unchanged. */
Result JsRuntimeEnterCall(JsRuntime *runtime, const JsSpan *span);
void JsRuntimeLeaveCall(JsRuntime *runtime);

/** Grow a job's argument array. `RoboOutOfMemory` when the heap refuses. */
Result JsJobSetArgs(JsHeap *heap, JsValue **args, int32_t *capacity, int32_t count,
                    const JsValue *source);

/** The queue's internals, used by the event module and the timer collector to post without a
 *  public door growing a parameter. */
Result JsLoopPostEventRecord(JsRuntime *runtime, const JsEventRecord *record);
Result JsLoopPostMicrotaskValue(JsRuntime *runtime, const JsValue *callback);

/** Run one job (the loop's inner step). Declared here so the event module can dispatch a record
 *  without including the loop's private state. */
Result JsLoopRunJob(JsRuntime *runtime, JsJob *job, int *outRan);

/** Post a firing timer as a job. Declared here because the timer table (`RoboJSTimer.c`) collects the
 *  due slots and the loop (`RoboJSEventLoop.c`) owns the queue: neither file owns both halves, so the
 *  seam between them is this one door rather than a shared struct. */
Result JsLoopPostTimerJob(JsRuntime *runtime, int64_t id);

/** Clear the job's argument storage (no-op on a NULL job). */
void JsJobClear(JsJob *job);

/* ------------------------------------------------------------------ */
/* Prototype installation                                              */
/* ------------------------------------------------------------------ */

/** Create the prototype chain roots and the global object. Called by `JsRuntimeCreate`. */
Result JsRuntimeBuildGlobals(JsRuntime *runtime);

/** The `Object`-shaped constructor functions and their static methods, installed as globals. */
Result JsRuntimeInstallObjectBuiltins(JsRuntime *runtime);
/** `String`, `Number`, `Boolean`, `Math` (a subset) and their static methods. */
Result JsRuntimeInstallValueBuiltins(JsRuntime *runtime);

/** Register a native function as a property of @p target. */
Result JsRuntimeDefineNative(JsRuntime *runtime,
                             JsObject *target,
                             const char *name,
                             int32_t arity,
                             JsNativeFn fn,
                             void *userdata);

/** Register a native function as a global (property of the global object **and** a global binding). */
Result JsRuntimeDefineGlobalNative(JsRuntime *runtime,
                                   const char *name,
                                   int32_t arity,
                                   JsNativeFn fn,
                                   void *userdata);

/** Register a native function as an own property of an object. */
Result JsObjectDefineNative(JsRuntime *runtime,
                            JsObject *target,
                            const char *name,
                            int32_t arity,
                            JsNativeFn fn,
                            void *userdata);

/** A shared, empty array assigned to `push`/`pop`-style argument buffers. Never NULL. */
const JsValue *JsEmptyArgs(void);

/** The three `console` bindings, registered by `JsRuntimeInstallGlobals` (16.5's host sink). */
Result JsConsoleLog(JsRuntime *runtime,
                    JsObject *thisObject,
                    const JsValue *args,
                    int32_t argCount,
                    JsValue *out,
                    void *userdata);
Result JsConsoleWarn(JsRuntime *runtime,
                     JsObject *thisObject,
                     const JsValue *args,
                     int32_t argCount,
                     JsValue *out,
                     void *userdata);
Result JsConsoleError(JsRuntime *runtime,
                      JsObject *thisObject,
                      const JsValue *args,
                      int32_t argCount,
                      JsValue *out,
                      void *userdata);

/** The DOM binding's install/uninstall internals, so the runtime can reach them without including
 *  a DOM-aware header (`RoboJSDom.c`). `JsDomUninstall` drops the wrappers and the node methods; the
 *  host's own storage belongs to the adapter and is not touched. */
int32_t JsDomUninstall(JsRuntime *runtime);

/**
 * Move an interned wrapper from a synthetic node id to the real one it materialised into.
 *
 * `JsDomHtml.c`'s pending records are the adapter's, but the wrapper table is `RoboJSDom.c`'s, so
 * this is the one internal door between them: a created element's id changes when it is attached, and
 * the object the script is holding has to change with it or a saved reference goes stale.
 */
Result JsDomRetargetWrapper(JsRuntime *runtime, JsDomNodeId from, JsDomNodeId to);

/** Build the `Event`-shaped object prototype once, before any listener can be registered. */
Result JsEventInstallPrototype(JsRuntime *runtime);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_JS_INTERNAL_H */
