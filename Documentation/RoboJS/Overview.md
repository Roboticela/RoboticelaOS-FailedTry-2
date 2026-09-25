# RoboJS — Overview

The one place the whole JavaScript subsystem is described end to end. Every other document under
`Documentation/RoboJS/` argues one mechanism and its limitations; this one is the map that says how the pieces
fit, what a consumer may rely on, and where a new piece belongs. It was written in **Phase 16.16**, which wired
the runtime to the document layer and froze its public surface.

> **There is one JavaScript runtime in RoboticelaOS.** It is written from the specification language subset
> documented here, not derived from any existing engine: no V8, no Node, no QuickJS, no embedded interpreter is
> a build or link dependency, and none is vendored. A second evaluation of a script — a host shell decoding a
> page's `<script>`, a layout pass running its own `typeof`, a test harness with its own evaluator — is a defect,
> not a shortcut.

---

## 1. The pipeline

```text
UTF-8 JavaScript source
      ↓  RoboJSLexer        tokens, each with a source span
tokens
      ↓  RoboJSParser       a recursive-descent parse under a depth budget
      ↓  RoboJSAst          one node struct, a shape table, and a printer
JsAst (JsNodeList)
      ↓  RoboJSInterpreter  statement completion: Normal / Break / Continue / Return / Throw
JsValue / JsObject / JsEnv
      ↓  RoboJSDom          the DOM a script sees — a host interface (JsDomHost), not a DOM
      ↓  RoboJSEvent        listeners, the event object, the input path
      ↓  RoboJSTimer        setTimeout / setInterval and their clear doors, on a caller's clock
      ↓  RoboJSEventLoop    one task at a time, timers by due time, then microtasks
host callbacks
      ↓  RoboJSDomHtml      the shipped adapter over this tree's own RoboHTML DOM
RoboHTML
```

**The arrows only point down, and only the last one crosses a module boundary.** RoboJS includes `RoboBase`,
`RoboMem` and the C standard library and nothing else; `RoboJSDomHtml.c`/`.h` are the *only* files in the module
that include a RoboHTML header, and a RoboJS consumer that wants the DOM binding includes both headers by name.
That is what `RoboJS.h`'s comment states and what `Security.md` asserts — and both halves are a source scan
(`Assert-RoboJSBoundaries`, `Testing.md` §7) rather than a promise.

---

## 2. The two seams a consumer uses

| Need | Door |
|---|---|
| run a script | `RoboJSRuntime.h` — `JsRuntimeCreate`, `JsRuntimeEvaluate`, `JsRuntimeGetGlobal`, `JsRuntimeDestroy` |
| attach a real DOM | `RoboJSDom.h` — `JsDomInstall(runtime, &host)` with a `JsDomHost` vtable |
| the shipped DOM | `RoboJSDomHtml.h` — `JsDomHtmlAttach(runtime, documentId)`, plus `JsDomHtmlGetCounters` |
| feed an input event | `RoboJSEvent.h` — `JsEventRecordInit`, `JsEventQueue` |
| drive the loop | `RoboJSEventLoop.h` — `JsLoopRunOne`, `JsLoopRunUntilIdle`, `JsLoopPostMicrotask` |
| timers directly | `RoboJSTimer.h` — `JsTimerSet`, `JsTimerClear` |
| what this build does | `RoboJSVersion.h` — `JsGetCapabilities` |

Everything else in `Runtime/Include/RoboJS/` is either an *engine* behind the seam (`RoboJSLexer.h`,
`RoboJSParser.h`, `RoboJSAst.h`, `RoboJSInterpreter.h`, `RoboJSHeap.h`) or a *value-model* header a binding
author needs (`RoboJSValue.h`, `RoboJSObject.h`, `RoboJSFunction.h`, `RoboJSEnvironment.h`,
`RoboJSException.h`). They are public because the suites and the module's own sources are their callers.

---

## 3. What each layer owns, and what it may not grow

| Layer | Owns | Must never |
|---|---|---|
| RoboJS lexer/parser/AST (16.2, 16.3) | the token vocabulary, the grammar, one node struct and its printer | evaluate, allocate an object, or know a DOM |
| RoboJS value model (16.4–16.7) | `JsValue`, `JsObject`, `JsFunction`, `JsEnv`, the heap | parse source, or hold a host handle it did not create |
| RoboJS interpreter (16.9) | statement completion, the scope chain, operators, calls | own a socket, a file, or a thread |
| RoboJS exceptions (16.8) | the five error kinds, `throw`/`catch`/`finally`, the pending-exception slot | print, log, or write a file |
| **RoboJSDom (16.10)** | the `JsDomHost` vtable, wrappers, the selector subset, the native binding set | parse HTML, compute layout, or cache a document |
| **RoboJSDomHtml** | the *one* adapter from that vtable to RoboHTML | add a second DOM, or keep a node registry of its own |
| RoboJSEvent (16.11) | `JsEventRecord`, listener lists, propagation, `preventDefault` | poll input, own a window, or hit-test |
| RoboJSTimer / RoboJSEventLoop (16.12–16.14) | the timer table, the task queue, the microtask FIFO, the budgets | read a real clock unless the host gave it one, or spawn a thread |
| RoboDesktop / a shell | choosing to run a script, and its `console` sink | evaluate anything itself, or decode a UTF-8 script body |
| RoboHTML | documents, nodes, attributes, the construction gate | run a script, or know a `JsValue` |

---

## 4. The rules, and which ones a build checks

| Rule | How it is kept |
|---|---|
| **one runtime** — no second evaluator, lexer, parser, value model or event loop anywhere in the tree | `Assert-RoboJSBoundaries` (three rules, run by `-Verify`) in one direction, and the scans of the layers below naming `RoboJS` in the other — `Testing.md` §7 |
| **one DOM seam** — a script reaches a document only through `JsDomHost` | `RoboJS.h` includes no RoboHTML header; `RoboJSDomHtml.c`/`.h` are the only crossers |
| **no host capability by name** | `js.security.boundary` reads the global table and asserts that no host function is reachable |
| **a limit is a limit** — every unbounded input has a number | `js.limit.budgets`; the numbers are `RoboJsDefault*` and are published through `JsGetCapabilities` |
| **an absence is stated** | the capability table's second block, asserted field by field in `js.runtime.capabilities` |
| **a diagnostic is a record, not a printf** | `JsRuntimeDiagnostic*`, complete-or-absent writers |
| **no engine work above the seam** | a consumer calls `JsRuntimeEvaluate`/the loop and never a lexer or an evaluator arm |

---

## 5. Consequences a consumer can rely on

- **A script cannot reach the host by accident.** The global object carries the standard builtins, `console` and
  `RoboJS` (the version probe) and nothing else; a host installs what it wants. A `typeof` for a host name is
  `"undefined"` until a binding creates it.
- **A host is never asked to be reentrant.** A native binding may call back into the interpreter, but the loop
  runs one task at a time, and a listener that posts work posts it as a *task* or a *microtask*, never by
  recursing into `JsLoopRunOne`.
- **An exception is a value, and the runtime survives one.** `JsRuntimeEvaluate` answers a `Result`; the pending
  exception is readable (`JsRuntimePeekException`), clearable (`JsRuntimeClearException`) and does not poison the
  next evaluation.
- **A budget is enforced, not advisory.** Steps, call depth, parse depth, timers, tasks, queue length, properties
  and string length are all counted, and the answer to exhaustion is a `RoboJsErrorLimit` at a located span —
  never a hang.
- **Memory is released as a whole.** `JsHeapReset` drops every chunk; there is no GC and no per-object free, so a
  reference cycle cannot leak and memory grows until a reset. `Memory.md` states the cost.
- **The DOM binding does not lie about ownership.** A node a script mutates *is* a RoboHTML node, resolved by
  RoboHTML's own registry; and where RoboHTML's model cannot represent an operation, the operation is refused
  with a named reason rather than approximated. `DOMBindings.md` is that boundary.

---

## 6. Where a change belongs

| A change | Where it goes |
|---|---|
| a new keyword or operator | `RoboJSLexer.c` + a token, `RoboJSParser.c`, a node kind, an interpreter arm, and `LanguageSubset.md` |
| a new built-in function | `RoboJSBuiltins.c` (or the owning prototype), plus a case in the suite that owns the shape |
| a new DOM method | `RoboJSDom.c` (host-agnostic) if a host can answer it, or `RoboJSDomHtml.c` if only this tree's DOM can |
| an event or an input shape | `RoboJSEvent.c`'s record + the vocabulary in `RoboJSTypes.h`; a device adapts *into* a record |
| a scheduling rule | `RoboJSEventLoop.c` — and `EventLoop.md`/`Microtasks.md` in the same change |
| a new limit | `RoboJSTypes.h`'s `RoboJsDefault*`, published through `JsGetCapabilities`, asserted in `js.limit.budgets` |
| a shell's script execution | RoboDesktop: `JsRuntimeEvaluate` and a `console` sink — never a second evaluator |

---

## 7. Evidence

| Artifact | What it shows |
|---|---|
| `Build/Runtime/RoboTest.host.exe --filter=js.` | **45 cases**: `js.runtime.*`, `js.lexer.*`, `js.parser.*`, `js.value.*`, `js.object.*`, `js.function.*`, `js.closure.*`, `js.exception.*`, `js.interpreter.*`, `js.limit.*`, `js.security.*`, `js.console.*`, `js.dom.*`, `js.event.*`, `js.timer.*`, `js.loop.*`, `js.lifecycle.*`, `js.integration.*` |
| `Tests/RoboJS/SuiteRoboJSIntegration.c` | the RoboHTML path: a wrapper's id resolved by `HtmlDocumentNodeById`, a mutation read back through the tree, and the construction boundary proved in both directions |
| `Assert-RoboJSBoundaries` (`-Verify`) | the ownership rules as a source scan: one runtime, one DOM seam, no second engine |
| **no guest checkpoint** | there is no `ROBO_QEMU_JS_*` marker and no `test-qemu` command for this module — the runtime is written to be guest-buildable, and `QemuTesting.md` states the absence rather than implying a suite |

A change to any mechanism above is not done until `TestRoboJS.ps1 -Target Host` passes, and then the boundary
suites the change touches. A change to the DOM adapter also needs `TestHtml.ps1` (the adapter is the only RoboJS
code that crosses into RoboHTML) and `TestDesktop.ps1 -Target Host`; a change to the module's includes, its link
list or its suites needs `-Verify` as well, because that is what runs the scan.

---

## 8. Documents

`Architecture.md` (16.1 — the runtime, the heap, the limits, the lifecycle) · `LanguageSubset.md` (16.2) ·
`Lexer.md`, `Parser.md`, `Ast.md` (16.2, 16.3) · `Values.md` (16.4) · `Objects.md` (16.5) · `Functions.md`
(16.6) · `Closures.md` (16.7) · `Exceptions.md` (16.8) · `Interpreter.md` (16.9) · `DOMBindings.md` (16.10) ·
`Events.md` (16.11) · `Timers.md` (16.12) · `EventLoop.md` (16.13) · `Microtasks.md` (16.14) · `Testing.md`
(16.15) · `Integration.md` (16.16) · `QemuTesting.md` · `Security.md` · `Memory.md` · `Threading.md` ·
`APIStability.md` · `Limitations.md` · this file.

The three *how-to-read-me* documents are `Overview.md` (the map), `APIStability.md` (what a consumer may rely on)
and `Limitations.md` (every gap, with the layer it belongs to). A consumer that reads only one of them should
read `Limitations.md` first.
