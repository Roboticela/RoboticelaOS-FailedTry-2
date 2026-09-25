# RoboJS — API stability

The public surface, what a consumer may rely on, and what a change is allowed to do to it. The module is at
`1.0.0` with `apiFrozen = 1` (`RoboJSVersion.h`), which makes this document a contract rather than a
description: the doors below are what the shell, the document layer and the tests are written against.

---

## 1. The tiers

### Stable — the seam a consumer uses

Frozen in 16.16. A change to any of these is a version bump and a note here.

| Header | The doors |
|---|---|
| `RoboJS.h` | the umbrella: the module's front door. It names no RoboHTML type, deliberately |
| `RoboJSRuntime.h` | `JsRuntimeConfigInit`, `JsRuntimeCreate`, `JsRuntimeEvaluate`, `JsRuntimeReset`, `JsRuntimeDestroy`, the global/heap accessors, the diagnostics, the counters |
| `RoboJSDom.h` | `JsDomInstall`, `JsDomUninstall`, `JsDomIsInstalled`, the `JsDomHost` vtable and its completeness check, wrapper accessors |
| `RoboJSEvent.h` | `JsEventRecordInit`, `JsEventQueue`, the record's vocabulary |
| `RoboJSTimer.h` | `JsTimerSet`, `JsTimerClear`, `JsTimerInfoOf`, `JsTimerCount`, `JsTimerDueCount`, `JsTimerCollectDue`, `JsTimerCancelAll` |
| `RoboJSEventLoop.h` | `JsLoopRunOne`, `JsLoopRunUntilIdle`, `JsLoopPump`, `JsLoopPostTask`, `JsLoopDrainMicrotasks`, `JsLoopHasPendingWork`, `JsLoopRequestStop`, `JsMicrotaskQueue`, `JsMicrotaskInstallGlobals`, the counters, `JsLoopClear` |
| `RoboJSVersion.h` | `JsVersionString`, `JsGetVersion`, `JsGetCapabilities`, `JsCapabilities` |

### Advanced-public — frozen as records, not as an interface

A binding author needs these; a *consumer* of the runtime should reach them only from inside a native
binding. The records are frozen — a field may be added at the end, none is renamed or reordered — while the
operational doors keep the latitude the engines need.

| Header | Why it is public |
|---|---|
| `RoboJSValue.h` | a native binding produces and reads values |
| `RoboJSObject.h` | a native binding defines properties and methods |
| `RoboJSEnvironment.h` | a binding creates a scope for a callback (`JsEnvDeclareConst`/`…Declare`/`…Set`) |
| `RoboJSFunction.h` | a native function's one call signature |
| `RoboJSException.h` | a binding raises and reads an exception |
| `RoboJSTypes.h` | the vocabulary — `Result` use, error kinds, `RoboJsDefault*` bounds, status names |
| `RoboJSDomHtml.h` | the shipped RoboHTML adapter and its counter record. It is the **only** RoboJS header that mentions RoboHTML, and it is *not* in the umbrella for that reason |

### Engines — public because the suites and the module's own sources are their callers

`RoboJSLexer.h`, `RoboJSParser.h`, `RoboJSAst.h`, `RoboJSInterpreter.h`, `RoboJSHeap.h`. Their grammar,
node shapes and step accounting may change inside a minor version. **A consumer must not call them**: a
consumer that wants a script evaluated calls `JsRuntimeEvaluate`, and one that lexes a script itself has
started a second front end (`Architecture.md` §1).

### Internal

`Runtime/Source/RoboJS/RoboJSInternal.h` (`struct JsRuntime`, the internal declarations) and
`RoboJSMath.h`. Not installed, not includable from a consumer's include path, and changed freely.

### Experimental and deprecated

None, and the absence is deliberate: a flag in `JsCapabilities` says what *is*, an engine header says what
*may move*, and nothing in this module is kept for compatibility with a shape it no longer has.

---

## 2. Ownership

| Object | Owns | Borrows |
|---|---|---|
| `JsRuntime` | the heap, the global object, the prototypes, the DOM wrappers, the timer table, both queues, the diagnostics, every `JsValue` string buffer it handed out | the `JsDomHost`'s `userdata` (copied *as a pointer* — the caller's object must outlive the install), the caller's `JsClockFn` |
| `JsValue` | nothing — it is copied by value (32 bytes) | a string value's buffer is **heap-owned and never written**, so a value is safe to pass between two environments |
| `JsObject`, `JsEnv` | — | the runtime's heap: there is no per-object free (`Memory.md` §2) |
| `JsDomHost` | — | the struct is *copied* on install; everything it points at is the caller's |
| `JsEventRecord` | — | the caller's struct; the queue copies it into the job |
| `JsTimerInfo`, `JsCapabilities`, diagnostic records | the caller's out-parameter | — |
| the RoboHTML document | **RoboHTML** — RoboJS never destroys one, and never keeps one alive past a detach | `JsDomHtmlAttach` records the document id and resolves nodes through RoboHTML's registry |

The last row is the one a shell has to get right, and `Integration.md` §5 states it as the seam's rule.

## 3. Lifetime rules

- **A runtime is created once and destroyed once.** `JsRuntimeReset` returns it to its just-created state
  *without* freeing the handle: the builtins and the version probe stay installed, everything a script made
  goes away. The doors stay valid across a reset.
- **A value stays valid until the runtime is reset**, because a `JsValue` that refers to an object refers to
  the heap. This is stated rather than enforced: a value held across a reset is a caller's bug, exactly as it
  is with the other by-value records in this tree.
- **`JsRuntimeEvaluate` borrows its source** for the duration of the call and copies only what it keeps — a
  function's source span, a string's buffer.
- **A wrapper is interned per node.** Two evaluations that read the same node get the same wrapper object, so
  `app === document.getElementById("app")` holds; the wrapper dies with the runtime, not with the call.
- **A pending exception lives until it is cleared** (`JsRuntimeClearException`) or replaced by the next one.
  Reading it does not clear it, so a host may report it *and* let a binding inspect it.

## 4. Threading

| Kind | What may be shared |
|---|---|
| **the runtime** | nothing — it belongs to the thread that created it. There is no lock, and `Threading.md` states the rule the module is written to |
| **shared, read-only** | the source a caller passes to `JsRuntimeEvaluate`, a `JsCapabilities` record, a `JsDomHost`'s data if the *host* makes it shareable |
| **per boundary** | the task queue, the microtask queue, the timer table and the pending exception: mutated only by the owning thread, at a boundary |
| **mutable global** | none. Two runtimes in one process share no heap chunk, no prototype and no table; the only process-wide thing is `RoboBase`'s allocator, which the heap uses through `RoboMem` and never directly |

**A device thread posts.** `JsLoopPostTask` and `JsEventQueue` are the doors a producer may call, and their
contract is "append and return" — which is why a full queue is a counted refusal (`RoboOutOfMemory`) rather
than a wait. Nothing else in the module is safe to call from another thread.

## 5. Caches, state and what is not derived

There is no invalidation model, and that is a design consequence rather than an omission:

| State | Fact |
|---|---|
| the DOM wrapper table | interned per node id; a structural move retargets the interned wrapper rather than creating a second one (`DOMBindings.md` §3) |
| the adapter's pending records | what a document that refuses construction produced; materialized when construction is open, and **never** repaired silently |
| derived geometry | none — RoboJS computes no layout, so a script's DOM write has nothing to invalidate. A change a page makes reaches the screen through RoboHTML/a layout pass, not through a callback into this module |
| timers, listeners, queues | cleared by `JsRuntimeReset`/`JsRuntimeDestroy`; `JsLoopClear` answers how many jobs it dropped, so a consumer can assert nothing survived |

The one rule worth stating as an invariant: **a fact a later layer needs is carried, not recomputed.** A
binding that could look up a node twice gets the same wrapper both times because it was interned, not because
it was derived again.

## 6. Error handling

| Input | Answer |
|---|---|
| a NULL where a pointer is required | `RoboInvalidArgument` |
| a runtime that fails to allocate | `RoboOutOfMemory`; no half-built runtime is returned |
| a script with a syntax error | `RoboError` from `JsRuntimeEvaluate`, a located `SyntaxError` pending, and **the runtime stays usable** |
| a script that throws | the same shape: `RoboError`, the thrown value pending |
| a script that exhausts a budget | `RoboJsErrorLimit` at a located span — never a hang (`Architecture.md` §6) |
| a non-callable where a callback is required | `RoboInvalidArgument` from a *host* door (`JsTimerSet`) and a `TypeError` from the *script* door (`setTimeout`) — two different mistakes, two different answers (`Timers.md` §1) |
| a full queue | `RoboOutOfMemory` with a `RangeError` pending — counted, never a dropped job |
| a loop door called re-entrantly | `RoboInvalidArgument` (`EventLoop.md` §3) |
| a host that is missing a required vtable entry | `RoboInvalidArgument` from `JsDomInstall`; a partial host installs nothing |
| a second install on a live runtime | the previous install is dropped and replaced, which is what a shell navigating to a new page wants |
| a DOM operation RoboHTML's model cannot represent | refused with a **named** reason, never approximated (`DOMBindings.md`) |

## 7. What a version bump means

| Component | When it moves |
|---|---|
| `RoboJsVersionMajor` | a stable door changes meaning or disappears, or `JsCapabilities`' contract changes |
| `RoboJsVersionMinor` | a door is added, a capability flag flips, a documented limitation is removed |
| `RoboJsVersionPatch` | a defect fixed with no surface change |

`apiFrozen = 1` reports that the surface above is pinned; it does not mean the module is finished. The
capability table is the honest half, and its rule is the tree's: **every flag is read field by field in
`js.runtime.capabilities`**, so a flag that stops being true fails a test rather than misleading a consumer.
A new capability is added to the table *and* to that case in the same change, and an absence that gets built
moves from the second block to the first — never the reverse.
