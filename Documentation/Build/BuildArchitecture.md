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

Every compile path is resume-capable: a second run compiles only what is stale.

| Layer | Stale rule | Progress lines |
|---|---|---|
| Runtime (`Runtime/Tools/Build.ps1`) | Object missing, older than its `.c`, or older than any header listed in the clang `-MMD` `.d` next to the `.o` | `PLAN` / `CC [n/N]` / `DONE` |
| Boot / kernel / libc / userland (`roboboot-build-common.ps1`) | Same rule via `Invoke-CcBatch` / `Invoke-CcDirBatch` | `PLAN` / `CC [n/N]` / `DONE` |
| Full stack (`BuildEverything.ps1`) | Completed parts recorded in `Build/.build-everything.json` | `PLAN` / `RESUME` / `DONE` |
| Pipeline (`scripts/robo build-all`, `RunQemu.ps1`) | Stages always re-enter; objects resume by `.d`. Progress journal in `Build/.build-all.json` | `PLAN` / `==== [n/N]` / `CC [n/N]` / `DONE` |

`PLAN` reports three counts for the unit about to run: **total**, **up-to-date**, and **to compile** (ninja-style `[finished/total]` on each `CC`). `DONE` reports how many were actually compiled vs reused.

`run-qemu` is incremental by default. Pass `--rebuild` (or `RunQemu.ps1 -Rebuild`) to force every object. A successful pipeline run clears `Build/.build-all.json`; a failure keeps the journal so the next run can say how far the last attempt got.

Shared helpers live in `Scripts/Utilities/RoboBuildProgress.ps1`.

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

`BuildEverything.ps1` calls `BuildBootloader`, `BuildKernel`, `BuildRuntime`, then `scripts/pipeline/robo.ps1 build-image`, resuming from `Build/.build-everything.json` when present.

## Host vs QEMU

Build always happens on the development machine. QEMU is not a build mode. There is no Host Runtime binary distinct from the runtime.
