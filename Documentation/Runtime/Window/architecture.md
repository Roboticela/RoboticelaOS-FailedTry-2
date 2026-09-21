# RoboWindow Architecture (Goal 7.1)

## Purpose

RoboWindow is the platform-independent windowing layer above RoboGraphics.
Goal 7.1 establishes the model and contracts; later goals add native windows,
QEMU presentation, and a real event loop.

## Modules

```text
RoboWindow API
      │
      ├── Types / Identity     window_id, display_id
      ├── Lifecycle            uninitialized → created → closing → closed
      ├── Config               requested create-time properties
      ├── State                actual + requested runtime fields
      ├── Display              placeholder primary display
      ├── Events               typed queue (push / poll / peek)
      ├── Event loop           poll / wait / wakeup (Goal 7.9)
      ├── Native handle        opaque uintptr_t token
      ├── Backend vtable       create/show/hide/present/process_events/wait/wakeup
      ├── Present              validate + sync present (Goal 7.4; mock / host stub)
      ├── Host backend         Win32 native create + message pump (Goals 7.5 / 7.9)
      └── Sizing               requested/actual + min/max (Goal 7.6)
```

## Dependency direction

```text
RoboBase
   ↓
RoboMem / RoboError / RoboDebug
   ↓
RoboGraphics (surfaces / geometry)
   ↓
RoboDisplay
   ↓
RoboWindow
   └── Backend interfaces → null / mock / host (win32) / future QEMU
```

RoboWindow may include RoboGraphics headers. RoboGraphics must not depend on
RoboWindow.

## Window identity

`WindowId` is a process-local monotonic integer. It is never a native
OS handle. After `destroy`, the id is invalid (`RoboNotFound`).

## Lifecycle

| State | Meaning |
|---|---|
| `UNINITIALIZED` | Slot not live |
| `CREATED` | Window object exists |
| `CLOSING` | Close requested; awaiting destroy |
| `CLOSED` | Destroyed / gone |

Visibility, focus, minimized, maximized, and fullscreen are **state flags**,
not lifecycle phases.

Valid transitions: `UNINITIALIZED→CREATED`, `CREATED→CLOSING|CLOSED`,
`CLOSING→CLOSED`, `CLOSED→CLOSED` (idempotent check helper only).
Close requests are irreversible at this layer (no cancel). See
[Lifecycle.md](Lifecycle.md).

## Configuration vs state

* **Config** — what the caller requests at create (size, title, flags).
* **State** — what the runtime currently reports (actual size, visibility,
  unknown position via `RoboWindowCoordUnknown`).

## Display model

Display ownership lives in **RoboDisplay** (Goal 7.3). After
`WindowInit`, the architecture **placeholder** backend is registered
(one 1280×800 primary, scale 1.0). No host enumeration.

Windows store `display_id` in config/state and may use
`WindowGetAssociatedDisplay` /
`WindowSetAssociatedDisplay`. Native window→monitor mapping is later.

## Events

Backends (and core) push canonical events with `WindowEventPush`.
Consumers poll with `WindowEventPoll`. This is **not** Goal 7.9’s
native event loop. Keyboard/mouse are out of scope.

## Backend contract

Register via `WindowBackendRegister` or `WindowBackendUseNull`.
Without a backend, `WindowCreate` returns `RoboNotSupported`.

The **null** backend:

* Completes in-process lifecycle only
* Leaves native handle empty
* Returns `RoboNotSupported` from `present_surface`
* Does not create OS windows

## Native handles

`WindowNativeHandle.opaque` is an uninterpreted token owned by the
backend. Core never includes platform headers.

## RoboGraphics relationship

```text
RoboWindow  →  presents  →  RoboGraphics Surface
```

`WindowSetPresentSurface` borrows a surface. `WindowPresent` /
`WindowPresentFramebuffer` validate format/stride/dims and call the
backend synchronously (Goal 7.4). Null backend → `RoboNotSupported`. Pixel
formats remain RoboGraphics-owned. See `Present.md`.

## Threading (Goal 7.1)

* Single owner thread for window operations (callers synchronize).
* Event push/poll are not thread-safe.
* Presentation, when implemented, is synchronous at the API boundary
  unless a future backend documents otherwise.

## Ownership

| Object | Owner |
|---|---|
| Window slot / id | RoboWindow registry |
| Title buffer | Window slot (copied) |
| Native handle payload | Active backend |
| Present surface pointer | Borrowed; caller retains surface |
| Event queue | RoboWindow process state |
| Display placeholders | RoboWindow process state |

## Chromium independence

No Chromium / Skia / toolkit includes. Ozone `PlatformWindow` is reference
only. Build.ps1 `-Verify` runs `Assert-RoboWindowChromiumIndependence`.

## Current limitations

* No native OS window
* No real host / QEMU display output (mock present only)
* No native event loop
* No keyboard / mouse
* No host DPI / display enumeration
* API not frozen (`RoboWindowApiFrozen=0`)
* Owner-thread model uses process-local tokens (no OS TLS yet)

## Next

Goal **7.5** — Window creation refinements (when requested).
