# RoboJS — Integration (16.16)

What Phase 16 actually wired to the phases under it, what it deliberately did **not** wire, and the exact
shape of each thing that is still a seam. This document is the honest half of the phase: the module is
complete on its own terms, and the *integration* is one adapter plus a set of named gates.

---

## 1. The one live integration: RoboHTML, through one file

```c
Result JsDomHtmlAttach(JsRuntime *runtime, int64_t documentId);
int    JsDomHtmlDetach(JsRuntime *runtime);
Result JsDomHtmlGetCounters(JsRuntime *runtime, JsDomHtmlCounters *out);
```

`Runtime/Source/RoboJS/RoboJSDomHtml.c` + `Runtime/Include/RoboJS/RoboJSDomHtml.h` are the **only** RoboJS
files that include a RoboHTML header, and that is a scanned property rather than a claim
(`Testing.md` §7). The split is deliberate:

| Half | Where | Why |
|---|---|---|
| the DOM a script sees | `RoboJSDom.c` / `RoboJSDom.h` — a `JsDomHost` vtable of callbacks | a host test implements the same vtable over a fixture, and `SuiteRoboJSHost.c` is that test |
| the adapter to *this* tree's document | `RoboJSDomHtml.c` / `.h` | so the one file that knows RoboHTML exists can be replaced without touching the binding |

`RoboJSDomHtml.h` is deliberately **not** in `RoboJS.h`, the module's front door: a consumer that wants the
DOM binding includes it by name, so `RoboJS.h` names no HTML type at all. What the adapter does and does not
support — wrapper interning, the selector subset, the construction gate — is `DOMBindings.md`; the phase's
integration cases are `Testing.md` §6.

**A second consumer of RoboHTML means a second adapter or none.** `RoboJSDomHtml.c` is not a helper other
code calls into; it installs a vtable (`JsDomInstall`) and owns a wrapper table. Anything that wants a
script to see a different document supplies a different host.

## 2. The build and test wiring

| Place | What it gained |
|---|---|
| `Runtime/Tools/Build.ps1` — `$Module` | `RoboJS` in the `ValidateSet`, so `-Module RoboJS` is a real target |
| …`$ProdSrcs` | the module's 18 production sources, one per question the phase asks |
| …`$SuiteSrcs`, `$suiteNeedles` | the three suites, selected by the `\RoboJS\`/`/RoboJS/` needle |
| …`$prefixMap.RoboJS` | `RoboBase`, `RoboMem`, `RoboContainers`, `RoboUnicode`, `RoboI18N`, `RoboFont`, `RoboGraphics`, `RoboHTML`, `RoboJS` — the *link* list, which is why the text and font modules appear: RoboHTML reaches them |
| …`$prefixMap.Html` | **`RoboGraphics` added.** A pre-existing defect, not a Phase 16 one: `RoboHTMLImage.c` decodes through RoboGraphics, so `-Module Html` and `TestHtml.ps1` could not link at all before this phase named the same dependency in the neighbouring row |
| `Tests/Runtime/Main.c` | three registrar declarations and three calls, so the runner sees `js.*` |
| `Scripts/Build/BuildRoboJS.ps1`, `Scripts/Test/TestRoboJS.ps1` | the per-subsystem entry points every module has (`TestRoboJS.ps1 -Target Host`, `-Filter "js."`) |
| `Scripts/Utilities/Invoke-RoboRuntimeBuild.ps1` | `RoboJS` in its `ValidateSet` |
| `Runtime/Tools/Build.ps1` — `Assert-RoboJSBoundaries` | the ownership scan, called by `-Verify` (`Testing.md` §7) |

## 3. The dependency direction, as it now stands

```text
RoboBase → RoboMem → RoboContainers → RoboUnicode / RoboI18N
          → RoboFont → RoboGraphics → RoboHTML
RoboHTML → RoboJS → (RoboJSDomHtml.c is the single crossing file)
```

`RoboJS` sits **above RoboHTML** — the document layer or one of the shell layers is its consumer — and
nothing below it may include it. That direction is machine-checked from both ends: the layers below name
`RoboJS` in their forbidden-include patterns, and `Assert-RoboJSBoundaries` holds this module to its own arm
of the tree. The AGENTS.md dependency graph names `RoboJS` in the same tier for the same reason.

## 4. The named gates, and the exact shape of each

| Gate | State | What it takes |
|---|---|---|
| **the shell runs a page's script** | built | `RoboDesktopScript.c/.h` is the one bridge file: `Assert-RoboDesktopBoundaries` exempts it by name for both the upward (`RoboJS`) and document-layer rules. `DesktopScene` creates it after each document build (`DesktopDocumentHtml` + `kDesktopPageScript`), pumps `JsLoopRunUntilIdle` every input step, and posts a `"click"` from a `POINTER_UP` on a `DOCUMENT` target (`DesktopInputStep.documentClick*`). The shell capability row stays `documents == 0` for the same reason as before — the bridge is beside the shell, not inside it |
| **the pointer path reaches a click listener** | built | `GetDesktopInputStep` records a document click; the scene posts `JsEventQueue("click", node)` and the next pump runs the listener. End-to-end: pointer up → layout node → DOM id → queued record → listener → `setAttribute` on `#desktopRoot` |
| **a guest checkpoint** | not built | no `ROBO_QEMU_JS_*` markers and no `test-qemu` command for this module (`QemuTesting.md`). The runtime is written to be guest-buildable — no `math.h` (`RoboJSMath.h`), no host capability, no thread — but naming it here would be a claim nothing runs |
| **`<script>` in a document** | not built, and not this module's | parsing a document's scripts, their order, `defer`/`async` and document-ready semantics is the document layer's work (Phase 17); RoboJS's part is `JsRuntimeEvaluate` and a `console` sink |
| **a capture phase, focus and pointer capture** | not built | `Events.md` §6; each is a listener-table question rather than a second dispatch |
| **a DOM construction gate that reopens after a parse** | RoboHTML's | the one boundary the integration suite asserts rather than works around (`DOMBindings.md`) |

Every row is stated so a consumer can refuse to build on it rather than discovering it at runtime, which is
the same rule `JsCapabilities`' absent block follows (`JsVersion.h`'s comment).

## 5. Lifecycle across the seam

| Sequence | What happens |
|---|---|
| `JsRuntimeCreate` | the heap, the globals, the builtins; no DOM, no listeners, no timers until a host installs them |
| `JsDomInstall` / `JsDomHtmlAttach` | the host vtable and the wrapper table; a second install on a live runtime is refused rather than replacing one |
| `JsRuntimeReset` | timers cancelled, both queues cleared, listeners and wrappers dropped, memory released — the builtins and the version probe stay, so the shell's next page starts from the same doors |
| `JsDomHtmlDetach` | the document link goes and the wrappers go with it; the RoboHTML document it pointed at is **not** destroyed — the document's owner is RoboHTML's |
| `JsRuntimeDestroy` | the whole heap at once (`Memory.md`), after the same teardown a reset does |

That ownership split is the one a shell has to get right: **RoboJS never destroys a document**, and it never
keeps one alive past a detach either.

---

**Tests.** `js.integration.*` (11 cases) is this document as a suite: the adapter both directions, the
construction boundary, the demo's ordering, the lifecycle across a detach, and two generations compared byte
for byte. `TestRoboJS.ps1 -Target Host` is the module's gate; `TestHtml.ps1` and `TestDesktop.ps1 -Target
Host` are the boundary suites the adapter touches.
