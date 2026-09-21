# RoboDisplay

Platform-independent display abstraction for RoboticelaOS Runtime.

**Version:** 0.4.0 (`RoboDisplayApiFrozen=0`)  
**Goals:** 7.3 (abstraction) + 7.8 (DPI / scale) + 7.12 (QEMU) + 7.13 (host)

## What this is

RoboDisplay describes physical/logical displays (identity, geometry, scale/DPI,
refresh, orientation, primary status) and defines the backend boundary.

| Backend | API | Notes |
|---|---|---|
| Placeholder / null | always | Headless tests |
| QEMU | `DisplayBackendUseQemu` | Guest GOP / host sim |
| Host | `DisplayBackendUseHost` | Win32 `EnumDisplayMonitors` |

Scale/DPI helpers live in `RoboDisplayScale.h` — see [docs/Scale.md](docs/Scale.md).
Host window presentation docs: `window/docs/Host.md`.

## Layout

| Path | Role |
|------|------|
| `Include/RoboDisplay/` | Public API |
| `Source/RoboDisplay/` | Manager + null/placeholder/QEMU/host stubs |
| `Source/RoboDisplay/platform/win32/` | Host monitor enum (host tests only) |
| `display/docs/` | Architecture + Scale notes |
| `display/tests/` | Host tests (`display.*`) + mock backend |

## Tests

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter "display."
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter "host."
```
