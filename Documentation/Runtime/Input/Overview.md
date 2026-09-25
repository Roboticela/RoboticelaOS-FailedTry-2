# RoboInput — Overview

**RoboInput is the one input system in RoboticelaOS.** It sits between hardware and every consumer of user
input — the desktop shell, windows, documents, RoboHTML/RoboDOM, RoboJS, and RoboText's editor — and it is
the only place a device reading becomes an event, a key becomes a character, or a point becomes a hit.

There is **one** pipeline and no parallel one:

```text
hardware
   ↓
kernel / device layer
   ↓
device adapter        (not built yet: `platformAdapters` is 0)
   ↓
RoboInput             ← the normalized event model
   ↓  device state (13.2):        the table, the keyboard's held keys, auto-repeat
   ↓  keyboard (13.9–13.11):      layout → pending accent → TEXT_INPUT
   ↓  pointer / touch / pen / gamepad (13.14–13.24, 13.53–13.54)
   ↓  focus (13.25–13.28)
   ↓  hit test → routing (13.29–13.36) → commands and shortcuts (13.12–13.13, 13.35–13.36)
   ↓  the semantic lanes above the router (13.52 accessibility)
   ↓  coalescing and latency around the queue (13.47–13.48)
   ↓
target: DesktopShell / RoboDOM / a text editor
   ↓
RoboText (12.22) for text; the display list and the rasterizer below that
```

Everything above the "RoboInput" line is a **report**; everything below it is a **consumer**. The rules that
keep it that way are in §Root rules of `AGENTS.md`, and the ones this build can check mechanically are a build
step (`Assert-RoboInputBoundaries`, run by `-Verify`).

---

## 1. What an event is

`InputEvent` (`Runtime/Include/RoboInput/RoboInputEvent.h`) is a **value**: trivially copyable, owning no
pointer into anything the runtime owns, so it can sit in a queue, be copied into a log, or be handed to a test
without a lifetime agreement. One event is:

| Field | Why it is there |
|---|---|
| `type` | what happened — a **stable, append-only** vocabulary (13.3) |
| `source` | which *category* of input reported it (keyboard, mouse, touch, pen, gamepad, remote, synthetic) |
| `device` | which device, as a copied identity (id + name) |
| `timestampMs` | monotonic milliseconds; **never a wall clock**, and 0 means "the platform does not say" |
| `modifiers` | the modifier state *as of this event*, so no consumer infers it |
| `space` | the plane every position in the payload is on |
| `window` / `display` | where the runtime believes it happened |
| `target` | what it is *about*, as inert identity, filled in by a dispatcher |
| `payload` | a union with exactly the one member the type owns |

Two decisions here are worth stating because they are the ones a later change tends to undo:

- **the payload is a union**, not three fields with comments saying which types make each meaningful. A
  responder *switches on the type* to decide which fields it may read, so that mapping wants to be one fact
  rather than a convention. It also means a debug dump cannot print a stale wheel delta a previous event left
  in the struct.
- **`space` is set exactly when the event has a position.** The commonest coordinate mistake is not converting
  between two planes, it is never saying which plane a number is in — and there is no honest answer to give
  for a keystroke. `InputEventIsValid` refuses an event that gets this wrong.

`InputEventIsValid` refuses a record whose fields disagree about what it is, and the seven rules are listed in
full in the header. `InputEventIsValid` is what a backend's event must survive before anything routes it.

---

## 2. Devices (13.2)

`InputDeviceDescriptor` says what the hardware **is**: seven device kinds (`keyboard`, `mouse`,
`touchscreen`, `touchpad`, `pen`, `gamepad`, `unknown`), eleven capability bits, a connection state, a
vendor/product id where the platform has one, and a copied name. A device **names no driver**: RoboInput does
not load one, and no driver includes RoboInput's private headers. A driver produces normalized events; this
module consumes them.

`InputDeviceRegistry` is a fixed table (a stated bound, no allocator, nothing to destroy). Its one rule that
matters is that **disconnect removes**: the entry is gone, the removed descriptor is handed back, and a
registry flag that marked it "disconnected" would leave a stale device findable by id, askable for as a type's
primary, and — worst — still holding a key. The returned descriptor is how the caller knows *which* id to
release.

---

## 3. Keyboard state (13.2)

`InputKeyboardState` keeps **one** thing: a held table of physical keys. Everything else is derived —

- the modifier state is read off the held table, so a device disconnect cannot leave `Ctrl` stuck;
- the lock bits (caps lock, num lock, scroll lock) are the one thing that *is* kept, because a lock is not a
  key that is down;
- auto-repeat is **one event per call** from a deadline the state pushes forward, driven by a clock the caller
  supplies. A test simulates time rather than sleeping, and a repeat is never generated after a release.

`InputKeyboardStateRepeatConfig` holds the initial delay and the interval. Nothing here reads a device:
`deviceReading` remains 0 until a platform adapter exists.

---

## 4. Coordinate spaces (13.71)

Four planes are named and each conversion is explicit: `SCREEN`, `WINDOW`, `DOCUMENT`, `APPLICATION`, with
`InputConvertPoint` refusing a conversion it does not have the geometry for rather than guessing. An event
carries the plane it arrived in and is **never modified** in place; a consumer that wants another plane asks
for it (`InputEventPositionIn`), and a target attached to the event must have been concluded in the event's own
plane, so "where the pointer is" and "what it is over" cannot be two answers to one question.

---

## 5. The queue, and the thread model

`InputQueue` is an **owned** FIFO — it copies what is pushed into it and hands a copy back on a pop, so no
caller's stack is referenced. Ordering is `(timestampMs, insertion)`, which is what makes a replay of a
recorded sequence reproduce the same order even when two events share a millisecond.

The recommended model, and the one the module is built for: a device thread **pushes**, and the UI thread
**pops and dispatches**. No UI object is mutated from a device thread or an interrupt. The queue is the only
synchronization boundary, and it is the caller's to put a lock around if it needs one — the module owns no lock
and no global state, which is also why there is no `InputInit` to call.

---

## 6. The four lanes above the queue: coalescing, latency, accessibility and gamepad navigation

The phases after 13.51 are all *policy* layers rather than new hardware paths, and each one is a file of its
own because each answers a question the model does not:

**Coalescing (13.47)** — `RoboInputCoalescing.h`. A mouse can report a thousand moves a second and a frame
draws sixty times, so the moves that happened *between two frames* are merged. The rule is one function,
`InputEventTypeMayCoalesce`, and it answers `1` for exactly three types (`POINTER_MOVE`, `TOUCH_MOVE`,
`PEN_MOVE`); every other one of the thirty-three is a **barrier**, which is the safe default for a type a later
phase appends. The table is keyed on *(device, pointer, type)*, so two fingers are two slots and never one, and
the merged record is the **newest** event whole with the window's movement in `deltaX`/`deltaY`. Three answers —
`EMIT`, `HOLD`, `FLUSH_FIRST` — and nothing is ever dropped: a full table asks the caller to drain, and a flush
against a full queue answers `RoboBusy` with the remainder **still held**.

**Latency (13.48)** — `RoboInputLatency.h`. Five stamps taken where they happen (hardware, queue, dispatch,
handler, present) and four differences: input-to-dispatch, dispatch-to-handler, input-to-frame and
input-to-present. A stamp that arrives **before** a stage already marked is refused and counted rather than
saturated — a plausible *good* number is the one failure mode this layer cannot have — and a `Begin` whose
queue stamp precedes its hardware stamp is refused for the same reason. The records are a small fixed table
(one frame's worth): a never-used slot first, the oldest finished record after that, and `RoboBusy` when every
record is still live.

**The accessibility lane (13.52)** — `RoboInputAccessibility.h`. A screen reader, a switch device or a voice
layer asks for a *meaning* — activate, focus, move focus, insert text, delete, select, scroll, dismiss,
toggle — and the lane maps it onto this module's own command vocabulary and routes it through the same
`InputCommandRouter` a keyboard shortcut uses. It **never** synthesizes a keystroke: a synthetic scan code
would have to guess a layout, a key and a modifier state, and would then be indistinguishable from a keyboard
in every log and every policy decision. `InputA11yCommandAllowed` is the other half: the text, focus and view
families plus cancellation, and **not** the window family, the debug overlay, the reset or quit — a client that
could close the surface it was describing would be able to dismiss the thing it was reading. Two of the brief's
requests are named rather than approximated: `SET_VALUE` has no command behind it (`RoboNotSupported`), and
`TOGGLE` is an activation because the control owns its second state.

**Gamepad navigation (13.54)** — `RoboInputGamepadNavigation.h`. 13.53 landed a controller as *hardware*; this
is the semantic map the brief names separately, and the two capability flags exist because they are two
claims. D-pad and left stick become focus moves by a **dominant-axis** rule (a stick at 45 degrees is one
direction, ties go vertical), the d-pad beats the stick, and a pressed A or B beats both so an activation is
one action rather than an activation interleaved with repeats. A held direction repeats on the same cadence
*shape* as 13.8, driven by the caller's clock so a test simulates time; an `Activate` or a `Back` is an edge and
never repeats. `InputGamepadNavigationActionCommand` turns an action into an `InputCommand`, so a controller
reaches the same target chain as a shortcut and an accessibility request. `InputGamepadNavigationReset` is the
gamepad shape of "a disconnect must not leave keys stuck".

---

## 7. What this build does *not* do

Each of these is a **0** in `InputCapabilities`, asserted field by field by `input.conformance.capabilities`,
so a reader cannot mistake a model for a system and a change that flips one must add the case that reads it:

| Flag | 0 means |
|---|---|
| `deviceReading`, `platformAdapters` | nothing opens a keyboard, a mouse, a digitizer or a controller; the bridge that exists is a *translation* of a RoboWindow event, and every device event in this tree is produced by a caller or a test |
| `textComposition` | no input **method**: no preedit, no candidates, no candidate window. A dead key is layout data and is deliberately *not* this; the *interface* (13.38/13.39, `inputMethods`) is built and its client is the editor |
| `eventLoop`, `multiprocessInput` | named absences, not oversights: there is no input thread (the queue is the boundary the caller's thread crosses) and no cross-process input |

And two absences that are not flags, because they are *other modules'* rows: **no consumer**. Nothing outside
RoboInput includes it — the shell still receives a `RoboWindow` event through `RoboDesktopObservePointer` — so
focus, capture, the wheel, shortcuts and the clipboard this module models are unreachable from the desktop
until that seam is wired. Naming it here is the point: `dispatch` being 1 says the module routes, not that the
shell is routed.

---

## 8. Case study: one keystroke, end to end

Pressing `'` and then `e` on a US-International keyboard:

1. **The adapter** (later) reports a key down with a physical key, an up, and nothing else.
2. **`InputKeyboardStateKeyDown`** records the key in its held table and derives the modifier state. A
   **`KEY_DOWN`** event is constructed with the device, the timestamp, the modifiers and the window.
3. **`InputTextPipelineTranslate`** asks the layout what that position means at the level the modifiers select.
   The quote key at the base level is a **dead acute**, so *no event is produced* and an accent is armed with
   the timestamp that armed it.
4. `e` arrives. The pipeline composes — `InputDeadKeyCompose(acute, 'e')` is `U+00E9` — and produces **one**
   `TEXT_INPUT` event carrying `é`, with the key event's device, timestamp, modifiers and window, and **no
   plane**.
5. **Focus and routing** (13.25–13.36) decide the event's target: the focused chain innermost-first, with the
   capture and bubble phases around it and a default action after it. The text event reaches the focused
   editor's command target; a key event was delivered at step 2 as well, for whatever else wanted it — a
   shortcut, a control, a game. If the press were a move rather than a keystroke, **coalescing** (13.47) would
   have merged it with its neighbours before any of this, and **latency** (13.48) would have stamped each
   stage it passed.
6. **RoboText's editor** (12.22) performs the one mutation: a replacement of a source range. Shaping,
   measurement, layout and painting are its downstream consequences, and RoboInput never sees a glyph.

Every step is a separate function in a separate file, and each one answers a question the next cannot answer
for it. That is the whole design.

---

## 9. Diagnostics

Every structure in the module has a `Write` door that is **complete or absent**: a dump reports the length a
complete line needs and leaves `?` in the buffer when it does not fit, so a caller that ignored the refusal
cannot read a truncated record as a whole one. `InputWriteModel` writes the *vocabulary* (every event type with
the payload it owns and the facts about it, every source, every plane) — the door a developer reads first and a
test asserts whole.
