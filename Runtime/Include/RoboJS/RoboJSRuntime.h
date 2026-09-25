/*
 * RoboJS runtime (Phase 16.1) — the owner of a heap, a global object, an event loop and a clock.
 *
 * ## One runtime per document, and nothing global
 *
 * `JsRuntime` is a struct, not a singleton. Two runtimes share no mutable state: each has its own
 * heap, its own global object and prototype chain, its own timers, its own task queue and its own
 * diagnostics. That is what makes "a destroyed window's script cannot touch the next window's DOM"
 * a property of the design rather than a promise, and it is why `input`/`document` in one runtime are
 * different objects from `input`/`document` in another. `rubik`-style global mutable state is
 * deliberately absent.
 *
 * ## Lifecycle
 *
 * `create` → `reset` (zero or more) → `destroy`. `JsRuntimeReset` is the one reclamation door: it
 * frees every allocation in the heap, cancels every timer, drops every listener, empties both queues
 * and rebuilds the global object and prototypes. The configuration survives a reset, because the
 * configuration is the caller's policy and the heap is the script's.
 *
 * ## What a runtime holds, and why each is a caller-visible door
 *
 *   the **heap** (`JsRuntimeHeap`) — every value, object, environment and AST node;
 *   the **global object** and **global environment** — `window`'s and `document`'s home;
 *   the **prototype chain roots** — `Object.prototype`, `Array.prototype`, `Function.prototype`,
 *   `Error.prototype`, `String.prototype`, so a binding registers a method on the chain its values
 *   already have;
 *   the **event loop**, the **task queue**, the **timer table** and the **microtask queue**;
 *   the **DOM host** and its wrapper table (16.10), which may be absent — a headless runtime is a
 *   supported configuration, not a degenerate one;
 *   the **clock** — either the host's monotonic milliseconds or, in deterministic mode, a counter a
 *   test advances by hand (`JsRuntimeAdvanceClock`);
 *   the **diagnostics** ring — a structured record per failure, capped and readable index by index.
 *
 * ## Evaluation is one door, and it reports
 *
 * `JsRuntimeEvaluate` lexes, parses and runs. It answers `RoboOk` when the script ran to completion
 * (whatever value it produced, including `undefined`), and a non-OK result **with a diagnostic
 * recorded** when it did not: `RoboParseError` for a syntax error, `RoboJsErrorLimit`-shaped
 * `RoboRangeError`-equivalent for a limit, and `RoboError` for an uncaught throw — the thrown value
 * readable through `JsRuntimeTakeException`. `JsRuntimeEvaluateProgram` is the same door for a tree
 * a caller already parsed, which is what an integration test and a cached script both want.
 */

#ifndef ROBO_JS_RUNTIME_H
#define ROBO_JS_RUNTIME_H

#include "RoboJS/RoboJSEnvironment.h"
#include "RoboJS/RoboJSHeap.h"
#include "RoboJS/RoboJSObject.h"
#include "RoboJS/RoboJSTypes.h"
#include "RoboJS/RoboJSValue.h"
#include "RoboJS/RoboJSVersion.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The runtime's clock: monotonic milliseconds. A host installs one; the deterministic mode does
 *  not use it at all. */
typedef uint64_t (*JsClockFn)(void *userdata);

/** A console sink. @p level is 0 for `log`, 1 for `warn`, 2 for `error`; the text is the joined,
 *  already-stringified arguments. */
typedef void (*JsConsoleFn)(void *userdata, int level, const char *text, int32_t length);

/** `JsRuntimeConfig` field states, so a caller can read one back without a second struct. */
typedef enum JsDiagLevel {
    RoboJsDiagInfo = 0,
    RoboJsDiagWarn = 1,
    RoboJsDiagError = 2
} JsDiagLevel;

/** One diagnostic record: a severity, a message, and where it happened. */
typedef struct JsDiag {
    JsDiagLevel level;
    JsErrorKind kind;
    int32_t line;
    int32_t column;
    /** The script's name, when the caller supplied one; else `""`. */
    const char *script;
    char message[256];
} JsDiag;

/** One runtime. Its layout is private to the implementation (`Source/RoboJS/RoboJSInternal.h`). */
typedef struct JsRuntimeConfig {
    /** Execution steps per evaluation; 0 → `RoboJsDefaultMaxSteps`. */
    int32_t maxSteps;
    /** Nested calls; 0 → `RoboJsDefaultMaxCallDepth`. */
    int32_t maxCallDepth;
    /** Microtasks one boundary will drain; 0 → `RoboJsDefaultMaxMicrotasks`. */
    int32_t maxMicrotasksPerCycle;
    /** Timer callbacks one cycle will run; 0 → `RoboJsDefaultMaxTimersPerCycle`. */
    int32_t maxTimersPerCycle;
    /** Tasks `JsLoopRunUntilIdle` will run; 0 → `RoboJsDefaultMaxTasksPerCycle`. */
    int32_t maxTasksPerCycle;
    /** Tasks the queue will hold; 0 → `RoboJsDefaultMaxQueueLength`. */
    int32_t maxQueueLength;
    /** Timer slots; 0 → `RoboJsDefaultMaxTimerCount`. */
    int32_t maxTimerCount;
    /** Properties one object may hold; 0 → `RoboJsDefaultMaxProperties`. */
    int32_t maxPropertiesPerObject;
    /** Bytes one string may hold; 0 → `RoboJsDefaultMaxStringLength`. */
    int32_t maxStringLength;
    /** Parser nesting; 0 → `RoboJsDefaultMaxParseDepth`. */
    int32_t maxParseDepth;
    /** Heap chunk size; 0 → `RoboJsDefaultHeapChunkBytes`. */
    int32_t heapChunkBytes;

    /** Install `setTimeout` and friends. */
    int enableTimers;
    /** Build the event loop and the microtask queue. A runtime without a loop can still evaluate. */
    int enableEventLoop;
    /** Install `console`. */
    int enableConsole;
    /** Keep diagnostics. A runtime without them still reports through the result code. */
    int enableDiagnostics;
    /** The clock is a counter `JsRuntimeAdvanceClock` moves, so tests need no wall clock. */
    int deterministic;

    /** The host clock, used when @p deterministic is 0. NULL means "the runtime's own monotonic
     *  counter", which advances by one per evaluation — never by wall time, so no observable
     *  behavior depends on how long a test took. */
    JsClockFn clock;
    void *clockUserdata;
    /** Where `console` writes. NULL routes through `RoboLog` at the matching level. */
    JsConsoleFn console;
    void *consoleUserdata;
} JsRuntimeConfig;

/** Fill @p config with the defaults (everything enabled, no host clock, no host console). */
void JsRuntimeConfigInit(JsRuntimeConfig *config);

/**
 * Create a runtime.
 * @return NULL when a required allocation fails; otherwise a runtime in the `Ready` state with its
 *         global object, its prototypes and its built-ins installed.
 */
JsRuntime *JsRuntimeCreate(const JsRuntimeConfig *config);

/** Create with the default configuration (a convenience for a test and for a document with no
 *  opinion). */
JsRuntime *JsRuntimeCreateDefault(void);

/** Release everything: heap, queues, timers, listeners, prototypes. Safe on NULL. */
void JsRuntimeDestroy(JsRuntime *runtime);

/**
 * Reclaim state without destroying the runtime: every allocation is released, every timer and
 * listener dropped, both queues emptied, and the global object and prototypes rebuilt from the
 * configuration. The configuration itself is kept.
 *
 * @return RoboOk; RoboInvalidArgument on NULL.
 */
Result JsRuntimeReset(JsRuntime *runtime);

/** 1 when @p runtime is a live, initialised runtime. */
int JsRuntimeIsInitialized(JsRuntime *runtime);

/** @return 1 when a script is currently running on this runtime (so a re-entrant evaluate is a
 *          `RoboBusy` rather than a corrupted environment). */
int JsRuntimeIsExecuting(JsRuntime *runtime);

/* ------------------------------------------------------------------ */
/* Evaluation                                                          */
/* ------------------------------------------------------------------ */

/**
 * Lex, parse and run @p source (length < 0 → strlen) as a program in the global scope.
 *
 * @param runtime   the runtime;
 * @param scriptName  a name for diagnostics (may be NULL → `""`);
 * @param source    the text;
 * @param length    bytes, or a negative value for "NUL-terminated";
 * @param out       the program's completion value; may be NULL.
 *
 * @return RoboOk when the program ran to completion. `RoboParseError` for a syntax error,
 *         `RoboRangeError`-shaped limits answer `RoboError` with an `RoboJsErrorLimit` diagnostic —
 *         see the header comment for the exact mapping — and an uncaught throw answers
 *         `RoboError`. A diagnostic is recorded for every failure when diagnostics are enabled.
 *         `RoboBusy` when a script is already running.
 */
Result JsRuntimeEvaluate(JsRuntime *runtime,
                         const char *scriptName,
                         const char *source,
                         int32_t length,
                         JsValue *out);

/** Run an already-parsed program (`RoboJsNodeProgram`). The same reporting as
 *  `JsRuntimeEvaluate` without the lexing and parsing. */
Result JsRuntimeEvaluateProgram(JsRuntime *runtime, JsNode *program, JsValue *out);

/** Parse @p source without running it. The caller owns the tree until the runtime is reset. */
Result JsRuntimeParse(JsRuntime *runtime,
                      const char *source,
                      int32_t length,
                      JsNode **out);

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

JsHeap *JsRuntimeHeap(JsRuntime *runtime);
/** The global object (`Object`-shaped): a property of it is a global name. */
JsObject *JsRuntimeGlobalObject(JsRuntime *runtime);
/** The global environment: the outermost link of every scope chain. */
JsEnv *JsRuntimeGlobalEnv(JsRuntime *runtime);
JsObject *JsRuntimeObjectPrototype(JsRuntime *runtime);
JsObject *JsRuntimeArrayPrototype(JsRuntime *runtime);
JsObject *JsRuntimeFunctionPrototype(JsRuntime *runtime);
JsObject *JsRuntimeErrorPrototype(JsRuntime *runtime);
JsObject *JsRuntimeStringPrototype(JsRuntime *runtime);

/** Define a global binding: `name` becomes a property of the global object **and** a binding in the
 *  global environment, so both `foo` and `this.foo` see one value. */
Result JsRuntimeDefineGlobal(JsRuntime *runtime,
                             const char *name,
                             const JsValue *value);

/** Read a global by name. `RoboNotFound` when it does not exist. */
Result JsRuntimeGetGlobal(JsRuntime *runtime, const char *name, JsValue *out);

/**
 * The inverse of `JsRuntimeDefineGlobal`: drop @p name from the global object **and** from the global
 * environment, so a bare name stops resolving at the same moment the property disappears. Removing
 * only one of the two would leave the pair `JsRuntimeDefineGlobal` exists to keep together.
 *
 * @return RoboOk; RoboNotFound when nothing declared it; RoboInvalidArgument for a NULL argument.
 */
Result JsRuntimeDeleteGlobal(JsRuntime *runtime, const char *name);

/** Install the standard bindings (`console`, `setTimeout` and friends, `queueMicrotask`, `Object`,
 *  `Array`, `Math`-subset, `JSON`-less). Idempotent. Called by `JsRuntimeCreate`. */
Result JsRuntimeInstallGlobals(JsRuntime *runtime);

/** The clock's current value in milliseconds (the deterministic counter, or the host's). */
uint64_t JsRuntimeNowMs(JsRuntime *runtime);

/**
 * Move the deterministic clock forward by @p deltaMs and answer the new value. Only meaningful in
 * deterministic mode; a runtime with a host clock refuses with `RoboNotSupported` rather than
 * silently doing nothing.
 */
Result JsRuntimeAdvanceClock(JsRuntime *runtime, uint64_t deltaMs);

/* ------------------------------------------------------------------ */
/* Counters and diagnostics                                            */
/* ------------------------------------------------------------------ */

/** Steps executed by the current (or the last completed) evaluation. */
int32_t JsRuntimeStepCount(JsRuntime *runtime);
/** Deepest call the runtime has reached. */
int32_t JsRuntimeMaxDepthReached(JsRuntime *runtime);
/** Programs completed since creation or the last reset. */
int32_t JsRuntimeEvaluationCount(JsRuntime *runtime);

/** Number of stored diagnostics. */
int32_t JsRuntimeDiagnosticCount(JsRuntime *runtime);
/** Read diagnostic @p index (0 is the oldest). RoboNotFound when out of range. */
Result JsRuntimeDiagnosticAt(JsRuntime *runtime, int32_t index, JsDiag *out);
/** Drop every stored diagnostic. */
void JsRuntimeClearDiagnostics(JsRuntime *runtime);
/** Record one diagnostic by hand — the door an integration layer uses to note what it did. */
void JsRuntimeAddDiagnostic(JsRuntime *runtime,
                            JsDiagLevel level,
                            JsErrorKind kind,
                            const char *script,
                            const JsSpan *span,
                            const char *format,
                            ...);

/**
 * Write a complete state dump — version, counters, limits, queue depths, timer counts, listener
 * count, heap bytes — into @p buffer, and answer the bytes a complete dump needs. The module's
 * complete-or-absent rule: when it does not fit, the buffer is left empty and the return value says
 * how big it had to be.
 */
int32_t JsRuntimeWrite(const JsRuntime *runtime, char *buffer, size_t capacity);

/* ------------------------------------------------------------------ */
/* Host seams                                                          */
/* ------------------------------------------------------------------ */

/** Install a console sink (overriding the configuration's). NULL clears back to the configured one. */
void JsRuntimeSetConsole(JsRuntime *runtime, JsConsoleFn console, void *userdata);

/** Run the runtime's event loop until it has no work left (16.13). A runtime with no loop answers
 *  `RoboNotSupported`. */
Result JsRuntimeRunUntilIdle(JsRuntime *runtime);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_JS_RUNTIME_H */
