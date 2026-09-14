# RoboWindow

Platform-independent windowing for Roboticela Runtime (**Goals 7.1–7.5**).

**Version:** 0.5.0 — native host window creation (`ROBO_WINDOW_API_FROZEN=0`)

Public RoboWindow must never depend on Chromium, Skia, SDL, GLFW, or Qt.
OS headers (`windows.h`, etc.) are confined to `Source/RoboWindow/platform/*`.
Chromium Ozone / `PlatformWindow` may be consulted as a temporary technical
reference only.

## Layout

| Path | Role |
|---|---|
| `runtime/Include/RoboWindow/` | Public API (no OS headers) |
| `runtime/Source/RoboWindow/` | Platform-independent implementation |
| `runtime/Source/RoboWindow/platform/win32/` | Host Win32 backend (host tests only) |
| `runtime/window/tests/` | Architecture / native / present / host tests |
| `runtime/window/docs/` | Docs |

## Dependency

```text
RoboBase → RoboMem / RoboError / RoboDebug
        → RoboGraphics
        → RoboDisplay
        → RoboWindow
             → Native Window Interface
             → null | mock | host (win32) backend
```

## Goal 7.5 scope

Real native OS window creation on the Windows development host via
`robo_window_backend_use_host()`. Guest ELF keeps a stub. Mock remains for
headless CI. See [Host.md](docs/Host.md).

## Tests

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test -Filter "window."
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter "window.host."
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter "window.present."
```

See `docs/Host.md`, `docs/Present.md`, and `docs/`.
