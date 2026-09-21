# Per-part build and test scripts

Every subsystem has its own build script and its own test script. `BuildEverything.ps1` and `TestEverything.ps1` only call those scripts; they do not contain a second compile or test engine.

## Build

Build always runs on the development machine. There is no host/QEMU split for compile.

| Script | Compiles |
|---|---|
| `Scripts/Build/BuildCore.ps1` | RoboBase |
| `BuildMemory.ps1` | RoboMem |
| `BuildContainers.ps1` | RoboContainers |
| `BuildUnicode.ps1` | RoboUnicode + RoboI18N |
| `BuildFont.ps1` | RoboFont |
| `BuildGraphics.ps1` | RoboGraphics |
| `BuildDisplay.ps1` | RoboDisplay |
| `BuildWindow.ps1` | RoboWindow |
| `BuildInput.ps1` | RoboInput |
| `BuildHtml.ps1` | RoboHTML |
| `BuildCss.ps1` | RoboCSS |
| `BuildLayout.ps1` | RoboLayout |
| `BuildPaint.ps1` | RoboPaint |
| `BuildRasterizer.ps1` | RoboRender |
| `BuildDesktopUi.ps1` | Vite + React → HTML/CSS/JS + `DesktopPage.h` |
| `BuildDesktop.ps1` | desktop UI, then RoboDesktop |
| `BuildRuntime.ps1` | full runtime guest ELF |
| `BuildLibc.ps1` | libc + userland |
| `BuildKernel.ps1` | kernel.elf |
| `BuildBootloader.ps1` | UEFI bootloader |
| `BuildEverything.ps1` | every script above, then the disk image |

Objects are incremental. Changing HTML does not rebuild the kernel.

## Test

```powershell
.\Scripts\Test\TestHtml.ps1 -Target Host
.\Scripts\Test\TestDesktop.ps1 -Target Qemu
.\Scripts\Test\TestEverything.ps1 -Target Host
```

`-Target Host` runs `Build/Runtime/RoboTest.host.exe` (or the pipeline's host kernel/bootloader suites). `-Target Qemu` boots the real OS when that part has a guest suite; parts with no QEMU suite exit 0 and say so.
