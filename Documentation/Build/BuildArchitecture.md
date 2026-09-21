# Build architecture

There is one compile engine per layer:

- Bootloader / kernel / libc / image: `Scripts/pipeline/robo.ps1` → `Scripts/roboboot-build-common.ps1`
- Runtime modules: `Runtime/Tools/Build.ps1`

`Scripts/Build/*.ps1` are the public entry points. They do not reimplement compilation.

## Scripts

| Script | What it compiles | Does not compile |
|---|---|---|
| `BuildHtml.ps1` | Html + declared deps, `libRoboHtml.a` | Kernel, bootloader, unrelated suites |
| `BuildCss.ps1` | Css + deps | Kernel, bootloader |
| `BuildLayout.ps1` | Layout + Html + Css + deps | Kernel |
| `BuildPaint.ps1` | Paint + layout chain | Kernel |
| `BuildRasterizer.ps1` | RoboRender + paint chain | Kernel |
| `BuildDesktop.ps1` | Desktop + render chain | Kernel |
| `BuildFont.ps1` / `BuildGraphics.ps1` / `BuildWindow.ps1` / `BuildInput.ps1` / `BuildUnicode.ps1` | named module + deps | Kernel |
| `BuildRuntime.ps1` | Full runtime guest ELF | Kernel, bootloader |
| `BuildBootloader.ps1` | BOOTX64.EFI + host preview | Runtime, kernel |
| `BuildKernel.ps1` | kernel.elf | Runtime HTML/CSS |
| `BuildEverything.ps1` | Bootloader → Kernel → Runtime → image | — |

Module builds pass `-Module <Name> -SkipGuest` so they compile objects and archive them **without** linking `RoboticelaRuntime.elf` and **without** requiring `libc.a`. That is how an HTML change avoids rebuilding the kernel.

## Incremental compilation

`Runtime/Tools/Build.ps1` compiles each `.c` to `Build/Runtime/obj/{guest,host}/` and skips the file when the object is newer than the source and the header tree stamp. Relink happens only when an object is newer than the binary.

`-Rebuild` forces a full recompile of that invocation.

## Full build order

```text
bootloader assets
 → libc / userland (libc.a, crt)
 → runtime guest ELF
 → bootloader EFI
 → kernel.elf
 → ESP / disk image
 → artifacts copy
```

`BuildEverything.ps1` calls `BuildBootloader`, `BuildKernel`, `BuildRuntime`, then `scripts/pipeline/robo.ps1 build-image`.

## Host vs QEMU

Build always happens on the development machine. QEMU is not a build mode. There is no Host Runtime binary distinct from the runtime.
