# RoboInput — API stability

**RoboInput's public surface is `Runtime/Include/RoboInput/`, and this file is what `RoboInputApiFrozen` = 1
means door by door.** Nothing under `Runtime/Source/RoboInput/` is part of it — only this module's own sources
include `RoboInputInternal.h`, and nothing outside the module includes a `Runtime/Source/...` path.

## The tiers

| Tier | What | Change rule |
|---|---|---|
| **Stable** | `RoboInput.h` and every header it names; every `Input*` function declared there; every enumerated *value*; every public struct's field meaning | Existing values, signatures, invariants and field semantics do not change. A door may be added; a value may be appended **after** the last one; a field may be appended to the end of a struct |
| **Advanced** | The seams a caller plugs into: `InputCommandTarget`, `InputHitTestProvider`, `InputClipboardTextSource`, `InputInjectionPrivilege`, the recognizer vtable of `RoboInputGesture.h` | Stable, and deliberately shaped as *(userdata, function)* pairs so a vtable's lifetime is a caller's choice rather than this module's |
| **Internal** | `RoboInputInternal.h` (`InputWriter`, the bounded string helpers); any `Ri*` symbol; the `*Internal.h` of a module that names one | Not installed, not stable, and nothing outside `Runtime/Source/RoboInput/` may include it |
| **Experimental** | none | — |
| **Deprecated** | none | — |

Two append-only rules are asserted rather than trusted: `input.conformance.vocabulary` pins every enumerated
value positionally, and `input.conformance.capabilities` reads **every** field of `InputCapabilities` — so a
value inserted in the middle of an enum, or a flag that quietly changed, fails a case rather than a consumer.

## Ownership and lifetime

- **This module owns no global state and allocates nothing of its own.** There is no `InputInit` and no
  singleton. Everything a caller uses is either a **value** (`InputEvent`, `InputModifiers`, a device identity,
  a request) or an object the caller creates and destroys (`InputQueue` is the only allocator-backed one;
  every other table is embedded in a struct the caller declares).
- **An event is a value, trivially copyable, and owns nothing.** It may sit in a queue, be copied into a log,
  or be handed to a test with no lifetime agreement. `InputEventIsValid` is what says whether a record may be
  used at all.
- **Tables are fixed-capacity and embedded.** `InputCoalescer`, `InputLatencyTrace`, `InputGamepadNavigation`,
  `InputA11yState`, the focus manager, the device registry, the pointer table and the gesture arena are *by
  value* in the caller's scope. A full table **refuses** (`RoboBusy`, or a stated answer) rather than dropping
  or evicting; where a bound exists it is published as a constant and mirrored in `InputCapabilities`.
- **A borrowed pointer is named as borrowed.** `InputCommandRouter` holds targets by value (a function and a
  `userdata`); a clipboard text source, a hit-test provider and an edit-cluster resolver are **borrowed** and
  never freed by this module. A door that borrows says so in its own comment.
- **Nothing references a caller's stack after a call returns.** The queue copies; a coalescer copies the
  events it holds; a latency record copies the stamps it is given.

## Threading

**A device thread pushes; the UI thread pops and dispatches; the queue is the only synchronization boundary.**
This module owns no lock, no thread and no atomic.

- `InputQueuePush` is the producer's door and `InputQueuePop`/`Peek`/`PeekAt` are the consumer's; the queue's
  ordering rule is `(timestamp, insertion)`, which is what makes a push from one thread and a pop from another
  mean something.
- Every other structure is **single-threaded**: a coalescer, a trace, a navigation state, a device registry, a
  focus manager and a gesture arena are touched by the thread that owns them (the UI/event thread's). A
  consumer on another thread must not call them, and no door here takes a lock to pretend otherwise.
- No UI object may be mutated from a device thread: a producer's whole job is to normalize and push
  (`RoboInputBridge.c`'s translation is a pure function of a record it was handed).

## Invalidation

- **A destroyed target is forgotten by identity.** `InputFocusManager` and the capture/hover tables are told
  through `TargetReleased`; a deactivated window goes through `InputSessionDeactivateWindow`, which drops the
  captures held in it, the focus it had and the transient gesture. A destroyed target must not remain focused
  or captured — that is the rule the session exists for.
- **A device disconnect removes its state.** `InputSessionDisconnect` sweeps every table a device can put state
  in (keys, pointers, clicks, hovers, touches, the captures those pointers held, the repeat, a pending accent)
  and returns a record of what it released. A gamepad's equivalent is `InputGamepadNavigationReset`.
- **A reset is one command.** `RoboInputCommandResetInputState` (13.78) cancels every transient state at once.
- **The counters are never reset by a clear.** `InputCoalescerClear` and `InputRouterClear` keep their totals: a
  counter that a reset zeroed would be the one nobody could explain.

## Errors

- **A refusal names a boundary.** Every implementation site returns through `RoboInputDiag`, and a diagnostics
  record carries the **domain** (`RoboInputDomainSession`, `…Accessibility`, `…Gamepad`, …) so a caller can see
  *which* rule refused rather than only that something did.
- **"Not mine" is not a failure**, and it is not this module's `RoboNotSupported` in one place: a command
  target that declines continues the walk; a client that is not permitted is refused *before* the router.
- **A count backs the interesting refusals.** `outOfOrderTotal` (a latency stamp that went backwards),
  `refusedTotal` (an accessibility policy refusal, a cleared band), `unclaimedTotal` (a command no target
  claimed — the hardest input bug to see) and `dropped` (queue pushes refused by the bound) are the numbers a
  diagnostics page prints.
- **A dump is complete or absent**: it reports the length a complete line needs and leaves `?` in the buffer
  when it does not fit, so a truncated record can never read as a whole one.

## What this freeze is not

It is **additive, not inert**, and the version stays 0.x for that reason. The phase landed by *appending* —
thirty-three event types after thirteen, a payload kind after another, a capability field beside its
neighbours — and that is the only shape of change this contract allows without it being a deliberate break.
What is frozen is that the *existing* values, signatures and invariants mean what they say today.

## Two absences, named

- **No consumer.** `dispatch` being 1 says the module routes, not that the shell is routed: nothing outside
  RoboInput includes it yet, and wiring that seam is a later phase's row rather than a change to this contract.
- **No platform adapter.** `deviceReading`/`platformAdapters` are 0: every device event in this tree is
  produced by a caller or a test, and a real keyboard's bytes reach this module only through RoboWindow.

See `Overview.md` for the pipeline, `AGENTS.md` for the rules a change must keep, and the per-area documents in
this directory (`KeyboardLayouts.md`, `TextInput.md`, `EventPolicies.md`, which covers the four policy layers
of 13.47/13.48/13.52/13.54) for the details of each.
