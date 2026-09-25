# RoboJS — Microtasks (16.14)

The queue that runs *between* jobs, and the one rule that makes it useful: **a microtask queued by a
microtask runs in the same drain.** `RoboJSEventLoop.h` carries the doors; `EventLoop.md` §1 is the ordering.

---

## 1. The doors

| Door | Who calls it |
|---|---|
| `queueMicrotask(f)` | a script — the only microtask source a script has |
| `JsMicrotaskQueue(runtime, callback)` | a native binding, an adapter, a host |
| `JsLoopPostMicrotaskValue` | the module's own internals (the `Timer`/`Event`/`Dom` paths) |
| `JsLoopDrainMicrotasks(runtime, *outRan)` | a host that wants a scoped drain without running a task |
| `JsMicrotaskInstallGlobals(runtime)` | `JsRuntimeInstallGlobals`; idempotent |

`JsMicrotaskQueue` answers **`RoboInvalidArgument`** for a NULL argument and **`RoboError`** with a pending
`TypeError` (`"queueMicrotask needs a function"`) for a non-callable — a script's mistake is a script's
error, exactly as `JsTimerSet` states its own two cases (`Timers.md` §1). A full queue is `RoboOutOfMemory`
with a `RangeError` (`"the microtask queue is full"`), counted rather than dropped.

## 2. The queue

A ring, `maxQueueLength` slots wide (4 096, the same number as the task queue), FIFO: **a microtask queued
by a microtask runs after it, in the same drain.** The queue is allocated with the runtime and freed with it;
`JsLoopClear` empties it and drops the stop request, and `JsRuntimeReset` calls that door — so nothing a page
queued survives the reset that ends it (`Architecture.md` §5).

`JsTaskKindMicrotask` exists on the job record so a dump can name a pending microtask, and the drain is the
only thing that runs one.

## 3. The drain

```text
while the queue is not empty:
    take the head, count it (JsLoopMicrotasksRun)
    call it
    if it threw          → stop: the rest stay queued
    if drained >= cap    → microtaskOverflows += 1, a RoboJsErrorLimit, RoboError
```

Three statements, and each one is a decision:

- **The drain runs to empty, including what the drain itself queues.** A chain of microtasks that each queue
  the next finishes inside one boundary, which is the difference between a microtask and a task.
- **A throwing microtask stops the drain.** The microtasks behind it were queued by work that did not
  finish, so running them would invent a state the language does not describe; they stay queued and the next
  boundary drains what remains. The exception is readable through `JsRuntimePeekException` and the loop
  stays usable.
- **`maxMicrotasksPerCycle` (4 096) cuts a chain that never ends.** A microtask that re-queues itself forever
  is not a hang: it is a `RoboJsErrorLimit` at the cap, one `…MicrotaskOverflows` count, and a runtime the
  host may keep using. `js.loop.microtasks` asserts both the count and the diagnostic.

## 4. When the drain happens

At the **start** of every boundary (`EventLoop.md` §1), before any job runs. So:

| Sequence | Order |
|---|---|
| one script: `queueMicrotask(f); setTimeout(g, 0)` | `f`, then `g` — the microtask at step 1 of the first boundary, the timer at steps 2–3 of it |
| a job that queues a microtask | the job, then the microtask at the **next** boundary, before the next job |
| a microtask that queues a microtask | both in one drain, in that order |
| a host that posts a task after running a script by hand | the script's microtasks first — they belong to the script that just ended |

`JsLoopDrainMicrotasks` is public for the last row's *other* half: a host that installs a binding which runs
a script as part of its own work can drain at its own boundary instead of waiting for one. It is the same
drain, with the same cap, the same counter and the same refusals — not a second implementation.

## 5. Why there is no `Promise`

`queueMicrotask` exists because 16.14 needs a microtask *source* an implementation can state completely in a
few hundred lines. A `Promise` does not: its constructor, its resolution procedure, its thenable
assimilation, its `then`/`catch`/`finally` chaining, its unhandled-rejection reporting and its interaction
with `await` are a specification of their own, and **a half-`Promise` is worse than none** — a page would
have to guess which half it got. `JsCapabilities` reports `promises` as 0, and the lexer refuses `async`/
`await` with a located `SyntaxError` rather than parsing them into something that cannot work.

The consequence, stated plainly: **a script cannot express "after the current job" except by
`queueMicrotask`**, which is a callback and not a value. That is a real limitation of this build, and it is a
row in `Limitations.md` rather than a footnote here.

## 6. What is absent

`Promise` and everything that implies it (`async`/`await`, unhandled-rejection reporting, thenable
assimilation), `MutationObserver`, `process.nextTick`, `queueMicrotask`-like prioritisation, a
`MessageChannel` port's microtask, a per-native-call implicit checkpoint (the drain is explicit, and it is
one door), and introspection of a *pending* microtask from a script — the counters are host doors.

---

**Tests.** `js.loop.microtasks` (§1–§4: the ordering against `setTimeout(f, 0)`, a microtask queued by a
microtask, a throwing microtask, a self-requeuing chain cut at the cap with the loop still usable, and the
counters), plus `js.loop.tasks` and `js.integration.demo` for the same ordering beside a task. `Threading.md`
§4 is the reason the drain is a thread-local queue rather than a lock.
