# RoboWindow

Platform-independent windowing for Roboticela Runtime (**Goals 7.1–7.4**).

**Version:** 0.4.0 — framebuffer presentation (`ROBO_WINDOW_API_FROZEN=0`)

RoboWindow must never depend on Chromium, Skia, Win32, X11, Wayland, Cocoa,
SDL, GLFW, or Qt. Chromium Ozone / `PlatformWindow` may be consulted as a
temporary technical reference only.

## Layout

| Path | Role |
|---|---|
| `runtime/Include/RoboWindow/` | Public API |
| `runtime/Source/RoboWindow/` | Implementation |
| `runtime/window/tests/` | Architecture + native + present tests |
| `runtime/window/docs/` | Docs |

## Dependency

```text
RoboBase → RoboMem / RoboError / RoboDebug
        → RoboGraphics
        → RoboDisplay
        → RoboWindow
             → Native Window Interface
             → presentation backend (mock / future host / QEMU)
```

## Goal 7.4 scope

Synchronous surface/framebuffer presentation through the backend vtable, with
validation and a test mock. Still **no** real OS windows or QEMU display.

## Tests

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test -Filter "window."
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter "window.present."
```

See `docs/Present.md` and `docs/`.
