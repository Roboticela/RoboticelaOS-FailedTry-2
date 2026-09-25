/*
 * The RoboJS runtime (see `RoboJSRuntime.h`).
 *
 * The file is five pieces:
 *
 *   **lifecycle**      create / reset / destroy, and the rule that a reset frees the script's heap
 *                      while keeping the caller's policy and its DOM host;
 *   **prototypes**     the five chain roots every value in this runtime eventually reaches, plus the
 *                      global object, built once here so a binding can hang a method off a chain its
 *                      values already have;
 *   **evaluation**     lex, parse, run, and turn whatever came out into a `Result` plus a diagnostic;
 *   **the clock**      the deterministic counter or the host's callback, and the one door that moves
 *                      the former;
 *   **diagnostics**    a fixed ring, a structured record, and a complete-or-absent state dump.
 */

#include "RoboJS/RoboJSRuntime.h"

#include "RoboJS/RoboJSException.h"
#include "RoboJS/RoboJSEvent.h"
#include "RoboJS/RoboJSEventLoop.h"
#include "RoboJS/RoboJSInterpreter.h"
#include "RoboJS/RoboJSParser.h"
#include "RoboJS/RoboJSTimer.h"

#include "RoboJSInternal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "RoboBase/RoboLog.h"

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

void JsRuntimeConfigInit(JsRuntimeConfig *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->maxSteps = (int32_t)RoboJsDefaultMaxSteps;
    config->maxCallDepth = (int32_t)RoboJsDefaultMaxCallDepth;
    config->maxMicrotasksPerCycle = (int32_t)RoboJsDefaultMaxMicrotasks;
    config->maxTimersPerCycle = (int32_t)RoboJsDefaultMaxTimersPerCycle;
    config->maxTasksPerCycle = (int32_t)RoboJsDefaultMaxTasksPerCycle;
    config->maxQueueLength = (int32_t)RoboJsDefaultMaxQueueLength;
    config->maxTimerCount = (int32_t)RoboJsDefaultMaxTimerCount;
    config->maxPropertiesPerObject = (int32_t)RoboJsDefaultMaxProperties;
    config->maxStringLength = (int32_t)RoboJsDefaultMaxStringLength;
    config->maxParseDepth = (int32_t)RoboJsDefaultMaxParseDepth;
    config->heapChunkBytes = (int32_t)RoboJsDefaultHeapChunkBytes;
    config->enableTimers = 1;
    config->enableEventLoop = 1;
    config->enableConsole = 1;
    config->enableDiagnostics = 1;
    config->deterministic = 1;
}

static int32_t JsConfigValue(int32_t configured, int32_t fallback)
{
    return configured > 0 ? configured : fallback;
}

/* ------------------------------------------------------------------ */
/* Global built-ins: registration helpers                              */
/* ------------------------------------------------------------------ */

const JsValue *JsEmptyArgs(void)
{
    static const JsValue empty = { RoboJsTypeUndefined, 0, 0.0, NULL, 0, NULL };

    return &empty;
}

Result JsObjectDefineNative(JsRuntime *runtime,
                            JsObject *target,
                            const char *name,
                            int32_t arity,
                            JsNativeFn fn,
                            void *userdata)
{
    JsObject *function;
    JsValue wrapped;

    if (runtime == NULL || target == NULL || name == NULL || fn == NULL) {
        return RoboInvalidArgument;
    }
    function = JsFunctionCreateNative(runtime->heap,
                                      name,
                                      arity,
                                      fn,
                                      userdata,
                                      runtime->functionPrototype);
    if (function == NULL) {
        return RoboOutOfMemory;
    }
    wrapped = JsValueObject(function);
    return JsObjectSet(runtime->heap, target, name, &wrapped);
}

Result JsRuntimeDefineNative(JsRuntime *runtime,
                             JsObject *target,
                             const char *name,
                             int32_t arity,
                             JsNativeFn fn,
                             void *userdata)
{
    return JsObjectDefineNative(runtime, target, name, arity, fn, userdata);
}

Result JsRuntimeDefineGlobal(JsRuntime *runtime, const char *name, const JsValue *value)
{
    JsBinding *binding;
    Result status;

    if (runtime == NULL || name == NULL || value == NULL) {
        return RoboInvalidArgument;
    }
    status = JsObjectDefine(runtime->heap, runtime->globalObject, name, value, JsPropAll);
    if (status != RoboOk) {
        return status;
    }
    /* One name, two readers: the property makes `this.foo` and `window.foo` work, the binding makes
     * a bare `foo` resolve without a property lookup in the hot path. They are written together and
     * nothing else writes either, so they cannot disagree. */
    binding = NULL;
    {
        int32_t i;

        for (i = 0; i < runtime->globalEnv->bindingCount; ++i) {
            if (strcmp(runtime->globalEnv->bindings[i].name, name) == 0) {
                binding = &runtime->globalEnv->bindings[i];
                break;
            }
        }
    }
    if (binding != NULL) {
        binding->value = *value;
        return RoboOk;
    }
    return JsEnvDeclareOrSet(runtime->heap, runtime->globalEnv, name, value);
}

Result JsRuntimeGetGlobal(JsRuntime *runtime, const char *name, JsValue *out)
{
    if (runtime == NULL || name == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    return JsObjectGet(runtime->globalObject, name, out);
}

Result JsRuntimeDeleteGlobal(JsRuntime *runtime, const char *name)
{
    Result binding;

    if (runtime == NULL || name == NULL) {
        return RoboInvalidArgument;
    }
    /* The binding is the reader a bare name resolves through, so it goes first: if it is not there,
     * there is nothing to remove and the object is left untouched. */
    binding = JsEnvRemove(runtime->globalEnv, name);
    if (binding == RoboNotFound) {
        return RoboNotFound;
    }
    JsObjectDelete(runtime->globalObject, name);
    return RoboOk;
}

Result JsRuntimeDefineGlobalNative(JsRuntime *runtime,
                                   const char *name,
                                   int32_t arity,
                                   JsNativeFn fn,
                                   void *userdata)
{
    JsObject *function;
    JsValue wrapped;
    Result status;

    if (runtime == NULL || name == NULL || fn == NULL) {
        return RoboInvalidArgument;
    }
    function = JsFunctionCreateNative(runtime->heap,
                                      name,
                                      arity,
                                      fn,
                                      userdata,
                                      runtime->functionPrototype);
    if (function == NULL) {
        return RoboOutOfMemory;
    }
    wrapped = JsValueObject(function);
    status = JsRuntimeDefineGlobal(runtime, name, &wrapped);
    return status;
}

/* ------------------------------------------------------------------ */
/* Prototypes and the global object                                    */
/* ------------------------------------------------------------------ */

Result JsRuntimeBuildGlobals(JsRuntime *runtime)
{
    JsValue wrapped;

    if (runtime == NULL) {
        return RoboInvalidArgument;
    }
    runtime->objectPrototype = JsObjectCreate(runtime->heap, NULL);
    if (runtime->objectPrototype == NULL) {
        return RoboOutOfMemory;
    }
    runtime->functionPrototype = JsObjectCreate(runtime->heap, runtime->objectPrototype);
    runtime->arrayPrototype = JsObjectCreate(runtime->heap, runtime->objectPrototype);
    runtime->errorPrototype = JsObjectCreate(runtime->heap, runtime->objectPrototype);
    runtime->stringPrototype = JsObjectCreate(runtime->heap, runtime->objectPrototype);
    if (runtime->functionPrototype == NULL || runtime->arrayPrototype == NULL ||
        runtime->errorPrototype == NULL || runtime->stringPrototype == NULL) {
        return RoboOutOfMemory;
    }
    runtime->globalObject = JsObjectCreate(runtime->heap, runtime->objectPrototype);
    if (runtime->globalObject == NULL) {
        return RoboOutOfMemory;
    }
    runtime->globalEnv = JsEnvCreate(runtime->heap, NULL, RoboJsEnvGlobal);
    if (runtime->globalEnv == NULL) {
        return RoboOutOfMemory;
    }
    /* The global link owns the object's mirroring from here on, so a `var`, an assignment and a
     * built-in install all keep `foo` and `this.foo` one value (`RoboJSEnvironment.h`). */
    if (JsEnvBindGlobalObject(runtime->globalEnv, runtime->globalObject) != RoboOk) {
        return RoboOutOfMemory;
    }
    /* `window` is the global object itself, which is what makes `window.foo = 1` and `foo` one
     * thing rather than two that can disagree. */
    wrapped = JsValueObject(runtime->globalObject);
    if (JsRuntimeDefineGlobal(runtime, "window", &wrapped) != RoboOk) {
        return RoboOutOfMemory;
    }
    if (JsRuntimeDefineGlobal(runtime, "globalThis", &wrapped) != RoboOk) {
        return RoboOutOfMemory;
    }
    {
        JsValue value = JsValueUndefined();

        JsRuntimeDefineGlobal(runtime, "undefined", &value);
        value = JsValueNumber(0.0 / 0.0);
        JsRuntimeDefineGlobal(runtime, "NaN", &value);
        value = JsValueNumber(1.0 / 0.0);
        JsRuntimeDefineGlobal(runtime, "Infinity", &value);
    }
    return RoboOk;
}

Result JsRuntimeInstallGlobals(JsRuntime *runtime)
{
    Result status;

    if (runtime == NULL) {
        return RoboInvalidArgument;
    }
    status = JsRuntimeInstallObjectBuiltins(runtime);
    if (status != RoboOk) {
        return status;
    }
    status = JsRuntimeInstallValueBuiltins(runtime);
    if (status != RoboOk) {
        return status;
    }
    if (runtime->config.enableEventLoop) {
        status = JsMicrotaskInstallGlobals(runtime);
        if (status != RoboOk) {
            return status;
        }
    }
    if (runtime->config.enableTimers) {
        status = JsTimerInstallGlobals(runtime);
        if (status != RoboOk) {
            return status;
        }
    }
    if (runtime->config.enableConsole) {
        JsObject *console = JsObjectCreate(runtime->heap, runtime->objectPrototype);

        if (console == NULL) {
            return RoboOutOfMemory;
        }
        if (JsObjectDefineNative(runtime, console, "log", 1, JsConsoleLog, NULL) != RoboOk ||
            JsObjectDefineNative(runtime, console, "warn", 1, JsConsoleWarn, NULL) != RoboOk ||
            JsObjectDefineNative(runtime, console, "error", 1, JsConsoleError, NULL) != RoboOk) {
            return RoboOutOfMemory;
        }
        {
            JsValue wrapped = JsValueObject(console);

            if (JsRuntimeDefineGlobal(runtime, "console", &wrapped) != RoboOk) {
                return RoboOutOfMemory;
            }
        }
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void JsRuntimeFreeTables(JsRuntime *runtime)
{
    if (runtime->jobs != NULL) {
        int32_t i;

        for (i = 0; i < runtime->jobCapacity; ++i) {
            JsJobClear(&runtime->jobs[i]);
        }
        RoboFree(runtime->jobs);
        runtime->jobs = NULL;
    }
    if (runtime->microtasks != NULL) {
        int32_t i;

        for (i = 0; i < runtime->microtaskCapacity; ++i) {
            JsJobClear(&runtime->microtasks[i]);
        }
        RoboFree(runtime->microtasks);
        runtime->microtasks = NULL;
    }
    if (runtime->timers != NULL) {
        RoboFree(runtime->timers);
        runtime->timers = NULL;
    }
    if (runtime->listeners != NULL) {
        RoboFree(runtime->listeners);
        runtime->listeners = NULL;
    }
    if (runtime->wrappers != NULL) {
        RoboFree(runtime->wrappers);
        runtime->wrappers = NULL;
    }
    if (runtime->diagnostics != NULL) {
        RoboFree(runtime->diagnostics);
        runtime->diagnostics = NULL;
    }
}

JsRuntime *JsRuntimeCreate(const JsRuntimeConfig *config)
{
    JsRuntime *runtime;

    runtime = (JsRuntime *)RoboCalloc(1, sizeof(JsRuntime));
    if (runtime == NULL) {
        return NULL;
    }
    JsRuntimeConfigInit(&runtime->config);
    if (config != NULL) {
        runtime->config = *config;
    }
    runtime->stepLimit = JsConfigValue(runtime->config.maxSteps, (int32_t)RoboJsDefaultMaxSteps);
    runtime->deterministic = runtime->config.deterministic ? 1 : 0;
    runtime->console = runtime->config.console;
    runtime->consoleUserdata = runtime->config.consoleUserdata;
    runtime->heap = JsHeapCreate((size_t)JsConfigValue(runtime->config.heapChunkBytes,
                                                      (int32_t)RoboJsDefaultHeapChunkBytes));
    if (runtime->heap == NULL) {
        RoboFree(runtime);
        return NULL;
    }
    JsHeapSetLimits(runtime->heap, runtime->config.maxPropertiesPerObject,
                    runtime->config.maxStringLength);

    runtime->jobCapacity = JsConfigValue(runtime->config.maxQueueLength,
                                         (int32_t)RoboJsDefaultMaxQueueLength);
    runtime->jobs = (JsJob *)RoboCalloc((size_t)runtime->jobCapacity, sizeof(JsJob));
    runtime->microtaskCapacity = runtime->jobCapacity;
    runtime->microtasks = (JsJob *)RoboCalloc((size_t)runtime->microtaskCapacity, sizeof(JsJob));
    runtime->timerCapacity = JsConfigValue(runtime->config.maxTimerCount,
                                           (int32_t)RoboJsDefaultMaxTimerCount);
    runtime->timers = (JsTimerSlot *)RoboCalloc((size_t)runtime->timerCapacity, sizeof(JsTimerSlot));
    runtime->listenerCapacity = 256;
    runtime->listeners = (JsListener *)RoboCalloc((size_t)runtime->listenerCapacity,
                                                  sizeof(JsListener));
    runtime->wrapperCapacity = 128;
    runtime->wrappers = (JsWrapper *)RoboCalloc((size_t)runtime->wrapperCapacity, sizeof(JsWrapper));
    runtime->diagnosticCapacity = JsDiagRingCapacity;
    runtime->diagnostics = (JsDiag *)RoboCalloc((size_t)runtime->diagnosticCapacity, sizeof(JsDiag));
    runtime->exception = JsValueUndefined();
    runtime->nextTimerId = 1;

    if (runtime->jobs == NULL || runtime->microtasks == NULL || runtime->timers == NULL ||
        runtime->listeners == NULL || runtime->wrappers == NULL || runtime->diagnostics == NULL) {
        JsRuntimeFreeTables(runtime);
        JsHeapDestroy(runtime->heap);
        RoboFree(runtime);
        return NULL;
    }
    if (JsRuntimeBuildGlobals(runtime) != RoboOk || JsRuntimeInstallGlobals(runtime) != RoboOk) {
        JsRuntimeFreeTables(runtime);
        JsHeapDestroy(runtime->heap);
        RoboFree(runtime);
        return NULL;
    }
    runtime->initialized = 1;
    return runtime;
}

JsRuntime *JsRuntimeCreateDefault(void)
{
    JsRuntimeConfig config;

    JsRuntimeConfigInit(&config);
    return JsRuntimeCreate(&config);
}

void JsRuntimeDestroy(JsRuntime *runtime)
{
    if (runtime == NULL) {
        return;
    }
    JsRuntimeFreeTables(runtime);
    JsHeapDestroy(runtime->heap);
    RoboFree(runtime);
}

Result JsRuntimeReset(JsRuntime *runtime)
{
    JsDomHost savedHost;
    int hadDom;
    void *savedUserdata;

    if (runtime == NULL) {
        return RoboInvalidArgument;
    }
    savedHost = runtime->domHost;
    hadDom = runtime->domInstalled;
    savedUserdata = runtime->domUserdataOwned;

    JsEventDropAll(runtime);
    JsLoopClear(runtime);
    JsTimerCancelAll(runtime);
    runtime->wrapperCount = 0;
    runtime->documentObject = NULL;
    runtime->domInstalled = 0;
    runtime->hasException = 0;
    runtime->exception = JsValueUndefined();
    runtime->executing = 0;
    runtime->steps = 0;
    runtime->callDepth = 0;
    runtime->maxDepthReached = 0;
    runtime->evaluationCount = 0;
    runtime->nowMs = 0;
    runtime->hostTick = 0;
    runtime->nextTimerId = 1;
    memset(&runtime->counters, 0, sizeof(runtime->counters));

    if (JsHeapReset(runtime->heap) != RoboOk) {
        return RoboOutOfMemory;
    }
    if (JsRuntimeBuildGlobals(runtime) != RoboOk || JsRuntimeInstallGlobals(runtime) != RoboOk) {
        return RoboOutOfMemory;
    }
    if (hadDom) {
        /* The same host, against a fresh heap: the wrappers went with the old heap and the document
         * object is rebuilt. The host's own storage (the adapter's records) is the caller's and
         * survives, which is what makes a reset a reset rather than a teardown. */
        runtime->domUserdataOwned = savedUserdata;
        if (JsDomInstall(runtime, &savedHost) != RoboOk) {
            return RoboOutOfMemory;
        }
    }
    return RoboOk;
}

int JsRuntimeIsInitialized(JsRuntime *runtime)
{
    return runtime != NULL && runtime->initialized;
}

int JsRuntimeIsExecuting(JsRuntime *runtime)
{
    return runtime != NULL && runtime->executing;
}

/* ------------------------------------------------------------------ */
/* Step and depth budgets                                              */
/* ------------------------------------------------------------------ */

Result JsRuntimeSpendStep(JsRuntime *runtime, const JsSpan *span)
{
    if (runtime == NULL) {
        return RoboInvalidArgument;
    }
    runtime->steps += 1;
    if (runtime->steps > runtime->stepLimit) {
        JsRuntimeThrow(runtime,
                       RoboJsErrorLimit,
                       span,
                       "the script ran longer than the %d steps this runtime allows",
                       runtime->stepLimit);
        return RoboError;
    }
    return RoboOk;
}

Result JsRuntimeEnterCall(JsRuntime *runtime, const JsSpan *span)
{
    int32_t limit;

    if (runtime == NULL) {
        return RoboInvalidArgument;
    }
    limit = JsConfigValue(runtime->config.maxCallDepth, (int32_t)RoboJsDefaultMaxCallDepth);
    if (runtime->callDepth >= limit) {
        JsRuntimeThrow(runtime,
                       RoboJsErrorLimit,
                       span,
                       "the call stack is deeper than the %d frames this runtime allows",
                       limit);
        return RoboError;
    }
    runtime->callDepth += 1;
    if (runtime->callDepth > runtime->maxDepthReached) {
        runtime->maxDepthReached = runtime->callDepth;
    }
    return RoboOk;
}

void JsRuntimeLeaveCall(JsRuntime *runtime)
{
    if (runtime != NULL && runtime->callDepth > 0) {
        runtime->callDepth -= 1;
    }
}

Result JsJobSetArgs(JsHeap *heap,
                    JsValue **args,
                    int32_t *capacity,
                    int32_t count,
                    const JsValue *source)
{
    int32_t i;

    if (args == NULL || capacity == NULL) {
        return RoboInvalidArgument;
    }
    if (count <= 0) {
        *args = NULL;
        *capacity = 0;
        return RoboOk;
    }
    *args = (JsValue *)JsHeapAllocArray(heap, (size_t)count, sizeof(JsValue));
    if (*args == NULL) {
        *capacity = 0;
        return RoboOutOfMemory;
    }
    for (i = 0; i < count; ++i) {
        (*args)[i] = source != NULL ? source[i] : JsValueUndefined();
    }
    *capacity = count;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Accessors                                                           */
/* ------------------------------------------------------------------ */

JsHeap *JsRuntimeHeap(JsRuntime *runtime)
{
    return runtime != NULL ? runtime->heap : NULL;
}

JsObject *JsRuntimeGlobalObject(JsRuntime *runtime)
{
    return runtime != NULL ? runtime->globalObject : NULL;
}

JsEnv *JsRuntimeGlobalEnv(JsRuntime *runtime)
{
    return runtime != NULL ? runtime->globalEnv : NULL;
}

JsObject *JsRuntimeObjectPrototype(JsRuntime *runtime)
{
    return runtime != NULL ? runtime->objectPrototype : NULL;
}

JsObject *JsRuntimeArrayPrototype(JsRuntime *runtime)
{
    return runtime != NULL ? runtime->arrayPrototype : NULL;
}

JsObject *JsRuntimeFunctionPrototype(JsRuntime *runtime)
{
    return runtime != NULL ? runtime->functionPrototype : NULL;
}

JsObject *JsRuntimeErrorPrototype(JsRuntime *runtime)
{
    return runtime != NULL ? runtime->errorPrototype : NULL;
}

JsObject *JsRuntimeStringPrototype(JsRuntime *runtime)
{
    return runtime != NULL ? runtime->stringPrototype : NULL;
}

int32_t JsRuntimeStepCount(JsRuntime *runtime)
{
    return runtime != NULL ? runtime->steps : 0;
}

int32_t JsRuntimeMaxDepthReached(JsRuntime *runtime)
{
    return runtime != NULL ? runtime->maxDepthReached : 0;
}

int32_t JsRuntimeEvaluationCount(JsRuntime *runtime)
{
    return runtime != NULL ? runtime->evaluationCount : 0;
}

uint64_t JsRuntimeNowMs(JsRuntime *runtime)
{
    if (runtime == NULL) {
        return 0;
    }
    if (runtime->deterministic) {
        return runtime->nowMs;
    }
    if (runtime->config.clock != NULL) {
        return runtime->config.clock(runtime->config.clockUserdata);
    }
    return runtime->hostTick;
}

Result JsRuntimeAdvanceClock(JsRuntime *runtime, uint64_t deltaMs)
{
    if (runtime == NULL) {
        return RoboInvalidArgument;
    }
    if (!runtime->deterministic) {
        return RoboNotSupported;
    }
    runtime->nowMs += deltaMs;
    return RoboOk;
}

void JsRuntimeSetConsole(JsRuntime *runtime, JsConsoleFn console, void *userdata)
{
    if (runtime == NULL) {
        return;
    }
    if (console == NULL) {
        /* "NULL clears back to the configured one" (`RoboJSRuntime.h`): the configuration's sink is
         * kept for exactly this, so clearing is not the same as muting. */
        runtime->console = runtime->config.console;
        runtime->consoleUserdata = runtime->config.consoleUserdata;
        return;
    }
    runtime->console = console;
    runtime->consoleUserdata = userdata;
}

Result JsRuntimeRunUntilIdle(JsRuntime *runtime)
{
    if (runtime == NULL) {
        return RoboInvalidArgument;
    }
    if (!runtime->config.enableEventLoop) {
        return RoboNotSupported;
    }
    return JsLoopRunUntilIdle(runtime, 0);
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

void JsRuntimeAddDiagnostic(JsRuntime *runtime,
                            JsDiagLevel level,
                            JsErrorKind kind,
                            const char *script,
                            const JsSpan *span,
                            const char *format,
                            ...)
{
    JsDiag *slot;
    va_list args;

    if (runtime == NULL || !runtime->config.enableDiagnostics) {
        return;
    }
    slot = &runtime->diagnostics[runtime->diagnosticCount % runtime->diagnosticCapacity];
    memset(slot, 0, sizeof(*slot));
    slot->level = level;
    slot->kind = kind;
    slot->line = span != NULL ? span->line : 0;
    slot->column = span != NULL ? span->column : 0;
    slot->script = script != NULL ? script : "";
    if (format != NULL) {
        va_start(args, format);
        vsnprintf(slot->message, sizeof(slot->message), format, args);
        va_end(args);
    }
    runtime->diagnosticCount += 1;
}

int32_t JsRuntimeDiagnosticCount(JsRuntime *runtime)
{
    if (runtime == NULL) {
        return 0;
    }
    return runtime->diagnosticCount < runtime->diagnosticCapacity ? runtime->diagnosticCount
                                                                 : runtime->diagnosticCapacity;
}

Result JsRuntimeDiagnosticAt(JsRuntime *runtime, int32_t index, JsDiag *out)
{
    int32_t stored;

    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    stored = JsRuntimeDiagnosticCount(runtime);
    if (index < 0 || index >= stored) {
        return RoboNotFound;
    }
    {
        int32_t oldest = runtime->diagnosticCount > runtime->diagnosticCapacity
                             ? runtime->diagnosticCount - runtime->diagnosticCapacity
                             : 0;

        *out = runtime->diagnostics[(oldest + index) % runtime->diagnosticCapacity];
    }
    return RoboOk;
}

void JsRuntimeClearDiagnostics(JsRuntime *runtime)
{
    if (runtime == NULL) {
        return;
    }
    memset(runtime->diagnostics, 0, (size_t)runtime->diagnosticCapacity * sizeof(JsDiag));
    runtime->diagnosticCount = 0;
}

int32_t JsRuntimeWrite(const JsRuntime *runtime, char *buffer, size_t capacity)
{
    char scratch[1024];

    if (buffer != NULL && capacity > 0) {
        buffer[0] = '\0';
    }
    if (runtime == NULL) {
        return 0;
    }
    if (buffer == NULL || capacity == 0) {
        return 0;
    }
    {
        int written = snprintf(scratch,
                               sizeof(scratch),
                               "RoboJS %s steps=%d/%d depth=%d/%d evals=%d heap=%u/%u jobs=%d "
                               "microtasks=%d timers=%d listeners=%d wrappers=%d dom=%d "
                               "diagnostics=%d\n",
                               JsVersionString(),
                               runtime->steps,
                               runtime->stepLimit,
                               runtime->callDepth,
                               JsConfigValue(runtime->config.maxCallDepth,
                                             (int32_t)RoboJsDefaultMaxCallDepth),
                               runtime->evaluationCount,
                               (unsigned)JsHeapLiveBytes(runtime->heap),
                               (unsigned)JsHeapAllocationCount(runtime->heap),
                               runtime->jobCount,
                               runtime->microtaskCount,
                               runtime->timerCount,
                               runtime->listenerCount,
                               runtime->wrapperCount,
                               runtime->domInstalled,
                               JsRuntimeDiagnosticCount((JsRuntime *)runtime));

        if (written < 0) {
            return 0;
        }
        if ((size_t)written + 1u > capacity) {
            /* Complete or absent: report the length a complete dump needs, leave the buffer empty. */
            return written;
        }
        memcpy(buffer, scratch, (size_t)written + 1u);
        return written;
    }
}

/* ------------------------------------------------------------------ */
/* Evaluation                                                          */
/* ------------------------------------------------------------------ */

static Result JsRuntimeReport(JsRuntime *runtime,
                              const char *scriptName,
                              Result status,
                              const JsError *error,
                              const JsValue *thrown)
{
    if (runtime == NULL || !runtime->config.enableDiagnostics) {
        return status;
    }
    if (status == RoboParseError && error != NULL) {
        JsRuntimeAddDiagnostic(runtime,
                               RoboJsDiagError,
                               error->kind,
                               scriptName,
                               error->hasSpan ? &error->span : NULL,
                               "%s",
                               error->message);
        return status;
    }
    if (status == RoboError && thrown != NULL) {
        char message[256];

        message[0] = '\0';
        JsExceptionMessage(thrown, message, sizeof(message));
        JsRuntimeAddDiagnostic(runtime,
                               RoboJsDiagError,
                               JsExceptionKindOf(thrown),
                               scriptName,
                               NULL,
                               "uncaught %s: %s",
                               JsErrorKindName(JsExceptionKindOf(thrown)),
                               message);
    }
    return status;
}

Result JsRuntimeParse(JsRuntime *runtime,
                      const char *source,
                      int32_t length,
                      JsNode **out)
{
    JsParser parser;
    Result status;

    if (runtime == NULL || source == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    status = JsParserInit(&parser,
                          runtime->heap,
                          source,
                          length,
                          JsConfigValue(runtime->config.maxParseDepth,
                                        (int32_t)RoboJsDefaultMaxParseDepth));
    if (status != RoboOk) {
        return status;
    }
    return JsParserParseProgram(&parser, out);
}

static Result JsRuntimeRunTree(JsRuntime *runtime, JsNode *program, JsValue *out)
{
    JsScriptResult result;
    Result status;

    memset(&result, 0, sizeof(result));
    runtime->steps = 0;
    runtime->callDepth = 0;
    status = JsInterpRunProgram(runtime, program, runtime->globalEnv, &result);
    runtime->evaluationCount += 1;
    if (status != RoboOk) {
        JsValue thrown;

        thrown = JsValueUndefined();
        if (result.error.kind != RoboJsErrorNone) {
            JsRuntimeAddDiagnostic(runtime,
                                   RoboJsDiagError,
                                   result.error.kind,
                                   "",
                                   result.error.hasSpan ? &result.error.span : NULL,
                                   "%s",
                                   result.error.message);
            if (out != NULL) {
                *out = result.value;
            }
            return status;
        }
        if (JsRuntimePeekException(runtime, &thrown) == RoboOk) {
            (void)JsRuntimeReport(runtime, "", RoboError, NULL, &thrown);
        }
        if (out != NULL) {
            *out = result.value;
        }
        return status;
    }
    if (out != NULL) {
        *out = result.value;
    }
    return RoboOk;
}

Result JsRuntimeEvaluateProgram(JsRuntime *runtime, JsNode *program, JsValue *out)
{
    if (runtime == NULL || program == NULL) {
        return RoboInvalidArgument;
    }
    if (runtime->executing) {
        return RoboBusy;
    }
    if ((JsNodeKind)program->kind != RoboJsNodeProgram) {
        return RoboInvalidArgument;
    }
    JsRuntimeClearException(runtime);
    runtime->executing = 1;
    {
        Result status = JsRuntimeRunTree(runtime, program, out);

        runtime->executing = 0;
        return status;
    }
}

Result JsRuntimeEvaluate(JsRuntime *runtime,
                         const char *scriptName,
                         const char *source,
                         int32_t length,
                         JsValue *out)
{
    JsNode *program = NULL;
    Result status;

    if (runtime == NULL || source == NULL) {
        return RoboInvalidArgument;
    }
    if (runtime->executing) {
        return RoboBusy;
    }
    if (out != NULL) {
        *out = JsValueUndefined();
    }
    {
        JsParser parser;
        Result parseStatus;

        runtime->executing = 1;
        parseStatus = JsParserInit(&parser,
                                   runtime->heap,
                                   source,
                                   length,
                                   JsConfigValue(runtime->config.maxParseDepth,
                                                 (int32_t)RoboJsDefaultMaxParseDepth));
        if (parseStatus == RoboOk) {
            parseStatus = JsParserParseProgram(&parser, &program);
        }
        if (parseStatus != RoboOk) {
            const JsError *error = JsParserErrorOf(&parser);

            JsRuntimeReport(runtime, scriptName, RoboParseError, error, NULL);
            runtime->executing = 0;
            return parseStatus == RoboOutOfMemory ? RoboOutOfMemory : RoboParseError;
        }
        status = JsRuntimeRunTree(runtime, program, out);
        runtime->executing = 0;
        (void)scriptName;
        return status;
    }
}
