# RoboDesktop

Native shell for RoboticelaOS. Architecture, frames, windows, geometry, documents and tests: **[Documentation/Runtime/Desktop/](../../Documentation/Runtime/Desktop/Architecture.md)**.

The window document is authored in `Desktop/` (Vite + React) and compiled by `Scripts/Build/BuildDesktopUi.ps1`.

```powershell
.\Scripts\Build\BuildDesktop.ps1
.\Scripts\Test\TestDesktop.ps1 -Target Host
```
