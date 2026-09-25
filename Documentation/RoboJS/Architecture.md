# RoboJS — Architecture (16.1)

The runtime, what it is made of, what it is bounded by, and what a caller may configure. This is the phase's
foundation document: every later document assumes the record described here.

---

## 1. One module, one runtime

`Runtime/Include/RoboJS/` is the interface and `Runtime/Source/RoboJS/` the implementation. `JsRuntime` is the
whole world: the heap, the global object, the prototype chain's roots, the scope chain, the pending exception,
the DOM seam, the listeners, the timers, the task queue, the microtask FIFO, the diagnostics, and the counters.

```c
typedef struct JsRuntime JsRuntime;      /* opaque; `RoboJSRuntime.h` is the only door */
```

A runtime is created by `JsRuntimeCreate(&config)`, and destroyed by `JsRuntimeDestroy`. `JsRuntimeReset` returns
the same runtime to its just-created state without freeing the handle, which is what a host shell does between
pages: `RoboJS.h`'s version probe and the builtins stay installed, everything a script made goes away.

**Nothing in the module is global state.** Two runtimes in one process share no heap chunk, no prototype, no
timer table and no macro; the only process-wide thing is `RoboBase`'s allocator, which the heap uses through
`JsHeap` and never directly.

---

## 2. The heap

`RoboJSHeap.h`/`RoboJSHeap.c`. A chunked bump allocator:

- Every allocation is `JsHeapAlloc`, every reallocation `JsHeapRealloc`, and every pointer a runtime hands out
  comes from the heap.
- Chunks are `heapChunkBytes` bytes (`RoboJsDefaultHeapChunkBytes`, 64 KiB) and are linked; a full chunk starts
  the next one.
- **There is no free and no collector.** `JsHeapReset` drops every chunk at once. That is why a reference cycle
  cannot leak — and why memory grows until a reset. `Memory.md` argues the trade and states the cost.

The alternative — a mark-and-sweep collector over a tree-walking interpreter — buys per-object reclamation at the
price of every pointer write becoming a potential root-liveness question. For a runtime whose whole working set is
one page's script, releasing as a whole is both simpler and *provably* cycle-free, and the failure mode (a script
that allocates a lot) is bounded by the same budgets as everything else.

---

## 3. Value, object, environment

Three headers, three jobs:

| Header | Job |
|---|---|
| `RoboJSValue.h` | `JsValue` — a **by-value** struct: a kind tag, flags, a `double`, a borrowed string with a length, and an owned `JsObject *`. No reference counting, no boxing of a small integer into a union. |
| `RoboJSObject.h` | `JsObject` — one struct with a `kind` (`Plain`, `Array`, `Function`, `Error`, `Host`), a `prototype`, an ordered property array, array elements, the callable slot, and the host slot. |
| `RoboJSEnvironment.h` | `JsEnv` — one link in the scope chain: a kind (`RoboJsEnvGlobal`, `RoboJsEnvFunction`, `RoboJsEnvBlock`), a parent, and its bindings. |

`JsValue` being by value is what makes the interpreter readable: an expression arm returns a `JsValue`, not a
pointer with an owner. The cost is that a string is a (pointer, length) pair rather than a header-prefixed
buffer, which `RoboJSValue.h` states.

---

## 4. Configuration and the defaults

`JsRuntimeConfigInit` zeroes a config and a zero field means *the default*, not *zero*. That is the rule that
keeps a config from being a second place to state a limit.

| Field | Default | What it bounds |
|---|---|---|
| `maxSteps` | 500 000 | execution steps per evaluation |
| `maxCallDepth` | 64 | nested calls |
| `maxMicrotasksPerCycle` | 4 096 | microtasks one boundary drains |
| `maxTimersPerCycle` | 256 | timer callbacks one cycle runs |
| `maxTasksPerCycle` | 4 096 | tasks `JsLoopRunUntilIdle` runs |
| `maxQueueLength` | 4 096 | tasks the queue holds |
| `maxTimerCount` | 1 024 | timer slots |
| `maxPropertiesPerObject` | 8 192 | properties one object holds |
| `maxStringLength` | 1 MiB | bytes one string holds |
| `maxParseDepth` | 64 | parser nesting |
| `heapChunkBytes` | 64 KiB | heap chunk size |

Five switches: `enableTimers`, `enableEventLoop`, `enableConsole`, `enableDiagnostics`, `deterministic`. A runtime
with no event loop is a supported configuration — it evaluates and answers, and `JsLoopRunOne` reports
`RoboNotSupported` — which is what a one-shot script wants.

Two host callbacks: a **clock** (`JsClockFn`) and a **console** (`JsConsoleFn`).

### The clock, and why a test never reads the wall

`deterministic = 1` makes the clock a counter `JsRuntimeAdvanceClock` moves. With `deterministic = 0` and no
`clock`, the runtime still uses its own monotonic counter that advances by one per evaluation — **never wall
time**. So no observable behaviour in this module depends on how long a test took; a test that wants ordering
advances the clock explicitly. A host that wants real time installs `JsClockFn`.

### The console

`console.log`/`warn`/`error` route to `console` when the host supplied one, and otherwise through `RoboLog` at the
matching level. `js.console.sink` proves the routing, because "the console went somewhere" should be a number, not
a claim.

---

## 5. Lifecycle and the four states

| State | `JsRuntimeIsInitialized` | `JsRuntimeIsExecuting` | What is legal |
|---|---|---|---|
| created | 1 | 0 | globals may be defined, scripts may be evaluated |
| executing | 1 | 1 | a native binding may call back; the loop may **not** be re-entered |
| held | 1 | 0 | the loop may be run; timers and listeners may be posted |
| destroyed | 0 | 0 | nothing; every door answers `RoboInvalidArgument` |

`JsRuntimeReset` releases the DOM wrappers, listeners, timers and tasks the script created, clears the pending
exception and the diagnostic ring, and re-installs the builtins — the state a navigation wants. A host that keeps
a pointer into a script's object across a reset is holding a freed pointer, which `APIStability.md` states as the
one thing a caller must not do.

**The loop is not reentrant.** `JsLoopRunOne` inside a listener is `RoboInvalidArgument` rather than a nested
drain: a task that wants to schedule work posts a task or a microtask. `Threading.md` is the ownership statement.

---

## 6. Evaluation, in three doors

```c
JsRuntimeEvaluate(runtime, name, source, length, out);   /* source text → value */
JsRuntimeParse(runtime, name, source, length, &program); /* source text → a tree, unevaluated */
JsRuntimeEvaluateProgram(runtime, program, out);         /* a tree → a value */
```

The `name` is the label a diagnostic reports (`"<page>"`, `"step-2"`), and it is why a suite can say *which* of a
checkpoint's twenty evaluations failed rather than only that one did.

`JsRuntimeEvaluate` answers a `Result`. A parse failure is `RoboError` with a `RoboJsErrorSyntax` diagnostic at a
located span; a script's own `throw` that reaches the top is also a non-`RoboOk` result, with the thrown value
left *pending*. The runtime is usable immediately after either: `js.exception.handling` and
`js.integration.steps` both assert that.

---

## 7. Counters and diagnostics

Diagnostics are a bounded ring of `JsDiag` records (`JsRuntimeDiagnosticCount`, `JsRuntimeDiagnosticAt`,
`JsRuntimeClearDiagnostics`), each carrying the domain, the message, the evaluation name, and a line/column. A
failure path that can say *where* it failed does; a caller that wants only the code reads the `Result`.

The runtime's counters answer the questions a test wants numbers for — steps spent, tasks run, microtasks drained,
timers fired, exceptions thrown and caught, DOM wrappers interned, refusals. They are reported rather than
inferred for the same reason the capability table exists: "the budget was enforced" should be a number, not a
claim.

---

## 8. The two host seams

**The DOM seam** is a `JsDomHost` vtable (`RoboJSDom.h`): a ~30-entry structure of callbacks a host implements
over whatever tree it has. `js.dom.*` exercises it with an in-memory fixture that has no parser and no layout, and
`RoboJSDomHtml.c` is the same vtable over RoboHTML. `DOMBindings.md` is the contract.

**The event seam** is a record, not a lock: `JsEventRecordInit` fills a fixed, copyable `JsEventRecord` and
`JsEventQueue` posts it. A device adapter — a pointer, a key, a resize, a custom event — constructs the same
record; nothing in the event layer polls a device. `Events.md` is the contract.

---

## 9. What this phase did not build, and where it belongs

`RoboJSVersion.h`'s absent block is the list. In one sentence each, with the home each would have:

- **`class`, `async`/`await`, generators, modules, destructuring, spread, template literals, optional chaining,
  `??`, `Symbol`, `BigInt`, typed arrays, regex literals, `Proxy`/`Reflect`.** Not lexed, so not "partial" — the
  lexer refuses them with a located `SyntaxError`. A later phase adds a token, a node kind, an evaluator arm.
- **Promises.** `queueMicrotask` exists because 16.14 needs a microtask source; `Promise` is a specification of
  its own and a half-Promise is worse than none.
- **A garbage collector, a JIT, a bytecode VM, multi-threading.** §2 and `Threading.md`.
- **Host capability access** — filesystem, process, network, environment. `Security.md`.
