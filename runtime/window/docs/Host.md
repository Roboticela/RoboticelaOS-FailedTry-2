# Host Native Window & Display Backend (Goals 7.5 / 7.13)

## Selected backend

| Host | Window backend | Display backend | Status |
|---|---|---|---|
| Windows (development host) | `win32` | `win32` | Implemented (7.13 present) |
| Guest freestanding ELF | stub | stub | `ROBO_NOT_SUPPORTED` |
| Other hosts | stub | stub | Extensible via new `platform/*` |

No SDL, GLFW, Qt, GTK, Chromium, Electron, or other third-party window frameworks.

## Architecture (Goal 7.13)

```text
Roboticela runtime
        ↓
RoboWindow (platform-independent)
        ↓
Win32 window backend (platform/win32)
        ↓
RoboGraphics surface (BGRA8 / RGBA8 / RGB8)
        ↓
staging CreateDIBSection (top-down 32bpp BGRX)
        ↓
InvalidateRect → WM_PAINT → BitBlt / StretchBlt
        ↓
real HWND client area
```

Display discovery:

```text
robo_display_backend_use_host()
        ↓
EnumDisplayMonitors / GetMonitorInfoW
        ↓
optional GetDpiForMonitor (Shcore) or LOGPIXELS
        ↓
RoboDisplay cache (bounds, work area, scale, DPI)
```

`robo_window_backend_use_host()` switches an already-initialized RoboDisplay
manager from the placeholder backend to the host monitor backend when available.

## Native APIs (Win32)

Isolated under:

* `Source/RoboWindow/platform/win32/RoboWindowBackendWin32.c`
* `Source/RoboWindow/platform/win32/RoboWindowKeyboardWin32.c`
* `Source/RoboWindow/platform/win32/RoboWindowMouseWin32.c`
* `Source/RoboDisplay/platform/win32/RoboDisplayBackendWin32.c`

Window / present:

* `RegisterClassExW` / `CreateWindowExW` / `DestroyWindow`
* `ShowWindow` / `UpdateWindow` / `InvalidateRect`
* `CreateDIBSection` / `BitBlt` / `StretchBlt` (CPU presentation)
* `PeekMessageW` / `DispatchMessageW` / `MsgWaitForMultipleObjects`
* `MonitorFromWindow` → host display id mapping

Display:

* `EnumDisplayMonitors` / `GetMonitorInfoW`
* `GetDpiForMonitor` (dynamic load from `Shcore.dll`) or `GetDeviceCaps(LOGPIXELS*)`
* Refresh rate is **not** reported (left 0; not fabricated)

Public headers never include `windows.h`.

## Pixel format

| RoboGraphics | Staging / GDI | Notes |
|---|---|---|
| BGRA8 | BI_RGB 32bpp top-down DIB (B,G,R,X) | Preferred |
| RGBA8 | Converted at present boundary | Host-only conversion |
| RGB8 | Converted at present boundary | Host-only conversion |

Conversion stays inside the Win32 present path — RoboGraphics APIs are unchanged.

## DPI / scale

Uses Phase 7.8 helpers. Effective scale = `dpi / 96` when DPI is known.
Logical window size is **client area** DIP; physical size is derived via
`robo_window_get_size_physical`. `WM_DPICHANGED` invalidates the client for
repaint; runtime DPI change reporting depends on the OS (Per-Monitor V2 when
available). Unsupported scales are not fabricated.

## Resize / close / input

* `WM_SIZE` → `robo_window_observe_size` + invalidate
* `WM_CLOSE` → `robo_window_observe_close_request` (no DestroyWindow in WndProc)
* Keyboard / mouse continue through existing Win32 → RoboKeyboard / RoboMouse
  observe path (Goals 7.10 / 7.11)
* Presentation ownership: staging DIB is owned by the backend; caller surfaces
  are not retained after `robo_window_present`

## Registration

```text
robo_window_init()          // placeholder display
robo_window_backend_use_host()
        ↓ also: robo_display_backend_use_host() when display is live
robo_window_create(config)
robo_window_show(...)
robo_window_present(id, surface)
```

* `robo_window_backend_host_available()` / `robo_display_backend_host_available()`
  return 0 when `ROBO_WINDOW_SKIP_HOST` is set or screen metrics are unavailable.
* Guest builds compile stubs only (`RoboWindowBackendHost.c`,
  `RoboDisplayBackendHost.c`).

## Headless / CI / smoke

```powershell
# Automated host window + present + display discovery
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test -Filter "window.host."
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter "display.host."

# Or the pipeline's display command (build + host filters + optional visible window)
scripts\robo test-display
scripts\robo test-display --visible        # plus ROBO_WINDOW_SMOKE=1

# Skip real HWND on CI machines without a desktop
$env:ROBO_WINDOW_SKIP_HOST="1"
```

## Limitations

* Windows host only for real presentation today
* No GPU / DXGI / Vulkan swap chain in 7.13 (CPU GDI path)
* Path fill/stroke still `ROBO_NOT_SUPPORTED` in RoboGraphics raster — smoke
  patterns use clear / rect / line
* Refresh Hz not exposed
* Physical monitor size (mm) not exposed
* QEMU backend (7.12) is separate and unchanged in contract

## Platform vs independent

| Independent | Platform-specific |
|---|---|
| RoboWindow / RoboDisplay / RoboGraphics APIs | `platform/win32/*` |
| Backend vtable contracts | HWND, HMONITOR, GDI |
| Mock / null / QEMU backends | Host availability heuristics |
