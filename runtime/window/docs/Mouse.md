# RoboWindow Mouse Foundation (Goal 7.11)

Platform-independent mouse events on top of the Goal 7.9 event loop and
Goal 7.10 modifier model.

Chromium `WebMouseEvent` / `ui::MouseEvent` are **technical reference only** —
not linked or copied.

## Architecture

```text
Native OS mouse messages
        ↓
Native backend translation (platform/win32)
        ↓
robo_window_observe_mouse
        ↓
mouse state tracker (+ shared key modifiers)
        ↓
ROBO_WINDOW_EVENT_MOUSE_* queue
        ↓
Application poll
```

Mouse events stay distinct from keyboard, text/IME, touch, stylus, and any
future generic pointer layer.

## Button model

Stable Roboticela identifiers (`robo_mouse_button_t`):

| Identifier | Meaning |
|---|---|
| Left / Right / Middle | Primary buttons |
| X1 / X2 | Auxiliary (browser back/forward style) when native exposes them |
| Unknown | Unmapped native button — logged, not queued |

Raw OS button numbers are never the public API.

## Event types

`robo_window_event_t.mouse` is valid for:

* `ROBO_WINDOW_EVENT_MOUSE_MOVE`
* `ROBO_WINDOW_EVENT_MOUSE_BUTTON_DOWN`
* `ROBO_WINDOW_EVENT_MOUSE_BUTTON_UP`
* `ROBO_WINDOW_EVENT_MOUSE_WHEEL`

Payload: type, button, client/screen coords, delta, scroll, click_count,
modifiers (`robo_key_modifier_flags_t`), timestamp, window_id.

## Coordinate semantics

| Field | Origin | Axes | Units |
|---|---|---|---|
| `client_x/y` | Top-left of **window client** area | +x right, +y down | Integer **logical** DIPs (same space as client size) |
| `screen_x/y` | Virtual desktop (when `has_screen_coords`) | same | Logical where the backend converts; Win32 uses screen pixels that match the window’s logical client mapping via `ClientToScreen` |
| `delta_x/y` | — | — | Change since last tracked client position (or backend-supplied) |

Do **not** treat client coordinates as framebuffer physical pixels. Use
`robo_window_get_scale` / `robo_window_get_size_physical` and
`robo_display_*_logical_to_physical` when physical pixels are required
(Goal 7.8).

## Movement

Absolute client position is always recorded. Delta is computed from the
previous sample when the injector leaves delta at zero. First sample after
reset has delta 0. No synthetic move events are invented by the core.

## Buttons & state

Press/release update `robo_mouse_is_button_pressed`. Multiple buttons may be
down together. Unknown buttons do not update pressed state.

### Stuck-button prevention

Pressed buttons clear on:

* focus loss
* window destroy
* runtime / subsystem shutdown (`robo_mouse_reset`)

## Click count

If the backend supplies `click_count` (e.g. Win32 double-click messages → 2),
it is preserved. Otherwise a small foundation synthesizes counts using
500 ms + 4 px slop — not a gesture engine. No drag, context-menu policy, or
app click semantics.

## Wheel / scroll

`scroll_x` / `scroll_y` use `ROBO_MOUSE_SCROLL_UNIT_LINE` where **|120| = one
discrete notch** (Win32 `WHEEL_DELTA`). High-resolution devices may send
smaller magnitudes; precision is kept. Zero deltas are ignored. Smooth
scrolling behavior is deferred to later layout/compositor work.

Sign: positive `scroll_y` = away from user (“up” / content scrolls down on
typical UIs); positive `scroll_x` = right.

## Modifiers

Reuse Goal 7.10 `robo_key_modifier_flags_t`. Win32 mouse translation calls
`robo_keyboard_win32_query_modifiers()`. No second modifier system.

## Targeting

* Prefer the window ID from the native HWND → slot mapping.
* `ROBO_WINDOW_ID_NONE` → captured window if soft/native capture is active,
  else focused live window.
* Destroyed / unknown → `ROBO_NOT_FOUND` (no use-after-free).

No DOM/layout hit-testing in this goal.

## Capture foundation

* Soft capture: `robo_window_mouse_capture` / `release` / `captured_id`
* When `caps.mouse_capture` is set (Win32), also calls `SetCapture` /
  `ReleaseCapture` through optional backend vtable entries.
* Win32 additionally `SetCapture`s on button-down so release outside the
  client still arrives; that transient OS capture is not always reflected
  in `robo_window_mouse_captured_id` unless the public capture API was used.

Not implemented: drag-and-drop, browser pointer capture, gestures.

## Native backend (Win32)

Handles: `WM_MOUSEMOVE`, button down/up/dblclk (L/R/M/X), `WM_MOUSEWHEEL`,
`WM_MOUSEHWHEEL`.

## Intentionally deferred

* Touch / stylus / generic pointer
* Gestures, pinch/zoom, smooth scroll policy
* Hit-testing into HTML/layout
* Clipboard
* QEMU-specific mouse architecture (reuse this translation when QEMU
  surfaces events through the same loop)

## Tests

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter "window.mouse."
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter "window.host.mouse"
```
