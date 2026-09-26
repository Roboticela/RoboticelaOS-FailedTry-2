# Desktop input integration (Phase 13.66 – 13.77)

`Runtime/Source/RoboDesktop/RoboDesktopInput.c` + public
`Runtime/Include/RoboDesktop/RoboDesktopInput.h`.

This is the **one** place the shell and RoboInput meet. The shell was
`DesktopObservePointer`: a point and a pressed flag, enough for raise and close and
nothing else. This layer is the join — every event arrives as a `WindowEvent`, goes
through `InputEventFromWindowEvent`, and every decision it makes is one of
RoboInput's own doors. There is no `DesktopMouse` and no `DesktopKey` in the tree.

## 1. What it owns

| Owned | Borrowed |
|---|---|
| one `InputSession`, one `InputQueue`, one `InputDispatcher` | the `Desktop` it drives (it never outlives it) |
| the shell's listener store, one `InputShortcutManager`, one `InputCommandRouter` | the caller's content record (`DesktopInputContent`) |
| one `InputCursorManager`, one `InputCoalescer`, one `InputLatencyTrace` | the caller's clock (`DesktopInputConfig::clockMs`) |
| the gesture state: the moving window and the resize zone | the document's layout tree, DOM handle and editor |

It never draws. A frame is still `DesktopRender`'s; this layer only marks what must be
redrawn and reports counts.

## 2. The loop, in order (13.76)

```
pump      the window queue, bounded by the caller's wait, through the bridge into an InputQueue
drain     one InputEvent at a time
  route    the InputRoute from the event window's geometry (13.71)
  target   the hit test (13.70), or the focused target for a key (13.69)
  walk     capture -> target -> bubble through the shell's listeners (13.31/13.32)
  act      the DefaultAction RoboInput identified (13.34)
  measure  the latency record, when a clock was supplied (13.48)
  cursor   the shape in force, re-decided after the listeners ran (13.75)
report     what happened, as counts
```

`GetDesktopInputStep` is pump + drain as one call; `DesktopInputPush` + `GetDesktopInputStep`
is the two-call shape a test uses.

### Capture

A **captured** pointer's moves, release and cancel go to its holder **whatever is under
it — including a position outside the screen**. That is what capture means, and it is
why a window dragged off the desktop still follows the pointer and a release that lands
outside the desktop still ends the gesture. No hit test runs for a captured move.

## 3. The shell as a consumer (13.66 / 13.67)

- **A listener.** Raise, close, begin a move on the title bar and anchor a resize on an
  edge are one `InputListener` on the window target for `POINTER_DOWN`, plus one for
  `POINTER_UP`. The listener store has no per-type mask, so each callback states the
  type it answers; a press that was also read as a release would end the gesture it began.
- **A provider.** The three seams `RoboInputDispatch.h` names — hit test, path, listeners
  — are supplied from the shell's window list and its listener store.

### Move and resize

The two gestures a window system cannot be without:

| gesture | armed by | carried by | ended by |
|---|---|---|---|
| move | a press on the title bar | `DesktopWindowSetBounds` under the model's capture | release |
| resize | a press on one of the eight edge/corner bands | the *press's* zone, against the *press's* frame | release |

The resize zone is recorded at the press and never re-derived: a resize that re-hit-tested
on every move would jump to another edge the moment the pointer crossed one. Both edges
of an axis are moved relative to the frame the gesture began with, so dragging an edge 40px
out and 40px back returns the original rectangle exactly.

## 4. Content (13.68 / 13.70 / 13.72 / 13.73 / 13.74)

`DesktopInputSetContent` describes a window's contents. Every field is optional and every
absent one degrades to a **stated** answer:

| field | with it | without it |
|---|---|---|
| `tree` | a client-area press resolves to a `DOCUMENT` target with the `PaintHitTest` node | the press is the window's |
| `documentNode` | the target carries a DOM identity a consumer resolves | no identity travels |
| `editor` | text commands reach a `RoboTextEditor` (12.22) | text is unclaimed |
| `commands` | the caller's application commands are asked after the editor | the slot declines |

A window's contents answer the keyboard only while that window **has** the keyboard:
`ContentCommandsExecute` compares the active/focused window and declines otherwise, so a
background window's editor cannot type what a key aimed at the focused one produced.

Copy, cut and paste are the **shell's** clipboard, not the editor's: the slot declines them
so the walk reaches `ShellClipboardExecute`, which reads the editor's selection and writes
the one clipboard the OS has.

### The one thing this build does not have

The shell's windows hold a `PaintDisplayList`, not a `LayoutTree`, so **nothing here can
hit-test a node inside a window** unless a caller registers a real document. That is why
`DesktopInputSetContent` exists and why the scene registers the document's tree on every
rebuild.

## 5. Focus (13.69)

Focus is per-window. `DesktopWindowRaise` and the model's focus manager are kept in step
from one door — `ActivateWindow` — rather than from two that could drift. `Tab`/`Shift+Tab`
and `DesktopInputPerformCommand(…FocusNext/…FocusPrevious)` activate the next window and
then ask it to take focus; they do **not** walk a model focus chain across windows, because
a chain target must belong to the active window (it names a control *inside* one).

A key with no focus goes to the front window. A focus on a window the shell no longer holds
is dropped and the event falls through to the front one.

## 6. Coordinates (13.71)

`DesktopInputRouteForWindow` is the shell's own geometry read once: `windowBounds` in screen
space, `clientBounds` in **window** space (the screen rectangle less the frame), which is
`InputRoute`'s documented meaning. A point that arrives on another plane is converted through
the event window's route (or the front window's) before the hit test; a conversion that cannot
be done is refused rather than guessed at.

## 7. Cursor (13.75)

`InputCursorManager` decides the shape in one place. The gesture declarations — `grabbing`
for a move, `resize-h`/`-v`/`-diagonal` for the edge being dragged — are overrides, removed
when the gesture ends. The manager is asked **again after the listeners ran**, so a shape a
gesture installed (or removed) is in force in the same step rather than one move later.

## 8. Measurement (13.77.12)

One `InputLatencyTrace` record per dispatched event, and only when the caller supplied a
clock. The two stamps are the event's own hardware timestamp and the caller's clock; a clock
on a different epoch produces a *refused* record rather than a plausible small number.
Without a clock the trace stays empty and every door answers zeroes — measured or absent,
never a guess.

## 9. Lifecycle (13.77.13)

`DesktopInputForgetWindow` takes a gone window's listeners, focus, captures, contents and
running gesture with it; a gesture that outlived its window would be a pointer captured for
good. `DesktopInputResetTransient` clears the transient session state, the two gestures and
the held-button flag. A destroyed system sets `live = 0`.

## 10. What is absent, and named

| Absent | Why |
|---|---|
| window snapping | no tiling policy and no gesture (Phase 14.17) |
| frame scheduling | the frame's *when* is the compositor's and the scene's (13.77.6) |
| cursor images | this layer names a shape; drawing one is the shell's |
| a control tree inside a window | the shell has no control nodes to focus or hit-test |
| per-window application windows | one window system; an application target is a value |

## 11. Capabilities

`GetDesktopInputCapabilities` reports one flag per seam named above, all 1, and the three
absences as 0 — each read field by field in `Tests/Desktop/SuiteRoboDesktopInput*.c`. The
version is `RoboInputVersionString`. `cursorShapes` is 13 (13.75's `grabbing`) and
`resizeZones` is 8 (13.67).

## 12. Tests

| Suite | Prefix | Covers |
|---|---|---|
| `Tests/Desktop/SuiteRoboDesktopInput.c` | `desktop.input.loop/devices/latency/capabilities/lifecycle` | the loop, devices, measurement, the table, lifecycle |
| `…InputPointer.c` | `desktop.input.hit_test/window_move/window_resize/coordinates/cursor` | where: hit test, the two gestures, planes, shape |
| `…InputContent.c` | `desktop.input.document/text/commands/clipboard/drag` | what is inside: node, editor, chords, clipboard, drag |

`TestDesktop.ps1 -Target Host` runs all of them; `-Target Qemu` runs `test-qemu-display`,
whose `ROBO_QEMU_INPUT_*` markers are emitted through the scene's own `DesktopInput`.
