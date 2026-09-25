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
   ↓  pointer / touch / pen / gamepad (13.14–13.24, 13.53)
   ↓  focus (13.25–13.28)
   ↓  hit test → routing (13.29–13.36)
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

## 6. What this build does *not* do

Each of these is a **0** in `InputCapabilities`, asserted field by field by `input.conformance.capabilities`,
so a reader cannot mistake a model for a system and a change that flips one must add the case that reads it:

| Flag | 0 means |
|---|---|
| `deviceReading`, `platformAdapters` | nothing opens a keyboard, a mouse, a digitizer or a controller; the bridge that exists is a *translation* of a RoboWindow event |
| `dispatch`, `propagation` | no event is routed, no listener is called, nothing bubbles and nothing is captured |
| `focus` | no focus is tracked, no scope, no Tab navigation |
| `textEditing` | text reaches **no** editor yet: the pipeline produces `TEXT_INPUT` events and 13.12/13.13's command layer is what will carry them into RoboText's editor |
| `textComposition` | no input **method**: no preedit, no candidates, no candidate window. A dead key is layout data and is deliberately *not* this |
| `clipboard`, `gestures`, `eventLoop`, `multiprocessInput` | named absences, not oversights |

---

## 7. Case study: one keystroke, end to end

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
5. **Focus and routing** (later) decide the event's target and hand it to the focused editor. A key event was
   delivered at step 2 as well, for whatever else wanted it — a shortcut, a control, a game.
6. **RoboText's editor** (12.22) performs the one mutation: a replacement of a source range. Shaping,
   measurement, layout and painting are its downstream consequences, and RoboInput never sees a glyph.

Every step is a separate function in a separate file, and each one answers a question the next cannot answer
for it. That is the whole design.

---

## 8. Diagnostics

Every structure in the module has a `Write` door that is **complete or absent**: a dump reports the length a
complete line needs and leaves `?` in the buffer when it does not fit, so a caller that ignored the refusal
cannot read a truncated record as a whole one. `InputWriteModel` writes the *vocabulary* (every event type with
the payload it owns and the facts about it, every source, every plane) — the door a developer reads first and a
test asserts whole.
