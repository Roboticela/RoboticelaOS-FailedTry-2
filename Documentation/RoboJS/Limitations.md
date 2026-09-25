# RoboJS — Limitations

**Every gap in this module, in one place, each with its reason and the layer it belongs to.** Read it before
building on the runtime, and read `JsCapabilities` (`RoboJSVersion.h`) next to it: the capability table is the
same list as a machine-readable record, and `js.runtime.capabilities` asserts it field by field — so a gap that
silently closed would fail a test rather than go unnoticed.

Two kinds of row appear here, and they are not the same thing:

- **Absent** — the construct or door does not exist, and using it is a located `SyntaxError`, a `TypeError` or a
  `typeof == "undefined"`.
- **Divergent** — the door exists and behaves differently from the specification. §13 is that list, kept
  separate because a divergence is the kind of thing a ported page trips over.

---

## 1. Language: refused by name

A refused construct is a **located `SyntaxError`** from the lexer or the parser, never a half-implementation.
`LanguageSubset.md` §3–§5 is the vocabulary.

| Absent | Why it is not faked | Where it goes |
|---|---|---|
| `class`, `extends`, `super` | a class is a prototype object, a constructor, accessors and a private-environment rule — four mechanisms, each visible in its own right | a token, a node kind and an interpreter arm |
| `async`/`await`, generators (`function*`, `yield`) | both are a suspension model; without it a half-form would run eagerly and lie about order | an interpreter state machine |
| modules (`import`/`export`) | there is no module loader, no resolution and no cycle rule; a document's `<script>` is a *script* | a loader + the document layer |
| destructuring, rest/spread, default parameter values | the parser's pattern grammar plus the call door's argument shaping | the parser, then `Functions.md`'s one callable |
| template literals, tagged templates | a second string token with an embedded expression grammar | the lexer + parser |
| regular-expression literals | a regex engine is a module of its own (a parser, a matcher, a backtracking budget); a `RegExp` object without one is worse than none | a new module, `regexLiterals` is 0 |
| `for…in`, `for…of` | enumeration order and the iterator protocol are each their own contract | the parser + the object model |
| labels, labelled `break`/`continue` | a labelled jump target in the completion record | the parser + interpreter |
| `with`, `eval`, `new Function` | `Security.md` §1 — a dynamic-scope hole and a second parser entry point | never |
| optional chaining `?.`, nullish `??`, computed keys, getters/setters, `Symbol`, `BigInt`, typed arrays, `Proxy`/`Reflect`, weak collections | each is a mechanism with its own document; "partially supported" is not a category this tree uses | a token/node/engine arm each |
| `Object.freeze`/`seal`, `defineProperty`, property descriptors, accessors | the attribute model has the three flags; the *reflective* doors do not exist | the object model |

## 2. The built-in surface, completely

Defined in one file, `RoboJSBuiltins.c`, plus `console` and `document`/`window` from their installers. This is
the whole list a page can see:

| Global | Members |
|---|---|
| `Object` | `keys`, `create` |
| `Array` | **no global constructor** — array *literals* work and `Array.prototype` carries `push`, `pop`, `join`, `indexOf`, `slice`; there is no `Array.isArray`, no `new Array(n)` and no `map`/`filter`/`reduce`/`forEach`/`sort`/`concat`/`splice`/`shift`/`unshift` **as a door on the prototype** |
| `String` | `fromCharCode`; `toUpperCase`, `toLowerCase` on the prototype; no `charAt`, `split`, `replace`, `match`, `substring`, `trim`, `slice`, `indexOf`, `repeat`, `padStart` |
| `Number` | the numeric conversions; no `Number.isInteger`, no `toFixed`, no constants |
| `Boolean`, `Math` | `Math.floor/ceil/round/abs/sqrt/pow/min/max` and `PI`; no `random`, no `sin`/`cos`/trig, no `log`/`exp`, no `trunc`/`sign`, no `hypot` |
| errors | `Error`, `TypeError`, `RangeError`, `ReferenceError`, `SyntaxError` (`Exceptions.md`) |
| `globalThis`, `undefined`, `NaN`, `Infinity` | yes |
| `parseInt` | yes — `Number.parseFloat`, `Number.isNaN` and `isNaN` as a global are **absent** |
| `console` | `log`, `warn`, `error`, routed to the host's sink |
| `RoboJS` | the version/capability probe |
| scheduling | `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval`, `queueMicrotask` |
| DOM | `document`, `window` — only once a host installs the DOM binding |

`JSON`, `Date`, `RegExp`, `Promise`, `Map`, `Set`, `Array` as a constructor, `Intl`, `Math.random` and the
`String`/`Array` method sets are **absent as a group**, and the reason is one sentence: this build implements
what the phase's documented subset and the document layer need, and every one of those is a mechanism with its
own document rather than a line of glue.

## 3. Values

| Absent / limited | Why |
|---|---|
| `BigInt`, `Symbol`, typed arrays | no arithmetic path, no identity space, no element buffer — each a new value type |
| boxed primitives | `new Number(1)` produces an object, but there is no distinct boxed representation (`Values.md` §5) |
| reference counting, weak values | the heap is released as a whole (`Memory.md` §2) |
| a byte/step cost per allocation | a script cannot be charged for memory separately from `maxPropertiesPerObject`/`maxStringLength` |
| exact shortest round-tripping number printing | `JsNumberToString` uses `%g` with seventeen significant digits, trimmed; the true shortest form needs a big-number algorithm (`RoboJSValue.c`) |

## 4. Objects, arrays and scope

| Absent / limited | Why |
|---|---|
| non-string keys, symbols as keys, accessor properties | the property list is (name, value, attributes) |
| enumeration order beyond "insertion, integer-like keys in element order" | the same list |
| `Object.freeze`/`seal`, descriptors, `Proxy`/`Reflect` | §1 |
| a live `children`/`classList`/`dataset` view | each is a small view over the same host doors (`DOMBindings.md`) |
| **no temporal dead zone** | a `let`/`const` is hoisted to `undefined`; `x` before `let x` reads `undefined` rather than throwing, and `const x;` with no initializer is legal here |
| **no redeclaration error** | a re-executed `const x = 1` re-initializes rather than throwing, because the hoisting pass tolerates `RoboAlreadyExists` — the same tolerance a re-executed `var` and function declaration need |
| `for…in` | the parser does not accept it |

## 5. Functions

`arguments` (as an object, with its aliasing rules), rest parameters, default values, destructuring
parameters, `bind`, generators, `async` functions, `new.target`, getters/setters on a function, and function
names as first-class values (`fn.name` is the stored name for a declaration and empty for an anonymous
expression). Each *would* be a change to `Functions.md`'s one callable shape rather than a second one.

`Function.prototype` carries `call` and `apply` and nothing else (`bind` is absent).

## 6. Exceptions

No stack traces of any kind, no `Error.captureStackTrace`, no `AggregateError`, no `cause`, no custom error
subclasses (an error subclass needs prototype setup this build's object model can express but the error model
does not name), and `finally` interacting with a generator or an `async` function — neither exists. `eval` is
absent, which is also why there is no "syntax error at runtime" path: every syntax error is a *parse* failure
with a span.

## 7. The DOM binding

| Absent | Why |
|---|---|
| **a construction gate that reopens after a parse** | RoboHTML finalizes a document when its input is exhausted and closes node creation; two Phase 8 suites pin that refusal as a decision. The one-condition fix (`RoboHtmlNodeOriginDom` registering a node on a finalized document) is RoboHTML's document-lifecycle work, deliberately not made from Phase 16 |
| `innerHTML`/`outerHTML`, `insertAdjacentHTML` | the same gate: the parser would have to run *into* an existing tree |
| `MutationObserver`, `ShadowRoot`, `Range`, `Selection`, `cloneNode`, `importNode` | mechanisms of their own |
| `dataset`, `classList`, live `children` collections | small views over the same host doors |
| CSSOM beyond `el.style` | `getComputedStyle` needs RoboCSS and RoboLayout, not a binding |
| a second selector engine | `querySelector`/`querySelectorAll` implement a documented subset (`DOMBindings.md`); `:hover`, `::before` and the sibling combinators are not in it |
| `document.write`, `cookie`, `location`, `history`, `navigator` | a document/window model above this module's seam |

**The headline row:** a page's script can **read everything, mutate every attribute and rearrange the nodes a
document already has**; it can **create nodes only where construction is open** — a document that was never
parsed, or one `HtmlDocumentReset` reopened — and a `textContent` write that would have to create a node is
refused **before** anything is destroyed. `js.integration.creation_refused` and `…creation_open` assert both
halves.

## 8. Events, timers and the loop

- **No capture phase**, no `stopImmediatePropagation`, no `bubbles`/`cancelable`, no event classes, no
  `CustomEvent` (an event is a plain object, so a script can build one and `dispatchEvent` it), no
  `once`/`passive`, no listener de-duplication, no `defaultPrevented` property, no delegation helpers, no
  focus or pointer-capture semantics, and no device poller.
- **No `requestAnimationFrame`/`requestIdleCallback`** (a frame is the compositor's), no `setImmediate` (the
  task door exists), no timer nesting/clamping rules and no interval drift correction.
- **No `Date`** — a script cannot read a wall clock at all. Time reaches a script only as *ordering*.
- **No `Promise`** and nothing implied by it (`async`/`await`, thenable assimilation, unhandled-rejection
  reporting). `queueMicrotask` exists because 16.14 needs a microtask *source* that can be stated completely; a
  half-`Promise` is worse than none.
- **No prioritised queues, no work scheduling, no `postMessage`**, no `window.onerror`, and no starvation
  guard for tasks other than the microtask cap.
- **The loop is not reentrant**: a listener that calls `JsLoopRunOne` gets `RoboInvalidArgument` and must post
  a task or a microtask instead.

## 9. The host, the sandbox and the machine

`hostProcessAccess`, `hostFileSystem`, `hostNetwork`, `hostEnvironmentVariables` and `multiThreaded` are **0**.
There is no file, no socket, no environment, no subprocess, no worker, no `SharedArrayBuffer` and no `Atomics`
— and no *way to add one from a script*, which is the property `js.security.boundary` asserts. No JIT, no
bytecode, no compiler, no constant folding, no tail-call elimination (recursion is bounded by
`maxCallDepth`), no garbage collector (the heap is released as a whole), and **no thread-affinity check**: a
thread-mismatch cannot be detected portably in this freestanding libc, so `Threading.md` §1 is a rule a caller
keeps rather than one the module enforces.

## 10. Integration and evidence

| Gap | The shape of the work |
|---|---|
| **the shell cannot evaluate a page's script** | `Assert-RoboDesktopBoundaries` names `RoboJS` among the things a RoboDesktop file may not reach for. A shell that runs one changes that pattern and adds one bridge in the same commit (`Integration.md` §4) |
| **§16.11's pointer path** | RoboDesktop's pointer path delivers a `RoboWindow` event; the RoboInput → desktop wiring that would turn a point into a *node-targeted* `JsEventRecord` is Phase 13's open shell integration. The proved path is a posted record → a listener → a DOM write read back out of RoboHTML |
| **`<script>` in a document** | parsing a document's scripts, their order, `defer`/`async` and ready semantics is the document layer's work; RoboJS's part is `JsRuntimeEvaluate` and a `console` sink |
| **no guest checkpoint** | no `ROBO_QEMU_JS_*` marker, no `test-qemu` command, and `RoboJS` is not in the guest program's `RuntimeModules` (`QemuTesting.md` states the four things that would change together) |
| **no fuzz door** | unlike `RoboTextSelectionFuzz`/`RoboTextEditingFuzz`, this module ships no "drive it from arbitrary bytes" entry point; malformed input is covered by the lexer/parser error cases |
| **no coverage instrument, no timing assertion, no performance budget** | every case runs on the deterministic clock, so "how long it took" is never an assertion (`Testing.md` §8) |

## 11. Divergent from the specification, on purpose

Not absences — the door exists and answers differently. Each is asserted by a case, so the divergence is a
decision rather than drift.

| Divergence | The rule here | Why |
|---|---|---|
| **a top-level `const` is a non-writable property of the global object** | the specification keeps it in the global *lexical* environment and gives it no object property | "a global binding lives in both the object and the environment" is this runtime's one global rule; a second kind of global binding would be a second rule |
| **no temporal dead zone** | a hoisted `let`/`const` reads `undefined` | a separate uninitialized state is a second binding state; `RoboJsEnvKind` is stored per link so a later phase has the field it needs |
| **no redeclaration error** | re-executing a declaration re-initializes | the same tolerance a re-executed `var`/function declaration requires |
| **`ASIs` are partial** | `;` is optional before `}`, at end of input, and after a statement that ended with `}`; the full restricted-production rules are not implemented | a documented grammar (`Parser.md` §3) rather than a heuristic |
| **top-level `return` is a `SyntaxError`** | there is no module wrapper to return from | |
| **number printing** | `%g` with seventeen significant digits, trimmed | the shortest round-tripping form needs a big-number algorithm |
| **a mark/attribute write that RoboHTML refuses propagates as its own reason** | the module does not overwrite a pending exception with a generic one | a failed mutation must not be hidden as a stored property |

## 12. What a consumer may therefore rely on

Because of the rows above, these statements are true of every build of this module:

- **A script cannot reach the host by accident** — the global table is §2's list, and a host binding is the only
  way anything else appears.
- **A failure is a record, not a hang** — steps, call depth, parse depth, properties, string length, timers,
  tasks, queue length and microtasks are all counted, and the answer is a located error.
- **Nothing a page made outlives its page** — `JsRuntimeReset` cancels timers, clears both queues, drops
  listeners and wrappers and releases the heap as a whole.
- **A refused operation is named** — never approximated, and never a silent no-op.

---

**Related.** `LanguageSubset.md` (what *is* parsed), `Security.md` (the sandbox), `Memory.md` (the heap and its
cost), `Threading.md` (ownership), `DOMBindings.md` (the document boundary), `Testing.md` §8 (evidence gaps),
`Integration.md` §4 (gates), `QemuTesting.md` (the guest half that is not built).
