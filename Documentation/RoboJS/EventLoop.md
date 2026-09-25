# RoboJS — The Event Loop (16.13)

Where a callback runs, and the only thing in the module that decides *when*. `RoboJSEventLoop.h` is the
contract; every ordering statement in this document is one a suite on a deterministic clock asserts.

---

## 1. One boundary, and the order inside it

```c
Result JsLoopRunOne(JsRuntime *runtime, int *outRan);
Result JsLoopRunUntilIdle(JsRuntime *runtime, int32_t maxCycles);
Result JsLoopPump(JsRuntime *runtime, int *outRan);
int    JsLoopHasPendingWork(JsRuntime *runtime);
void   JsLoopRequestStop(JsRuntime *runtime, int stop);
```

`JsLoopRunOne` performs **one boundary**, and the order inside it is the whole contract:

```text
boundary
  1. drain the microtask queue to empty        (bounded by maxMicrotasksPerCycle)
  2. if no task is queued, collect every due timer into the queue
  3. run at most one job
```

Read the order twice, because it is not the order the phrase "task, then microtasks" suggests and the
difference is observable:

- **The drain goes first**, because the microtasks waiting at a boundary belong to the task that has just
  ended — including a script a caller ran *outside* the loop. A job's own microtasks therefore run at the
  **next** boundary, which is what keeps one boundary one job.
- **Only one job runs per boundary.** A job that posts another job does not run it in the same boundary, so
  a listener can never re-enter the task it is running inside.
- **A due timer is collected only when the queue is empty.** A queued task therefore runs before a timer
  that became due while it was waiting, and timers are ordered among themselves by due time
  (`Timers.md` §4).

That is what makes `queueMicrotask(f); setTimeout(g, 0)` from one script print `f` then `g` — the
microtask at step 1 of the first boundary, the timer at steps 2–3 of the same one — a fact about the loop
rather than a race. `js.loop.microtasks` asserts it on the deterministic clock, synchronously and without
sleeping.

## 2. Producers post; the owner runs

```c
Result JsLoopPostTask(JsRuntime *runtime, JsTaskKind kind, const JsValue *callback,
                      const JsValue *thisValue, const JsValue *args, int32_t argCount);
```

**Nothing in the module calls a JavaScript function from a producer's stack.** A device thread, an input
adapter, a timer and a native binding all take the same door: append a job and return. The thread that owns
the runtime — the one that created it — runs the boundary. `Threading.md` states the rule; the queue is the
rule as a data structure rather than a promise, and `js.event.deferred` is the case that proves a posted
event does not reach a listener until a boundary.

| Kind | Who posts it | What runs |
|---|---|---|
| `RoboJsTaskScript` | a caller that wants a script evaluated at a boundary | the callback, with the runtime installed |
| `RoboJsTaskTimer` | `JsTimerCollectDue`, one job per due timer | the timer slot's callback with its captured arguments |
| `RoboJsTaskEvent` | an adapter via `JsEventQueue` | the dispatch for the job's record (`Events.md` §4) |
| `RoboJsTaskNative` | any binding | the callback, with `thisValue` and arguments |
| `RoboJsTaskMicrotask` | `JsMicrotaskQueue` / `queueMicrotask` | the callback, in the microtask queue rather than the task queue |

**The kind is kept to be readable, not to be dispatched differently.** Four of the five are "call this
function with these arguments"; the kind is what lets a dump say what is pending and what lets
`maxTimersPerCycle` have something to count. A caller cannot invent a kind that runs differently —
`JsTaskKindIsKnown` is the door, and the run is one `switch`.

A full queue is **`RoboOutOfMemory` on the post**, with a `RangeError` left pending, rather than a silently
dropped job: the caller can decide to retry after a boundary, and a dropped callback is the one outcome a
page cannot recover from.

## 3. The loop is not reentrant

`JsLoopRunOne` and `JsLoopRunUntilIdle` answer **`RoboInvalidArgument`** when the runtime is already
executing a script or already inside a boundary.

That is a deliberate refusal rather than a defensive check. Every ordering statement above depends on "one
job at a time", and a listener that could call `JsLoopRunOne` would be running a task from inside a task —
turning the queue's order into the call stack's. A callback that wants more work has two honest doors:
post a task (`JsLoopPostTask`) or a microtask (`JsMicrotaskQueue`).

`js.loop.reentrancy` asserts the refusal from **five** places, because "not reentrant" is a claim about each
of them rather than about one: from inside an evaluation, from inside a microtask, from inside a listener,
from inside a native task, and from a `JsLoopRunUntilIdle` already running. Each one reads
`RoboInvalidArgument` and then proves the loop still runs the work afterwards.

## 4. Idle, stop, and the frame door

| Door | Behaviour |
|---|---|
| `JsLoopHasPendingWork` | 1 when a job **or** a microtask is waiting, or a timer is still active — a timer is work even though it is not yet a job |
| `JsLoopRunUntilIdle(runtime, 0)` | runs boundaries until nothing is pending, or until the configured `maxTasksPerCycle` boundaries have run |
| `JsLoopRunUntilIdle` at its cap | **`RoboTimeout`** with a `RoboJsErrorLimit` pending, so a caller can tell "finished" from "gave up" rather than assuming |
| `JsLoopRequestStop(runtime, 1)` | the *next* boundary answers `RoboTimeout` and does nothing, until it is cleared — a window-closing request without a caller-owned flag |
| `JsLoopPump` | collect due timers, then one boundary: the door a host frame calls once per frame so a timer fires without a pump of the caller's own |
| `JsLoopClear` | empties both queues and the stop request, and answers how many jobs were dropped — what `JsRuntimeReset` calls, so nothing a page queued outlives it |

## 5. What a boundary can answer

| Result | Meaning |
|---|---|
| `RoboOk`, `*outRan == 1` | something ran; the runtime is usable |
| `RoboOk`, `*outRan == 0` | nothing was ready — the loop is idle, which is not an error |
| `RoboError` | a job threw; the value is in the runtime's pending exception (`Exceptions.md`) and the loop **stays usable**, which is what "an uncaught error does not stop the desktop" means |
| `RoboTimeout` | a stop was requested, or a cycle budget was reached |
| `RoboNotSupported` | the runtime was created with `enableEventLoop = 0` |
| `RoboInvalidArgument` | the runtime is mid-evaluation or already in a boundary (§3) |

A throwing microtask **stops the drain and leaves the rest queued**: those microtasks were queued by work
that did not finish, so running them would be inventing a state the specification does not describe. The
next boundary drains what remains.

## 6. Counters

`JsLoopPendingTaskCount`, `JsLoopTasksRun`, `JsLoopPendingMicrotaskCount`, `JsLoopMicrotasksRun` and
`JsLoopMicrotaskOverflows` are read by the suites and by a shell's status line. `…MicrotaskOverflows`
counts the boundaries that hit `maxMicrotasksPerCycle`; each one is a cut chain, and a nonzero value is the
one number that says a page is fighting the budget rather than working inside it.

## 7. What is absent

`requestAnimationFrame` (a frame belongs to the compositor, not to a script — `Timers.md` §6), a
`setImmediate` (the task door already exists), prioritised or weighted task queues, a starvation guard for
tasks other than the microtask cap, `MessageChannel`/`postMessage`, `window.onerror` and any uncaught-error
hook (the exception is a record the host reads, not a callback this module invents), an idle callback, and
a second thread. **The loop shares the thread that owns the runtime**, which is why a step budget, a call
depth budget and a microtask cap all exist: "runs forever" is not an option for a loop that shares a thread
with an operating system.

## 8. Where the numbers come from

`JsRuntimeConfig` (`Architecture.md` §4): `maxMicrotasksPerCycle` (4 096), `maxTimersPerCycle` (256),
`maxTasksPerCycle` (4 096) and `maxQueueLength` (4 096), each published through `JsGetCapabilities`' bounds
block and asserted in `js.limit.budgets` rather than hard-coded in a caller.

---

**Tests.** `js.loop.tasks` (the boundary order, posting, the queue's refusal), `js.loop.microtasks` (16.14's
ordering and the cap), `js.loop.reentrancy` (§3's five refusals), `js.lifecycle.cleanup` (a reset drops both
queues) and `js.integration.demo` / `js.integration.steps` (the same ordering over a real document). `Timers.md`
is the *due* half; this document is the *when* half.
