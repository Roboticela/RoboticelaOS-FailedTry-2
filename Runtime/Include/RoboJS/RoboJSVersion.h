/*
 * RoboJS's version pin and capability table — Phase 16 in one record.
 *
 * ## Why the second block exists
 *
 * Every module in this tree states its capabilities the same way and for the same reason: **an
 * untested flag is a flag that stops being true**. So the table is two blocks, and the second is not
 * an apology — it is the list of things a consumer would otherwise assume. `asyncAwait`,
 * `classes`, `promises`, `modules` and `bigInt` are `0` because this build does not lex them;
 * `garbageCollector` is `0` because the heap is released as a whole; `hostProcessAccess` is `0`
 * because a script that could reach the filesystem would make the sandbox a comment. A flag whose
 * whole job is to say "not here" is worth more than a flag nobody reads.
 *
 * ## What landed
 *
 * All sixteen goals of the phase, at the level this tree can honour: a **minimal, independently
 * implemented** JavaScript runtime.
 *
 *   16.1  the module, `JsRuntime`, its configuration, its limits and its lifecycle
 *   16.2  a lexer and a recursive-descent parser for the documented subset
 *   16.3  an AST: one node struct, a shape table, and a printer a test can diff
 *   16.4  the six value types, their conversions, and both equalities
 *   16.5  objects, prototypes, property attributes, and arrays with their own element storage
 *   16.6  functions: one callable shape for a script's and a binding's, plus `new`
 *   16.7  closures: an environment chain whose links outlive the call that made them
 *   16.8  exceptions: `throw`/`try`/`catch`/`finally` and the five error kinds
 *   16.9  the interpreter: completion-based control flow under a step and depth budget
 *   16.10 DOM bindings over the existing RoboHTML DOM through a host interface
 *   16.11 event bindings: `addEventListener`, listener order, `preventDefault`, propagation
 *   16.12 timers: `setTimeout`/`setInterval` and their clear doors, on a caller's clock
 *   16.13 the event loop: one task at a time, timers by due time, then microtasks
 *   16.14 microtasks: a FIFO queue drained at every task boundary, with a starvation cap
 *   16.15 the suites — lexer, parser, AST, values, objects, functions, closures, exceptions,
 *         interpreter, DOM, events, timers, loop, microtasks, and one conformance suite
 *   16.16 the integration: one `JsDomHost` seam, the RoboHTML adapter, the runtime lifecycle
 *
 * ## What does not, and where it belongs
 *
 *   **The rest of the language.** `class`, `async`/`await`, generators, modules, destructuring,
 *   spread, template literals, optional chaining, `Proxy`, `Reflect`, `Symbol`, `BigInt` and typed
 *   arrays are not lexed, so they are not "partially supported" — the lexer refuses them with a
 *   located `SyntaxError`. A later phase adds a token, a node kind and an evaluator arm.
 *
 *   **Promises.** `queueMicrotask` exists because 16.14 needs a microtask source; `Promise` does not,
 *   because a Promise is a specification of its own and a half-Promise is worse than none.
 *
 *   **A garbage collector.** `garbageCollector` is 0 and `JsHeapReset` is the door: the heap is a
 *   chunked bump allocator released as a whole, which is why a cycle can never leak. The cost —
 *   memory grows until a reset — is stated in `Documentation/RoboJS/Memory.md` rather than hidden.
 *
 *   **A JIT, a bytecode compiler, and multi-threaded execution.** This is a tree-walking interpreter
 *   on one thread, and `Documentation/RoboJS/Threading.md` states the ownership: the runtime, its
 *   DOM wrappers, its listeners and its timers all belong to the thread that created it, and a device
 *   thread *posts*.
 *
 *   **Host capability access.** `hostProcessAccess`, `hostFileSystem` and `hostNetwork` are 0. A
 *   script reaches the outside world only through a binding this tree installed, which is what
 *   `Documentation/RoboJS/Security.md` and the security suite assert.
 */

#ifndef ROBO_JS_VERSION_H
#define ROBO_JS_VERSION_H

#include "RoboBase/RoboError.h"
#include "RoboJS/RoboJSTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RoboJsVersionMajor 1
#define RoboJsVersionMinor 0
#define RoboJsVersionPatch 0

/** `"1.0.0"` — the phase's narrative is in this file's comment, not in the string. */
const char *JsVersionString(void);

/** Numeric components; NULL @p out → `RoboInvalidArgument`. */
Result JsGetVersion(I32 *outMajor, I32 *outMinor, I32 *outPatch);

/**
 * What this build can do, in three blocks.
 *
 * The first block is what a caller may rely on. The second is what it may *not*, stated so a
 * consumer can refuse to build a path on it rather than discovering the absence at runtime. The
 * third is the bounds every fixed-size table in the module is allocated from, so a test reads a
 * limit instead of hard-coding one a later phase might move.
 */
typedef struct JsCapabilities {
    /* ---- Landed ---- */
    int lexer;                /* 16.2 */
    int parser;               /* 16.2 */
    int ast;                  /* 16.3 */
    int astPrinter;           /* 16.3 a diffable tree, complete or absent */
    int values;               /* 16.4 */
    int objects;              /* 16.5 */
    int prototypes;           /* 16.5 property lookup walks a chain */
    int propertyAttributes;   /* 16.5 writable/enumerable/configurable, enforced */
    int arrays;               /* 16.5 with their own element storage */
    int functions;            /* 16.6 user and native through one call door */
    int closures;             /* 16.7 an environment outlives its call */
    int exceptions;           /* 16.8 */
    int errorObjects;         /* 16.8 the five kinds with `name`/`message`/`line`/`column` */
    int interpreter;          /* 16.9 */
    int stepLimits;           /* 16.1 a runaway script is stopped, the host is not */
    int callDepthLimit;       /* 16.1 */
    int domBindings;          /* 16.10 over a caller-installed host */
    int querySelector;        /* 16.10 a selector subset matched over the host's tree */
    int eventBindings;        /* 16.11 */
    int eventPropagation;     /* 16.11 bubble phase with `preventDefault` */
    int timers;               /* 16.12 */
    int eventLoop;            /* 16.13 */
    int microtasks;           /* 16.14 */
    int microtaskStarvationCap; /* 16.14 a chain that never ends is cut, not run forever */
    int console;              /* console.log/warn/error routed to the host's sink */
    int diagnostics;          /* a structured record per failure */
    int deterministicClock;   /* a fake clock a test advances by hand */
    int hostDomAdapter;       /* the RoboHTML adapter ships in this build */
    int lifecycle;            /* create/reset/destroy release timers, listeners and memory */
    int apiFrozen;            /* 25: the public surface is pinned */

    /* ---- Deliberately absent ---- */
    int classes;
    int asyncAwait;
    int generators;
    int modules;
    int destructuring;
    int spreadAndRest;
    int templateLiterals;
    int optionalChaining;
    int nullishCoalescing;
    int symbols;
    int bigInt;
    int typedArrays;
    int regexLiterals;
    int proxyAndReflect;
    int promises;
    int weakCollections;
    int garbageCollector;
    int jit;
    int bytecodeVm;
    int multiThreaded;
    int hostFileSystem;
    int hostProcessAccess;
    int hostNetwork;
    int hostEnvironmentVariables;

    /* ---- Bounds ---- */
    unsigned maxSteps;              /* `RoboJsDefaultMaxSteps` */
    unsigned defaultMaxCallDepth;
    unsigned maxMicrotasksPerCycle;
    unsigned maxTimersPerCycle;
    unsigned maxTasksPerCycle;
    unsigned maxQueueLength;
    unsigned maxTimerCount;
    unsigned maxPropertiesPerObject;
    unsigned maxStringLength;
    unsigned maxParseDepth;
    unsigned heapChunkBytes;
    /** Token kinds, node kinds and error kinds, so a consumer can size a name table. */
    unsigned tokenKindCount;
    unsigned nodeKindCount;
    unsigned errorKindCount;
    /** The JavaScript subset, as one line a caller may print. */
    const char *languageSubset;
} JsCapabilities;

/** Fill @p out from this build. NULL → `RoboInvalidArgument`. */
Result JsGetCapabilities(JsCapabilities *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_JS_VERSION_H */
