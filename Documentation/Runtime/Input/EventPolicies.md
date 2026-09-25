# RoboInput — the four policy layers

Four phases after 13.51 add no hardware path and no second event model. Each adds a **policy** — a rule about
what may happen to events the model already produces — and each is a file of its own because each answers a
question no other layer can:

| Phase | File | Answers |
|---|---|---|
| 13.47 | `RoboInputCoalescing.h` | which events may be merged, and what the merge says |
| 13.48 | `RoboInputLatency.h` | how long the user waited, and at which stage |
| 13.52 | `RoboInputAccessibility.h` | what a semantic client may ask for, and what it may not |
| 13.54 | `RoboInputGamepadNavigation.h` | what a held controller means |

`Overview.md` §6 is the summary; this is the detail. `ApiStability.md` says which doors are frozen.

---

## 1. Coalescing (13.47)

**The rule is one function, and it is a complement.** `InputEventTypeMayCoalesce` answers `1` for exactly
`POINTER_MOVE`, `TOUCH_MOVE` and `PEN_MOVE`; **every other one of the thirty-three types is a barrier**,
including one a later phase appends, because merging a press, a release, a key, a repeat, a text event, a
composition change, a focus change or a hotplug either drops one or invents one. `input.coalescing.rule`
iterates the whole vocabulary and asserts each answer against `InputCoalesceTypes`, so the rule and the table
cannot drift apart.

**The key is *(device, pointer, type)*.** Two fingers are two slots and never one; a mouse and a stylus are two
devices. That is also why the merge is cheap — the key is already in the event.

**`Push` answers, it does not dispatch.** `EMIT` (send it now), `HOLD` (taken; do not dispatch) and
`FLUSH_FIRST` (drain before dispatching this one). The third is the whole ordering rule: a press that arrived
after four held moves must not overtake them, or a consumer would receive a press at a position it was never
told the pointer had reached.

**A merged record is the newest event, whole,** with two fields saying what they are:

| Field | What it says |
|---|---|
| `deltaX`/`deltaY` | the **sum** of every held event's deltas: the movement over the merged window, `0` where the platform reported none |
| `previous` | left as the newest event reported it, so `position - previous` still means *the last step* |

**Nothing is ever dropped.** A table with no free slot is a `FLUSH_FIRST`, not a refusal: the caller drains and
offers the event again. A flush against a queue with no room answers `RoboBusy` with the remainder **still
held**, so a slow frame costs a delayed move rather than a lost pointer. A delta sum that no `int32_t` can hold
is a `FLUSH_FIRST` as well — two movements stay two events rather than one wrapped into its opposite.

**The cost model is the point.** `input.coalescing.baseline` drives 960 moves in 60 frames: **60** queue
pushes, **900** merges, **0** refusals, and the first queued event carries all sixteen samples of its frame.
Emissions are proportional to frames and pointers, never to events — asserted as exact counts rather than
timed, because a wall clock would measure the machine rather than the model.

---

## 2. Latency (13.48)

Five stamps, taken where the work happens:

```text
Hardware ──→ Queue ──→ Dispatch ──→ Handler ──→ Present
```

| Measurement | Difference | What it answers |
|---|---|---|
| `dispatchMs` | Dispatch − Hardware | the delay before anything *could* react |
| `handlerMs` | Handler − Dispatch | how long the work took |
| `frameMs` | Handler − Hardware | the brief's *input-to-frame*: input to the frame that carries the result |
| `presentMs` | Present − Hardware | the brief's *input-to-present* |

`Begin` takes the first two stamps and hands out a handle (never 0); `Mark` takes the other three; `Measure`
answers for an unfinished record too, with `complete` saying whether all five are in.

**Out of order is refused, not saturated.** A stamp earlier than a stage already marked is `RoboInvalidArgument`
and `outOfOrderTotal++`; a `Begin` whose queue stamp precedes its hardware stamp is refused the same way.
Saturating to zero would report a *good* latency for a call site measuring from the wrong clock, and a
plausible wrong number is worse than a refusal. A stage may not be marked twice (`RoboAlreadyExists`), and the
two stages `Begin` owns may not be marked at all.

**A micro trace, not a profiler.** Sixteen records, no allocator: `Begin` takes a never-used slot, then the
oldest **finished** record (its numbers have already joined the aggregates), and answers `RoboBusy` only when
every record is still **live** — a caller timing more events at once than the trace holds. Completed records
keep their slot until it is taken, so "what did that event cost?" is answerable one event later. The aggregates
(`sum`/`max`/`last` per measurement) are what a diagnostics overlay prints.

Named absences: no sampling policy, no histogram, no per-target breakdown, no persistence. That is Phase 41's.

---

## 3. The accessibility lane (13.52)

```text
InputA11yRequest → InputA11yCommandMap → InputCommand → InputCommandRouter → a target
```

An accessibility client — a screen reader, a switch device, a voice layer, an automation harness — asks for a
**meaning**: activate, focus, move focus, insert text, delete, select, scroll, dismiss, toggle. What it never
does is synthesize a keystroke: that would have to guess a layout, a key and a modifier state, and would then be
indistinguishable from a keyboard in every log and every policy decision. There is no event in this lane.

**The policy is half the layer.** `InputA11yCommandAllowed` permits the text-editing family, the focus family
(activation, cancellation, back), and the view family; it refuses the four window commands and the three system
ones (`ToggleDebugOverlay`, `ResetInputState`, `Quit`) and `NONE`. A client that could close the window could
dismiss the surface it was describing. The check happens **before** the router, so a barred command cannot reach
a target by another name — and `InputA11yInvokeCommand` is the seam that makes that checkable, because no
request in the vocabulary maps onto a window command.

**Two of the brief's requests are named rather than approximated:**

| Request | Answer |
|---|---|
| `SET_VALUE` | `RoboNotSupported` — 13.13's vocabulary has no "set this control's value", and a control's value is not a platform-neutral meaning |
| `TOGGLE` | `RoboInputCommandActivate` — activating a two-state control *is* how its state changes, and the control owns the second state |

**Refusals are counted**: `routed`, `unclaimed` (the router said nobody wanted it), `failed`, `refused` (the
policy), `unsupported` (`SET_VALUE`), `rejected` (a malformed request). `lastOutcome`/`lastCommand`/`lastLevel`
are the last word for a diagnostics line, and the lane records its rank (`RoboInputPrivilegeAccessibility`) so a
log can say who asked.

Not here: an accessibility tree, a descendant walk, a name/role/value protocol, focus-by-semantics, a screen
reader. Phase 31 owns those and will call this seam.

---

## 4. Gamepad navigation (13.54)

13.53 landed a controller as *hardware*; this is the semantic map, and the two capability flags
(`gamepad`, `gamepadNavigation`) exist because they are two claims.

| Input | Action |
|---|---|
| D-pad buttons, then d-pad axes, then the left stick | `FocusUp`/`Down`/`Left`/`Right` by the **dominant axis** |
| A | `Activate` |
| B | `Back` |

Four rules, each stated once:

- **A deadzone is a number.** At or inside it is a rest, so the comparison is strict (`0` means only real
  movement counts, and an all-zero pair is still nothing). Outside it, the larger magnitude wins and a **tie
  goes vertical**, because a stick at 45° must produce one direction rather than none.
- **The d-pad beats the stick, and a face button beats both.** The d-pad's buttons are checked in a fixed order
  (up, down, left, right), then its axes, then the stick; a pressed A or B suppresses any direction. That is what
  makes an activation *one* action instead of an activation interleaved with repeats of the direction underneath.
- **Held directions repeat; edges do not.** A direction emits on change, then after `repeatDelayMs`, then every
  `repeatIntervalMs` — driven by the caller's clock, so a test simulates time rather than sleeping. `Activate`
  and `Back` are **edges**: once on the press, and a repeat on `A` would be a second click.
- **A disconnect must not leave a direction stuck.** `InputGamepadNavigationReset` forgets held buttons, axes
  and the repeat deadline while keeping the configuration and the counters — the gamepad shape of 13.44's rule.

`InputGamepadNavigationActionCommand` turns an action into an `InputCommand` (`FOCUS_NEXT`,
`FOCUS_PREVIOUS`, `FOCUS_DIRECTION` + direction, `ACTIVATE`, `BACK`), so a controller reaches the same target
chain as a shortcut and an accessibility request. The `CENTERED` flag is honored as *exactly zero* rather than
as "a small number", and the axes that mean nothing to focus (the right stick, the two triggers) are ignored
rather than guessed at.

Not here: polling, a device handle, rumble, vibration, analog deadzone curves beyond one number, per-application
binding tables, and a guest-side driver — 13.6's platform adapter is still the missing half, so whoever
produces 13.53 events feeds this.
