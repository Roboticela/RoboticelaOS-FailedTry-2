# RoboJS — Memory

The heap, why it has no collector, and what a caller has to do about it.

**One sentence:** every allocation a runtime makes comes from a chunked bump allocator that is released **as a
whole**, so a reference cycle cannot leak and memory grows until a reset. That is a trade, and this document is
where the cost is written down rather than discovered.

---

## 1. The heap

`RoboJSHeap.h` / `RoboJSHeap.c`.

| Door | Job |
|---|---|
| `JsHeapAlloc(heap, bytes)` | bump-allocate; a full chunk starts the next |
| `JsHeapRealloc(heap, old, oldBytes, newBytes)` | grow in place when the allocation ends its chunk, else copy and abandon |
| `JsHeapString(heap, bytes, length)` | intern a NUL-terminated copy; `length == -1` means `strlen` |
| `JsHeapReset(heap)` | **drop every chunk** and start a fresh one |
| `JsHeapLiveBytes` / `JsHeapAllocationCount` / `JsHeapChunkCount` | the numbers a test asserts |
| `JsHeapSetLimits` / `JsHeapMaxProperties` / `JsHeapMaxStringLength` | the per-object caps |

Chunks are `JsRuntimeConfig::heapChunkBytes` (`RoboJsDefaultHeapChunkBytes`, 64 KiB). There is **no free and no
collector**: `JsHeapAlloc` never returns a block to circulation before the whole heap goes, so there is no free
list, no compaction, and no write barrier.

---

## 2. Why no collector

A mark-and-sweep collector over a tree-walking interpreter is not hard to *write* — it is hard to make *sound*.
Every place a `JsValue` or a `JsObject *` is held outside the heap becomes a root: a native binding's local, a
`JsEnv` on the call stack, the runtime's own fields, a DOM wrapper's state, an in-flight task's argument array, a
listener closure, a timer's callback. A single missed root is a use-after-free that reproduces once a week.

The alternative this build took has one invariant to maintain — *everything a runtime owns is in the heap* — and
the failure mode is a number that grows rather than a pointer that dangles.

**What that means in practice:**

| Consequence | What it costs |
|---|---|
| a long-lived runtime that runs many scripts grows | the host calls `JsRuntimeReset` between pages |
| `delete obj.key` frees nothing | the property goes; the bytes do not |
| a `for (i = 0; i < 1e6; i++) { arr.push({}) }` loop is bounded by `maxPropertiesPerObject` | the cap is the answer, not the collector |
| a cycle can never leak, because there is nothing to leak *through* | — |

For the workload this runtime exists for — one page's script, run, then replaced — releasing as a whole is both
simpler and *provably* cycle-free, and the resource cost is bounded by the same budgets as everything else
(`Security.md` §4).

---

## 3. The lifetime of the four kinds of allocation

| Allocation | Freed by |
|---|---|
| a value's string bytes | `JsHeapString` copies into the heap; the bytes live until a reset |
| an object, its property array, its element storage | heap; grown by `JsHeapRealloc` |
| an environment link and its binding array | heap; a link stays alive because a closure points at it |
| a `JsRuntime` itself | `JsRuntimeDestroy` → `JsHeapDestroy`, which frees the chunks *and* the `JsHeap` |

`JsRuntimeReset` is the middle door: it drops the heap's contents (so every script object, string, environment and
closure is gone), clears the DOM wrappers, the listeners, the timers and the task queue, and re-installs the
builtins. What survives a reset is the runtime handle and the configuration — which is exactly what a navigation
needs, and exactly why a host must not keep a pointer into a script's object across one. `APIStability.md` states
that as the caller's obligation.

**`JsRuntimeDestroy` and a live DOM.** A runtime attached to a document `JsDomHtmlDetach`es first: the adapter's
state is freed, the wrappers are dropped, and the *document is untouched*. `js.integration.lifecycle` proves both
orders (runtime-then-document and document-then-runtime) end cleanly, and `DomExpectCleanTree` proves the document
survived a script's whole lifetime.

---

## 4. The numbers a test can assert

`JsHeapLiveBytes`, `JsHeapAllocationCount` and `JsHeapChunkCount` exist so that "the heap works" is a measurement:

- **Growth is monotone within a run.** An allocation count that went *down* would mean a free, which does not
  exist.
- **A reset returns to the baseline.** The allocation count immediately after `JsRuntimeReset` is the count for a
  just-created runtime, which is what `js.runtime.lifecycle` asserts — the one shape that catches "the reset
  forgot to drop something".
- **A chunk is only abandoned when it is full**, so `chunkCount` says how many 64 KiB blocks a script needed. A
  test that wants to know whether a workload fits asks that rather than guessing.

The per-object caps (`maxPropertiesPerObject`, `maxStringLength`) are *limits*, not memory accounting: an object
that reaches the property cap refuses the next property with a `RangeError`, and a string that reaches the byte
cap refuses the append. Both are published through `JsGetCapabilities` so a caller reads the number instead of
hard-coding one.

---

## 5. What is deliberately absent

- **A garbage collector** — §2, and `JsCapabilities::garbageCollector` is 0.
- **Weak collections** (`WeakMap`, `WeakSet`, `WeakRef`, `FinalizationRegistry`). They are *about* a collector's
  liveness question; there is no liveness question here, so a "weak" map would be a map with a smaller cap and a
  misleading name.
- **An arena or a pool for short-lived objects.** Freelists inside a bump allocator buy reuse for values that a
  reset will drop anyway; the interpreter's own locals are the stack.
- **A byte/step cost per allocation.** A script cannot be charged for its memory separately from
  `maxPropertiesPerObject` and `maxStringLength`, so a build that wants a byte budget has to add one to `JsHeap`
  (a stated row, not an implied gap).
