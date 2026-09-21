# RoboticelaOS

Independent operating system and Chromium-class runtime (HTML, CSS, layout, paint, raster, desktop) with its own kernel and UEFI bootloader. Chromium is reference material only; it is not a dependency.

Contributor rules: **[AGENTS.md](AGENTS.md)**. Architecture: [Documentation/Architecture/RepositoryArchitecture.md](Documentation/Architecture/RepositoryArchitecture.md).

## Build

From the repository root (PowerShell):

```powershell
.\Scripts\Build\BuildHtml.ps1          # one subsystem, no kernel
.\Scripts\Build\BuildRuntime.ps1       # full runtime guest ELF
.\Scripts\Build\BuildEverything.ps1    # bootloader + kernel + runtime + image
```

The pipeline entry point is still `.\scripts\pipeline\robo.ps1` (same folder as `Scripts/` on Windows). `.\build.ps1` is a compatibility shim.

Output lands in `Build/`. Do not commit it.

## Test

```powershell
.\Scripts\Test\TestHtmlHost.ps1        # host
.\Scripts\Test\TestEverything.ps1 -SkipQemu
.\Scripts\Test\TestQemu.ps1            # real UEFI machine
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
| Kernel | `Kernel/` |
| Boot | `Bootloader/` |
| Tests | `Tests/<Subsystem>/` |
| Docs | `Documentation/` |

Details: `AGENTS.md`.
