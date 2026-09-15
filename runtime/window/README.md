# RoboWindow

Platform-independent windowing for Roboticela Runtime (**Goals 7.1–7.14**).

**Version:** 0.14.0 — Window + RoboGraphics integration (`ROBO_WINDOW_API_FROZEN=0`)

Public RoboWindow must never depend on Chromium, Skia, SDL, GLFW, or Qt.
OS headers (`windows.h`, etc.) are confined to `Source/RoboWindow/platform/*`.

## Layout

| Path | Role |
|---|---|
| `runtime/Include/RoboWindow/` | Public API (no OS headers) |
| `runtime/Source/RoboWindow/` | Platform-independent implementation |
| `runtime/Source/RoboWindow/platform/win32/` | Host Win32 backend |
| `runtime/Source/RoboWindow/platform/qemu/` | QEMU / SYS_DISPLAY backend |
| `runtime/window/tests/` | Suites including `window.graphics.*` |
| `runtime/window/docs/` | Docs |

## Goal 7.14 scope

Owned render surface + frame lifecycle (`begin_frame` / `end_frame` /
`present_frame` / invalidate). See [docs/Graphics.md](docs/Graphics.md).

## Tests

```powershell
scripts\robo test-window          # window.* host suites
scripts\robo test-display         # display.* + host display suites
scripts\robo test-qemu-display    # real QEMU display smoke
```
