# Host Native Window Backend (Goal 7.5)

## Selected backend

| Host | Backend name | Status |
|---|---|---|
| Windows (development host) | `win32` | Implemented |
| Guest freestanding ELF | stub | `ROBO_NOT_SUPPORTED` |
| Other hosts | stub | Extensible via new `platform/*` |

No SDL, GLFW, Qt, GTK, Chromium, or other third-party window frameworks.

## Native APIs (Win32)

Isolated under `Source/RoboWindow/platform/win32/RoboWindowBackendWin32.c`:

* `RegisterClassExW` / `CreateWindowExW` / `DestroyWindow`
* `ShowWindow` / `UpdateWindow`
* `SetWindowTextW` (UTF-16 via `robo_unicode_utf8_to_utf16`)
* `SetWindowPos` (size / position)
* `SetForegroundWindow` / `SetFocus` (focus; may return unsupported)
* `GetWindowRect` / `GetClientRect` / `IsWindowVisible` (query_state)
* `PeekMessageW` / `DispatchMessageW` (minimal `process_events`)
* `MonitorFromWindow` reserved for later display mapping

Public headers never include `windows.h`.

## Registration

```text
robo_window_init()
        ↓
robo_window_backend_use_host()   // or use_null / mock register
        ↓
robo_window_create(config)
        ↓
Win32 CreateWindowExW
        ↓
opaque { token, generation }
```

* `robo_window_backend_host_available()` — 0 when `ROBO_WINDOW_SKIP_HOST` is set
  or when no interactive screen metrics exist.
* Guest builds compile `RoboWindowBackendHost.c` stub only.

## Handle ownership

* Backend owns `HWND` for the lifetime of a valid native handle.
* `token` is a 1-based slot index; `generation` increments per create.
* Destroy / shutdown invalidate the handle; copies must not be reused.
* Failed create does not leave a live slot.

## State synchronization

Win32 sets `reports_actual_state`. After size/move/show/hide, core calls
`query_state` for client size, outer position, visibility, and focus.

Logical size is **client area** (chrome adjusted via `AdjustWindowRectEx`).
Physical display DPI scaling is **not** applied in Goal 7.5.

## Thread affinity

Native create and subsequent HWND ops must run on the thread that initialized
the backend (`GetCurrentThreadId` check → `ROBO_PERMISSION_DENIED`).

Also respects RoboWindow owner-thread tokens when bound.

## Title / size / position

| Field | Behavior |
|---|---|
| Title | UTF-8 → UTF-16; empty OK; invalid UTF-8 → `ROBO_PARSE_ERROR` |
| Size | Client dimensions; min/max from config; invalid → `ROBO_INVALID_ARGUMENT` |
| Position | Screen coordinates; negative allowed; `ROBO_WINDOW_COORD_UNKNOWN` → OS default at create |
| Fullscreen | Deferred — logged, not applied |
| Scale preference | Ignored in 7.5 (documented) |

## Display association

Window keeps `display_id` from config or RoboDisplay primary (placeholder
backend). Host monitor enumeration mapping is deferred.

## Presentation

`present_surface` returns `ROBO_NOT_SUPPORTED` on Win32 in 7.5. Goal 7.4 mock
presentation remains for tests. Real blit is later display-backend work.

## Event boundary

`GWLP_USERDATA` points at the backend slot. `WM_CLOSE` pushes
`ROBO_WINDOW_EVENT_CLOSE_REQUESTED`. Full keyboard/mouse routing is Goal 7.9.
`robo_window_process_events()` drains the Win32 message queue briefly.

## Headless / CI

* Automated tests skip real windows when host is unavailable or
  `ROBO_WINDOW_SKIP_HOST` is set (prints `SKIP …`).
* Mock/null backends remain the default headless path.
* Manual smoke: `ROBO_WINDOW_SMOKE=1` with filter `window.host.smoke`.

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test -Filter "window.host."
$env:ROBO_WINDOW_SMOKE="1"
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter "window.host.smoke"
```

## Platform vs independent

| Independent | Platform-specific |
|---|---|
| RoboWindow API, config, lifecycle, opaque handle | `platform/win32/*` |
| Backend vtable contract | Win32 HWND ops |
| Mock / null backends | Host availability heuristics |
| RoboDisplay association fields | MonitorFromWindow (future map) |
