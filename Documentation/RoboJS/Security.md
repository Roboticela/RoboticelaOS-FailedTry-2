# RoboJS — Security

What a script in this tree can reach, and what makes that a boundary rather than a convention.

The rule is one sentence: **a script reaches the outside world only through a binding this tree installed, and
this tree installs none.** `RoboJS.h` says it, `JsCapabilities` states it as five zeros, and
`js.security.boundary` checks it by name.

---

## 1. The capability zeros

| Flag | Why it is 0 |
|---|---|
| `hostFileSystem` | a script that can open a path makes every other rule advisory |
| `hostProcessAccess` | `process`, `exit`, `env`, spawning |
| `hostNetwork` | `fetch`, `XMLHttpRequest`, `WebSocket` |
| `hostEnvironmentVariables` | `environment` |
| `proxyAndReflect` | `Proxy` and `Reflect` are how a script attacks a *host's* invariants rather than its own |

Alongside them, `eval`, `Function` (the constructor that compiles a string) and `importScripts` are **absent**
rather than restricted. `eval` is the one absence that is a deliberate design choice rather than a
not-yet-implemented row: a build whose only job is to run a page's script has no reason to let that script compile
more, and a dynamically-compiled path is the one that cannot be audited by reading the source it will run.

A `typeof` for any of them is `"undefined"`, which is what `js.security.boundary` asserts — one check per name,
because "we did not install it" is the kind of fact that quietly stops being true.

---

## 2. The global object is the whole reachable surface

`JsRuntimeCreate` installs, and only installs:

| What | Members |
|---|---|
| the six value constructors' prototypes | `Object`, `String`, `Number`, `Boolean`, `Error` and the four error kinds |
| `Math` | `PI`, `abs`, `ceil`, `floor`, `round`, `pow`, `sqrt`, `min`, `max` |
| the prototype methods | `String.prototype` (`indexOf`, `slice`, `toUpperCase`, `toLowerCase`, `fromCharCode`, `length`), `Array.prototype` (`push`, `pop`, `join`, `indexOf`), `Function.prototype` (`call`, `apply`), `Object.keys`/`Object.create` |
| `parseInt` | the one free function |
| `console` | `log`, `warn`, `error` — routed to the host's sink |
| `RoboJS` | the version probe, and nothing else |
| a DOM binding, when a host installs one | `document`, `window` |

There is no `globalThis`, no `global`, no `self`, no `window` unless a host created one, and no `require`,
`module` or `exports`. A host that wants a name creates it with `JsRuntimeDefineGlobal` — which is the *only* way a
name with host behaviour comes into existence, and is therefore the one line a reviewer has to read.

`JsRuntimeGetGlobal` reads the global **object**, so it answers exactly what a script sees; a name that is not
there is `RoboNotFound` rather than an undefined value, which keeps "absent" distinguishable from "set to
undefined".

---

## 3. The DOM is the one host reach, and it is narrow

A page's script can read and mutate a document. The interesting question is what that is worth to an attacker,
and the answer is a property of the adapter's design rather than of a sandbox:

- **The adapter owns no node.** Every id it hands out is a RoboHTML node id, resolved through RoboHTML's registry.
  A wrapper cannot name a node that is not in the document, because the id space *is* the document's.
- **No path-like object crosses the seam.** There is no `open`, no `read`, no socket, and no host pointer in the
  vtable: a `JsDomHost` can only answer questions about nodes, and `JsDomHost` is the only host interface in the
  module.
- **A refused mutation is refused before it half-lands.** §3 of `DOMBindings.md`: the construction gate and the
  shape classification both run before the first write, and Phase 8's mutation paths reject an entire operation
  before touching a link. So a script cannot use a refused write to leave the tree inconsistent.
- **The document is not the filesystem.** `/System/Fonts/` is installed into the image by the build; a script has
  no door to it, no `Blob`/`File`/`URL`, and no `@font-face`.

---

## 4. Resource exhaustion is a limit, not a hang

A hostile script's cheapest attack is not a capability — it is time and memory. Every one of those has a number,
and the answer to exhaustion is a located `RoboJsErrorLimit` (a `RangeError` in script terms) rather than a
microtask or a callback that never completes:

| Attack | The number that stops it |
|---|---|
| `while (true) {}` | `maxSteps` (500 000), checked in the interpreter's step counter |
| deep recursion | `maxCallDepth` (64) |
| `function f() { setTimeout(f, 0); } f();` | `maxTimersPerCycle` (256) |
| a microtask that posts a microtask | `maxMicrotasksPerCycle` (4 096), the starvation cap |
| a task that posts a task | `maxTasksPerCycle` (4 096) inside one `JsLoopRunUntilIdle` |
| a queue grown by a listener | `maxQueueLength` (4 096) |
| an array grown by `push` | `maxPropertiesPerObject` (8 192) and the array's own length check |
| a string built by concatenation in a loop | `maxStringLength` (1 MiB) |
| `((((((…))))))` in the source | `maxParseDepth` (64) |
| allocation | the heap's chunked bump allocator, released by `JsHeapReset` |

Each is configurable — a config field of 0 means the default, and a host may pass a *smaller* number than the
default. A host may not pass a larger one by accident: the defaults are the published ceiling, and
`JsGetCapabilities` reports them so a caller can read a limit rather than hard-code one.

**The exhaustion does not poison the runtime.** After a step-limit failure the pending exception is readable and
clearable, the globals are intact, and the next evaluation runs — `js.limit.budgets` asserts it, and
`js.integration.steps` asserts it in the middle of a twenty-step checkpoint.

---

## 5. What is *not* claimed

- **No sandbox boundary against the host's own bugs.** This is not a process-isolation mechanism, a `seccomp`
  policy or a capability system. A host that hands a script a `JsDomHost` whose callbacks read files has given the
  script a file door; the module's job is to not *provide* one, not to prevent a host from building one.
- **No memory-safety claim beyond the language's own.** The module is C: a host that passes a bad pointer into
  `JsDomInstall` or a stale `JsRuntime *` gets undefined behaviour. `APIStability.md` states the lifetime rules
  that are the caller's half.
- **No side-channel story.** Timing (`JsClockFn`), memory-growth observation and error-message differences are all
  observable to a script that shares a runtime with another; they are not mitigated. This is a page-script runtime
  in one OS, not a browser's cross-origin model.
- **No `eval`, no `Function`, no dynamic import** — stated above, and named here so the absence is a decision
  rather than a gap someone "fixes" later.

---

## 6. Where a reviewer looks

| Question | Where the answer is |
|---|---|
| what can a script reach by name? | §2's table; `JsRuntimeInstallGlobals` in `RoboJSRuntime.c`; `js.security.boundary` |
| what did the host add? | every `JsRuntimeDefineGlobal` call site; a binding is one line |
| is a name reachable through a prototype? | the prototype chain's roots are created in `JsRuntimeCreate`, before any host runs |
| can a script hang the shell? | §4's table; `js.limit.budgets` |
| can a refused mutation leave the tree inconsistent? | `DOMBindings.md` §3; `DomExpectCleanTree` in the integration suites |
