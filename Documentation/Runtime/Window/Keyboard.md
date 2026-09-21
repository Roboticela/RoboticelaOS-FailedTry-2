# RoboWindow Keyboard Foundation (Goal 7.10)

Platform-independent keyboard events on top of the Goal 7.9 event loop.

Chromium `KeyEvent` / DomCode / DomKey and W3C UI Events `code`/`key` are
**technical reference only** — not linked or copied.

## Architecture

```text
Native OS keyboard messages
        ↓
Native backend translation (platform/win32)
        ↓
WindowObserveKey
        ↓
keyboard state tracker
        ↓
RoboWindowEventKeyDown / KEY_UP queue
        ↓
Application poll
```

Text input, IME, composition, and character generation are **not** part of
this goal.

## Physical vs logical

| Concept | Meaning |
|---|---|
| `PhysicalKey` | Hardware-oriented position (scan-code based on Win32). Stable across layouts when the OS exposes a scan code. |
| `LogicalKey` | Interpreted key identity for this event (virtual-key based). May vary with layout for writing-system keys. |

Logical keys are **not** Unicode text. Letters mean “the key named A”, not
the character `a`.

Unknown/unmapped native keys become `*_UNKNOWN` and do not crash the runtime.

## Events

`WindowEvent` carries `key` (`KeyboardEvent`) when type is:

* `RoboWindowEventKeyDown`
* `RoboWindowEventKeyUp`

Fields: action, physical, logical, modifiers, `is_repeat`, `timestamp_ms`
(0 if unavailable), `window_id`.

### Repeat

Native auto-repeat arrives as additional `KEY_DOWN` with `is_repeat != 0`.
Release is a single `KEY_UP`. No custom text-repeat scheduler.

### Modifiers

Flags: Shift / Ctrl / Alt / Meta (Super), Caps/Num/Scroll Lock, and left/right
variants when the backend can distinguish them.

## Targeting

* Prefer the window ID supplied by the native HWND → slot mapping.
* `RoboWindowIdNone` resolves to the first live focused window.
* Destroyed / unknown IDs → `RoboNotFound` (no use-after-free).
* No focused window → `RoboNotFound` for delivery (state may still update).

Focus management beyond existing `observe_focus` / `request_focus` is deferred.

## State queries

* `KeyboardIsPhysicalPressed`
* `KeyboardIsLogicalPressed`
* `KeyboardModifiers` / `KeyboardModifierActive`

### Stuck-key prevention

Pressed keys clear on:

* focus loss (`observe_focus(..., 0)`)
* window destroy
* runtime / subsystem shutdown (`KeyboardReset`)

## Win32 translation

| Source | Mapping |
|---|---|
| Virtual key (`wParam`) | logical key |
| Scan code + extended bit (`lParam`) | physical key |
| `GetKeyState` | modifier snapshot |
| Bit 30 of `lParam` | repeat |
| `GetTickCount64` | timestamp_ms |

Messages: `WM_KEYDOWN` / `WM_KEYUP` / `WM_SYSKEYDOWN` / `WM_SYSKEYUP`.

### Limitations

* Not every OEM / media key is mapped (→ UNKNOWN).
* Layout-dependent letter physical positions assume PC/AT set-1 scan codes.
* `SendInput` smoke may miss the window if OS focus is stolen; tests fall back
  to `observe_key` injection.
* No IME / WM_CHAR handling in 7.10.

## Mock injection

Tests call `WindowObserveKey` with the same structures as the native
path (`window.key.*` suite).

## Deferred

Mouse, pointer, touch, clipboard, Unicode text events, IME, dead keys,
composition strings, editable text (later Phase 7 / Phase 12 work).
