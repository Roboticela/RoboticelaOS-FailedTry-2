# RoboticelaOS

Independent operating system and Chromium-class runtime (HTML, CSS, layout, paint, raster, desktop) with its own kernel and UEFI bootloader. Chromium is reference material only; it is not a dependency.

Contributor rules: **[AGENTS.md](AGENTS.md)**. Architecture: [Documentation/Architecture/RepositoryArchitecture.md](Documentation/Architecture/RepositoryArchitecture.md).

## Build

Build always runs on the development machine. Each part has its own script; `BuildEverything.ps1` calls them in order.

```powershell
.\Scripts\Build\BuildHtml.ps1          # one subsystem
.\Scripts\Build\BuildDesktop.ps1       # Vite desktop UI + shell
.\Scripts\Build\BuildRuntime.ps1       # full runtime guest ELF
.\Scripts\Build\BuildEverything.ps1    # every part, then the disk image
```

The pipeline entry point is still `.\scripts\pipeline\robo.ps1` (same folder as `Scripts/` on Windows). `.\build.ps1` is a compatibility shim.

Output lands in `Build/`. Do not commit it.

## Test

Tests take `-Target Host` (default) or `-Target Qemu`. `TestEverything.ps1` calls each part's test script.

```powershell
.\Scripts\Test\TestHtml.ps1 -Target Host
.\Scripts\Test\TestDesktop.ps1 -Target Qemu
.\Scripts\Test\TestEverything.ps1 -Target Host
```

## QEMU

```powershell
.\Scripts\Qemu\RunQemu.ps1
```

## Where new code goes

| Feature | Folder |
|---|---|
| HTML / CSS / layout / paint | `Runtime/Source/RoboHTML` (etc.) |
| Desktop shell | `Runtime/Source/RoboDesktop` |
| Desktop UI (Vite + React) | `Desktop/` |
| Kernel | `Kernel/` |
| Boot | `Bootloader/` |
| Tests | `Tests/<Subsystem>/` |
| Docs | `Documentation/` |

Details: `AGENTS.md`.
