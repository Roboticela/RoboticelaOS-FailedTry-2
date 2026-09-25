# RoboJS — Timers (16.12)

A timer is a **due record**, never a callback that ran. `RoboJSTimer.h` is the contract, and the one sentence that
matters is: `JsTimerSet` records a due time and answers an id; **it does not call anything.**

---

## 1. Setting one

```c
Result JsTimerSet(JsRuntime *runtime, uint64_t delayMs, const JsValue *callback,
                  const JsValue *args, int32_t argCount, int repeating, int64_t *outId);
Result JsTimerClear(JsRuntime *runtime, int64_t id);
Result JsTimerInfoOf(JsRuntime *runtime, int64_t id, JsTimerInfo *out);
int32_t JsTimerCount(JsRuntime *runtime);
int32_t JsTimerDueCount(JsRuntime *runtime, uint64_t nowMs);
int32_t JsTimerCollectDue(JsRuntime *runtime, uint64_t nowMs, int32_t maxFirings);
int32_t JsTimerCancelAll(JsRuntime *runtime);
```

`JsTimerSet` **refuses a non-callable** with `RoboInvalidArgument` and no exception: a host passing one is a
host-contract violation, and nothing in a script threw. The script's own door — `setTimeout(f, 0)` — checks first
and throws a `TypeError` before reaching here. The two answers are different on purpose: one is a script's error,
the other is a caller's bug.

```c
typedef struct JsTimerInfo {
    int64_t  id;
    uint64_t dueMs;       /* when it is next due, on the runtime's clock */
    uint64_t intervalMs;  /* 0 for a one-shot */
    int      repeating;
    int      active;
    int32_t  fired;       /* how many times its callback has run */
} JsTimerInfo;
```

## 2. The clock is the caller's

| Configuration | The clock |
|---|---|
| `deterministic = 1` | a counter that **only** `JsRuntimeAdvanceClock` moves |
| `deterministic = 0`, `clock` installed | the host's `JsClockFn` |
| `deterministic = 0`, no clock | the runtime's own counter, advancing by one per **evaluation** |

The third row is the one worth reading twice: with no clock at all it is deliberately **not** wall time, so no
observable behaviour in this module depends on how long a test took. A test that wants ordering asserts around a
hand-advanced clock ("nothing fires at 999 ms; the interval fires at 1000 ms") without sleeping — which is what
`js.timer.schedule` does.

**A delay of 0 is not "now".** It is due at the current clock value, and the callback still runs at a **loop
boundary** — so the statements after `setTimeout(f, 0)` run first. That is the difference between a timer and a
callback call, and `js.integration.demo`'s `listener,microtask,timer` order is the assertion.

## 3. The table

- **`maxTimerCount`** (1 024) bounds the slots. Exhaustion throws a `RangeError` naming the cap and answers
  `RoboError` — a counted refusal, not an overrun and not a silent drop.
- **A repeating timer is one record, not a new one per firing.** Its id is stable across firings and its due time
  moves forward by its interval, which is what makes a callback that clears **its own** id during a firing work.
  A per-firing record gets that case wrong: the id it clears is one that no longer exists.
- **`maxTimersPerCycle`** (256) bounds how many a single boundary will fire, so an interval with a zero delay
  cannot starve the tasks behind it.
- **Ids are runtime-local and never reused.** `JsTimerSet` answers 1, 2, 3, … from a monotonic counter a reset
  rewinds. `JsTimerClear` on an id this runtime never issued — including one from another runtime — answers
  `RoboNotFound` rather than clearing a stranger's timer through a shared table.

## 4. Firing

`JsLoopRunOne` asks `JsTimerCollectDue(runtime, now, maxTimersPerCycle)` when its task queue is empty, and each
due timer becomes a **task** in the queue. So a timer's callback runs:

- after the script that scheduled it finished,
- after the microtask queue was drained for that boundary,
- in due-time order, and
- one task at a time — a callback that schedules another timer does not run it in the same boundary.

The ordering statements are `EventLoop.md`'s; `Timers.md` is the half that says a timer is *due*, and it is the
queue that says *when*.

`JsTimerCancelAll` is what `JsRuntimeReset` calls, so a page's timers cannot outlive it — one of the four things a
reset releases (`Architecture.md` §5).

## 5. The script-visible doors

`setTimeout`, `setInterval`, `clearTimeout`, `clearInterval`, installed by `JsTimerInstallGlobals` when
`config.enableTimers` is set. The clear doors are the same door: an id from either setter clears either, which is
the specification's behaviour and the one a page relies on when it stores a mixed bag of ids.

Extra arguments after the delay are captured and passed to the callback (`JsTimerSet`'s `args`/`argCount`), so
`setTimeout(f, 0, 1, "two")` calls `f(1, "two")`.

A runtime with `enableTimers = 0` answers `RoboNotSupported` from `JsTimerSet` and does not install the four
globals — a supported configuration, and the one a one-shot evaluation uses.

## 6. What is absent

`requestAnimationFrame`/`requestIdleCallback` (a frame is a compositor's, not a script's), `setImmediate` (the task
door already exists), a `Date` object (so a script cannot read the clock at all — `JsCapabilities` has no `date`
flag, and the absence is deliberate: time reaches a script only as *ordering*, never as a wall-clock value), timer
nesting/clamping rules, and a drift correction for an interval whose callback ran long.
