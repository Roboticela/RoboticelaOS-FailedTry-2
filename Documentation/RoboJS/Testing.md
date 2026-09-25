# RoboJS — Testing (16.15)

What proves this runtime, how to run it, and what it deliberately does not prove. The module's claim is
narrow and testable — *an independently implemented interpreter for a documented subset, over a host
interface it does not own* — so the suites are arranged as one file per claim, plus one file that drives the
real RoboHTML document through the real adapter.

---

## 1. The suites

| File | Cases | Owns |
|---|---|---|
| `Tests/RoboJS/SuiteRoboJS.c` | 23 | the language runtime: `js.runtime.*`, `js.lexer.*`, `js.parser.*`, `js.value.*`, `js.object.*`, `js.function.*`, `js.closure.*`, `js.exception.*`, `js.interpreter.*`, `js.limit.*`, `js.security.*`, `js.console.*` |
| `Tests/RoboJS/SuiteRoboJSHost.c` | 11 | the host seams over a **fixture** DOM: `js.dom.*`, `js.event.*`, `js.timer.*`, `js.loop.*`, `js.lifecycle.cleanup` |
| `Tests/RoboJS/SuiteRoboJSIntegration.c` | 11 | the **RoboHTML** adapter and the phase's demos: `js.integration.*` |
| `Tests/RoboJS/RoboJSTestSupport.h` | — | `static inline` helpers: the fixture DOM, the deterministic runtime, `JsTestNan`/`JsTestInfinity`, two exception shorthands |

**45 cases.** Everything goes through the public doors — `JsRuntimeEvaluate` for behaviour and the value API
for the rest — so a case cannot pass by reading the module's internals, and the split matters: the host
suite proves *the binding's* behaviour with no HTML parser, cascade or layout in the picture, and the
integration suite proves that the same binding drives the tree this repository actually ships.

## 2. The framework

`Tests/Runtime/Framework/RoboTest.h`, shared with the other 30-odd suites:

| Door | Use |
|---|---|
| `TestRegister("name", fn)` | register one case; the name is the filter key |
| `RoboCheck(cond)` | a boolean assertion, reported with file and line |
| `RoboCheckEqInt(a, b)` | an integer assertion that reports **both sides** — the reason a failure names the values rather than "not equal" |
| `RoboCheckStrEq(a, b)` | a string assertion with both strings in the report |
| `TestFail(fmt, …)` | a located failure with a message |
| `TestMain(argc, argv)` | the runner; `--filter=<prefix>` selects by name prefix |

The runner prints `Tests: N / Passed: N / Failed: N` and exits nonzero on a failure, which is what the
PowerShell entry points key on. A new case is one `static void TestX(void)` plus a `TestRegister` line in
the file that owns the claim.

## 3. Running it

```powershell
pwsh -NoProfile -File Scripts/Test/TestRoboJS.ps1 -Target Host            # 45 cases
pwsh -NoProfile -File Scripts/Test/TestRoboJS.ps1 -Target Host -Verify   # …plus the boundary source scans
pwsh -NoProfile -File Scripts/Build/BuildRoboJS.ps1 -Configuration Debug -BuildTests
```

`TestRoboJS.ps1` runs `Runtime/Tools/Build.ps1 -Module RoboJS -BuildTests -SkipGuest` and then
`Build/Runtime/RoboTest.host.exe --filter=js.`. `-Rebuild` forces a clean compile; `-Target Qemu` reports
that this part has no guest suite (`QemuTesting.md`). `BuildRoboJS.ps1` compiles the module's 178
production sources and nothing above it, which is the module boundary as a build command: a RoboJS source
that reached for a layout or a painter header would fail to compile there before a scan had to say so.

## 4. The fixtures

| Fixture | What it is |
|---|---|
| `JsTestRuntimeCreate(&config)` | a runtime with `deterministic = 1` (a counter only `JsRuntimeAdvanceClock` moves) and diagnostics on — so no case in this module depends on how long it took |
| `JsTestDom` / `JsTestDomHostInit` | a **32-node** tree in a struct, with its own attributes, children, parent links, `id`/`class`/`tagName` lookups — the second implementation of `JsDomHost`, and the reason the host suite needs no document |
| `JsTestNan` / `JsTestInfinity` | computed by a division from variables, never a constant: the guest libc has no `math.h`, and a folded division by zero would be a warning where a warning is an error |
| `JsTestExceptionMessage(runtime, buf, size)` | the pending exception's `message`, read through `JsRuntimePeekException` |
| `JsTestEvalExpectError(runtime, name, source, needle)` | evaluate, assert the evaluation failed, and assert the pending exception is a `name` whose message contains `needle` — one line per expected error rather than six |
| `DomParse` / `DomExpectCleanTree` (`Tests/Html/RoboHTMLDomTestSupport.h`) | a real RoboHTML document, and the tree invariants (no orphans, no bad links, no cycles, no sibling mismatches) asserted after every structural case |

`JsTestNan`/`JsTestInfinity` are also a statement about the module: `RoboJSMath.h` carries the same shim for
the same reason, so **neither the runtime nor its tests need a math library** — which is part of why the
module builds for the guest (`Memory.md` §5).

## 5. What the cases assert, by claim

| Claim | Case |
|---|---|
| the runtime is self-contained and its lifecycle releases what a page made | `js.runtime.lifecycle`, `js.lifecycle.cleanup` |
| two runtimes share nothing | `js.runtime.lifecycle` (second runtime, own heap, own globals) |
| the documented subset is what the lexer accepts | `js.lexer.vocabulary`, `js.lexer.tokens`, `js.lexer.errors` |
| a parse failure is located, and the AST printer is diffable | `js.parser.ast`, `js.runtime.dump` |
| the six value types, both equalities, and the conversions the interpreter needs | `js.value.types`, `js.value.equality`, `js.value.conversions`, `js.value.int32` |
| property attributes and arrays with their own element storage | `js.object.properties`, `js.object.arrays` |
| one callable shape for a script's function and a binding's, plus `new` | `js.function.declaration` |
| an environment outlives the call that made it, and two closures share one | `js.closure.lifetime` |
| `throw`/`try`/`catch`/`finally`, the five error kinds, a live runtime after an uncaught throw | `js.exception.handling` |
| statements, loops, operators and declarations | `js.interpreter.expressions`, `js.interpreter.loops`, `js.interpreter.declarations` |
| the budgets stop a runaway and leave the host usable | `js.limit.budgets` |
| the sandbox is the *absence* of host capability | `js.security.boundary`, `js.console.sink` |
| the DOM binding: interning, selectors, mutation, the pending case | `js.dom.install`, `js.dom.queries`, `js.dom.mutation`, `js.dom.lifecycle` |
| listeners, order, `preventDefault`, the bubble walk, deferred delivery | `js.event.binding`, `js.event.deferred` |
| a timer is a due record, on the caller's clock | `js.timer.schedule` |
| one boundary: drain, then one job; a posted job is not run inline | `js.loop.tasks`, `js.loop.reentrancy` |
| microtask ordering and the starvation cap | `js.loop.microtasks` |
| the capability table, **field by field** | `js.runtime.capabilities` |
| the RoboHTML adapter, both directions | `js.integration.*` (§6) |

`js.runtime.capabilities` deserves the callout: it reads every landed, absent and bounds field of
`JsCapabilities` and asserts each one, so a flag that stops being true **fails a test** rather than
misleading a consumer. The absent block is asserted in the same case as the landed block, on purpose.

## 6. The integration suite, and the boundary it asserts rather than works around

RoboHTML's tree builder finalizes a document when its input is exhausted; finalization closes **node
creation** (Phase 8 pins that refusal in `SuiteRoboHTMLMutation` and `SuiteRoboHTMLElement`, "pinned as a
test so the seam is a decision rather than an accident"). So the integration suite is arranged around what
that means rather than pretending otherwise:

| Case | Claim |
|---|---|
| `js.integration.attach` | the mount: `window`/`document`, and every wrapper's id resolved by `HtmlDocumentNodeById` — RoboHTML saying "yes, that is one of my nodes" rather than the suite believing a counter |
| `js.integration.read` | the read-only surface against RoboHTML's own answers |
| `js.integration.attributes` | attribute writes land in RoboHTML's storage, and the tree version does **not** move while `attributeWrites` does |
| `js.integration.structure` | rearrangement (move, insert, detach) with the parent links read back out of the tree |
| `js.integration.text_in_place` | a `textContent` write whose lone child is already a text node is one `HtmlTextSet`, not a construction |
| `js.integration.creation_refused` | the refusal is named, and the tree is proved **untouched** — the classification happens before anything is destroyed, which is the correctness requirement the case exists for |
| `js.integration.creation_open` | the whole `createElement → setAttribute → appendChild → textContent` path where construction *is* open (a document that was never parsed, and one `HtmlDocumentReset` reopened) |
| `js.integration.demo` | the phase's end-to-end page, with the `listener, microtask, timer` order |
| `js.integration.steps` | the demo's steps replayed at the grain a host suite can reach |
| `js.integration.lifecycle` | a destroyed document, then a fresh attach |
| `js.integration.determinism` | two generations, compared byte for byte |

## 7. The boundary scan

`Assert-RoboJSBoundaries` in `Runtime/Tools/Build.ps1`, run by `-Verify`, is the module's ownership rule as
a scan rather than as prose — because each of its three rules is one a well-meaning change would break:

| Rule | What it refuses |
|---|---|
| **one runtime** | a RoboJS source that includes the shell, a window, a display, a renderer, a painter, layout, CSS, the text layer, typography, input, the compositor, the font layer or the rasterizer — anything above this module's floor |
| **one DOM seam** | a `RoboHTML/` include anywhere but `RoboJSDomHtml.c` and the integration suite that proves the adapter, by *name* so a second file reaching for the tree still fails |
| **no second engine** | Chromium, Skia, V8, QuickJS, Duktape, MuJS or JerryScript as an include, in the module, its suites or its documents |

The reverse half is older than this module: the scans of the layers *below* RoboJS (`Assert-RoboLayoutBoundaries`,
`…Paint…`, `…Render…`, `…Text…`, `…Typography…`, `…Input…`, `…Compositor…)` each name `RoboJS` in their
forbidden-include pattern, so nothing below can reach up for a JavaScript engine either. Together the two
halves are the machine-checked form of "there is one runtime, and it sits in one place".

## 8. What is not proved here

- **No guest half.** There is no `ROBO_QEMU_JS_*` checkpoint and no `test-qemu` command for this module, so
  every case above runs on the host. `QemuTesting.md` states the absence rather than implying a suite.
- **No fuzz door.** Unlike Phases 12.21/12.22 (`RoboTextSelectionFuzz`, `RoboTextEditingFuzz`), this module
  ships no "drive it from arbitrary bytes" entry point; malformed *input* is covered by
  `js.lexer.errors`/`js.parser.ast` and malformed *states* by the lifecycle and limit cases. A fuzzer is
  named as a gap in `Limitations.md`.
- **No coverage instrument, no timing assertions and no performance budget.** Every case runs on the
  deterministic clock, precisely so that "how long it took" can never be part of an assertion.
- **No cross-check against another engine.** Comparing this runtime's answers with a real JavaScript
  implementation would make that engine's behaviour the specification, and the whole point of this phase is
  that the subset is documented here. The *language* cases assert the documentation, and
  `LanguageSubset.md` is the document they assert.
- **The pointer path.** §16.11's literal "a RoboInput pointer event reaches a click listener" is not
  reachable while RoboDesktop's pointer path delivers `RoboWindow` events; the end-to-end path proved here is
  a posted `JsEventRecord` → a listener → a DOM write read back out of RoboHTML (`Events.md` §5,
  `Limitations.md`).

---

**Change rule.** A change to any mechanism is not done until `TestRoboJS.ps1 -Target Host` passes, and then
the boundary suites the change touched: a DOM-adapter change also needs `TestHtml.ps1` (that adapter is the
only RoboJS code that crosses into RoboHTML) and `TestDesktop.ps1 -Target Host`; a change to the build lists
or the module boundary needs `-Verify`.
