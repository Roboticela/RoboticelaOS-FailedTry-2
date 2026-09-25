# RoboJS — Threading and ownership

Who owns a runtime, what may happen on which thread, and how another thread's work reaches a script.

**One sentence:** everything a runtime owns belongs to the thread that created it, the loop is not reentrant, and
a device thread **posts**.

---

## 1. The ownership rule

| Thing | Owner |
|---|---|
| a `JsRuntime` and its heap | the thread that called `JsRuntimeCreate` |
| every `JsValue`, `JsObject`, `JsString` and `JsEnv` in it | the same thread, transitively |
| the DOM wrappers and the `JsDomHtmlState` | the same thread |
| the listener table, the timer table, the task queue, the microtask FIFO | the same thread |
| a `JsDomHost`'s own tree | the **host's** thread — see §4 |
| a host's `JsClockFn`/`JsConsoleFn` | called on the runtime's thread, so it must be safe there |

**There is no lock in the module and no atomic.** That is not an oversight: a lock would make every value
operation a possible contention point and every compound operation a possible deadlock, in a runtime whose whole
job is to run one script in one shell.

Two runtimes in one process are independent: they share no heap, no prototype, no timer table, no macro. Two
*threads* each with their own runtime are therefore fine, and are the supported way to run two scripts at once.
Two threads sharing one runtime is undefined behaviour, which is the honest thing to say rather than to imply a
safety that is not there.

---

## 2. The loop is not reentrant

`JsLoopRunOne` inside a listener is `RoboInvalidArgument`:

```text
JsRuntimeIsExecuting(runtime) == 1  →  the loop refuses to be entered again
```

A task that wants to run more work posts it:

| It wants | It posts |
|---|---|
| "after the current task, but before the next one" | `JsLoopPostMicrotask` |
| "as the next thing" | `JsLoopPostTask` (`RoboJsTaskNative`, `RoboJsTaskCallback`, …) |
| "later, by time" | `JsTimerSet` |

This is the rule that makes the event loop's ordering statements true — a nested drain would run a task *inside* a
task, and then "microtasks run at every task boundary" would describe something that never happened. `Events.md`
§4 and `Microtasks.md` are the ordering contracts it protects.

---

## 3. How another thread's work reaches a script

A device thread — an input poller, a compositor, a timer wheel, an OS notification — does **not** call
`JsRuntimeEvaluate`. It constructs a record and posts it:

```c
/* on the device thread: build a fixed, copyable record */
JsEventRecord record;
JsEventRecordInit(&record, "click", targetNode);
record.clientX = 4.0;
record.clientY = 8.0;

/* hand it over; the runtime's own thread runs it at a boundary */
JsEventQueue(runtime, &record);
```

Three properties make that safe to state as a contract:

1. **`JsEventRecord` is fixed-size and by value.** No allocation, no pointer into the device's state, no ownership
   question. A device thread can build one in its own frame.
2. **Nothing runs on the posting call.** `JsEventQueue` appends; the listener runs inside
   `JsLoopRunOne`/`JsLoopRunUntilIdle` on the runtime's thread. `js.integration.demo` asserts that (`order.length`
   is 0 immediately after the post).
3. **The target is an id, not a pointer.** A node id is meaningful to the runtime's own DOM seam; a device that
   wanted more would be handing over a host pointer, which the seam does not accept.

`JsEventQueue` is therefore the *only* documented cross-thread door in the module, and everything else a device
thread might want to say — "this timer expired", "this document changed" — is expressed as a task
(`JsLoopPostTask`) with the same by-value discipline.

---

## 4. The host's half

| Question | The host's answer |
|---|---|
| whose thread is the document on? | the host's. `JsDomHost` callbacks are invoked **on the runtime's thread** while the runtime is executing or draining, so a host whose tree is owned by another thread must marshal before answering |
| can a host callback call back into the runtime? | yes — a native binding may call `JsRuntimeGetGlobal`, `JsRuntimeThrow`, run a script, or call a script function through the interpreter. What it may **not** do is enter the loop (a nested drain, or a synchronous `JsLoopRunOne` that would run a task out of order). A host callback that wants "run this later" posts a task or a microtask |
| can a host hold a `JsValue`? | only as a borrowed argument, for the duration of the call. A `JsValue` is meaningless after the heap is reset, and nothing in the module refcounts one |
| can a host free its own tree mid-call? | no. `isLive` is how a host says "my document is gone"; the adapter then refuses rather than dereferencing. Destroying a document between two calls is legal, *during* one is not |
| can a host run on a device thread and touch the runtime? | no. §3's record-and-post is the door |

---

## 5. What is absent, and why each is a row rather than an apology

- **`multiThreaded` is 0** in the capability table. There is no shared runtime, no worker, no structured clone, no
  `SharedArrayBuffer`, no `Atomics`. A "worker" in this tree would be a second runtime on a second thread with its
  own heap — which is a legitimate design and a phase of its own, not a flag.
- **No atomics or locks**, per §1.
- **No preemption.** A script runs until it yields (it returns from the evaluation, or the loop boundary arrives)
  or until a budget stops it. A shell that wants a responsive device during a long script has to run the script
  under a step budget and drain between frames — which is what `JsRuntimeConfig::maxSteps` and
  `JsLoopRunUntilIdle(runtime, budget)` exist for.
- **No signal handling, no cancellation token.** A runaway script is stopped by a budget; a host that wants to
  stop one for another reason lowers the budget. `Security.md` §4 is the list of numbers.
- **No thread-affinity assertion.** `JsRuntimeIsExecuting` tells a caller whether the runtime is mid-evaluation
  (which is what the loop's reentrancy refusal reads), but nothing checks *which* thread called: a
  thread-mismatch cannot be detected portably in this freestanding libc. §1 is therefore a rule a caller keeps,
  not one the module can enforce.
