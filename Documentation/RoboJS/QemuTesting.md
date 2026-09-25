# RoboJS — Guest / QEMU testing

**There is no guest half of Phase 16, and this document exists to say so rather than to imply one.** No
`ROBO_QEMU_JS_*` marker is pinned in `Scripts/pipeline/commands.test.ps1`, `User/robo-runtime/main.c` has no
JavaScript checkpoint, `RoboJS` is not in the guest program's `RuntimeModules` list
(`scripts/roboboot-build-common.ps1`), and `Scripts/Test/TestRoboJS.ps1 -Target Qemu` therefore reports
`RoboJS has no QEMU suite. Use -Target Host.` and exits 0.

That is a stated gap, not a hidden one: Phases 12.18–12.24 each added a second off-screen render because
their evidence was *visual*; this module's evidence is a number a host suite reads on a deterministic clock,
and inventing a guest marker list it could not honour would be the same defect as an untested capability
flag.

---

## 1. Why the module is guest-ready anyway

The runtime was written so that a guest checkpoint is a *build-list entry plus a checkpoint*, not a port:

| Property | Where it is kept |
|---|---|
| **no `math.h`** | `Runtime/Source/RoboJS/RoboJSMath.h` is a header-only shim (`JsMathNan`, `…Floor`, `…Pow`, …) — the guest libc has no math header, and the tests carry the same shim (`JsTestNan`/`JsTestInfinity`) |
| **no clock of its own** | a timer is due on the *caller's* clock (`Timers.md` §2): `deterministic = 1` makes it a counter only `JsRuntimeAdvanceClock` moves, and with no clock at all it is not wall time |
| **no thread** | `Threading.md`: the runtime, its wrappers, its listeners and its timers belong to the thread that created it; a producer posts a job |
| **no host capability** | `hostProcessAccess`, `hostFileSystem` and `hostNetwork` are 0 in `JsCapabilities`, and `js.security.boundary` reads the global table to prove it |
| **no allocator of its own** | the heap goes through `RoboMem`, which is in the guest already |

So the module compiles against the guest libc today — which is a claim a build can check and a *run* cannot,
which is exactly why it is not offered as evidence of a guest suite.

## 2. What stands in for it

| Evidence | What it shows |
|---|---|
| `Build/Runtime/RoboTest.host.exe --filter=js.` | 45 cases, `Testing.md` |
| `js.runtime.capabilities` | every landed / absent / bounds field of `JsCapabilities`, read one by one, so a flag that stops being true fails a test |
| `js.integration.determinism` | two generations of the same page, compared byte for byte |
| `js.integration.demo` | the phase's end-to-end page: a listener, a microtask and a timer, with the DOM write read back out of RoboHTML's own registry |
| `Assert-RoboJSBoundaries` (`-Verify`) | the ownership rules as a source scan |

## 3. What a guest checkpoint would be, in order

For the row to change, four things change together — and the first is the one that makes it real:

1. **`RoboJS` in the guest program's `RuntimeModules`** (`scripts/roboboot-build-common.ps1`), next to
   `RoboHTML`, which it sits above. The list is discovered from each module's own directory, so no source
   file is re-typed there.
2. **A checkpoint in `User/robo-runtime/main.c`** — a *second off-screen render* rather than an edit to the
   shared scene, the way 12.20's vertical and 12.21's selection checkpoints are: build a small document,
   attach the adapter to it with `JsDomHtmlAttach`, evaluate a script that reads an attribute, writes one,
   adds a listener, posts an event and schedules a timer, then run `JsLoopRunUntilIdle` **by hand** and
   report the counters.
3. **`ROBO_QEMU_JS_*` markers** added to the pinned list in `Scripts/pipeline/commands.test.ps1` under the
   command that already runs the other checkpoints — for this module, values a *test* can quietly get wrong
   rather than pixels: `_OK`, `_MARKERS`, `_ATTRS`, `_LISTENERS`, `_TASKS`, `_MICROS`, and a `_FAIL <stage>`
   that names what refused.
4. **An entry point** — `TestRoboJS.ps1 -Target Qemu` gaining `-PipelineQemu "test-qemu-display"`, which is
   the pattern `TestDesktop.ps1` uses; the part only declares one when the guest pipeline has a command to
   run.

Until all four exist the marker list must not name them: **a pinned marker that nothing prints is a test
that fails for the right reason, and a document that claims one is a claim nothing runs.**

---

**Related.** `Overview.md` §7 lists the artifacts this phase offers; `Testing.md` §8 repeats this absence
next to the other things the suites do not prove; `Integration.md` §4 carries it as a named gate.
