# Native Window Abstraction (Goal 7.2)

## Layering

```text
RoboWindow (canonical id + lifecycle)
        ↓
Native Window Interface (backend vtable)
        ↓
Opaque native handle (token + generation)
        ↓
Future OS / QEMU backend
```

Goal 7.2 defines the abstract native interface. Goal 7.5 adds the first real
host backend (`win32`) behind `RoboWindowBackendHost.h` — still without exposing
HWND or `windows.h` through the public API. See [Host.md](Host.md).

## Opaque handle

`WindowNativeHandle` is a typed `{ token, generation }` pair.

* Valid when both fields are non-zero
* Backend-owned resource until `destroy_window` / `invalidate`
* Comparable via `WindowNativeHandleEqual`
* Stale copies after destroy must not be reused against the core

## Backend contract

Required vtable fields include `capabilities`, `set_title` (UTF-8 for call
duration only), `focus`, and presentation/process_events entry points.

Capabilities are explicit flags — see [Capabilities.md](Capabilities.md).

## Title encoding

The backend boundary receives **UTF-8**. Invalid sequences yield
`RoboParseError` before the backend is called.

## State synchronization

When `reports_actual_state` is set, geometry/visibility after requests come
from `query_state`. Otherwise core applies provisional actual = requested.

## Presentation

`WindowPresent` validates the surface (format, stride, dims) and returns
`RoboNotSupported` when the backend cannot present (null / non-present mock).
With a present-capable mock, presentation is synchronous and verified in
`window.present.*` tests — still no OS window. See `Present.md`.
