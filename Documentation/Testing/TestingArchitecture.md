# Testing architecture

Three surfaces:

1. **Host unit tests** — `Build/Runtime/RoboTest.host.exe` and kernel/bootloader/libc `test_*.exe` on the development OS.
2. **QEMU acceptance** — the real machine under UEFI, asserted against serial markers.
3. **Integration** — `test-e2e` / `TestIntegrationQemu.ps1` when a public contract changed.

## Commands

| Want | Script |
|---|---|
| HTML | `Scripts/Test/TestHtml.ps1 -Target Host` |
| CSS | `TestCss.ps1 -Target Host` |
| Layout / paint / rasterizer / desktop | `TestLayout.ps1`, `TestPaint.ps1`, `TestRasterizer.ps1`, `TestDesktop.ps1` with `-Target Host` |
| Kernel | `TestKernel.ps1 -Target Host` or `-Target Qemu` |
| Desktop in QEMU | `TestDesktop.ps1 -Target Qemu` |
| Everything | `TestEverything.ps1 -Target Host` or `-Target Qemu` |

Build is always on the development machine (`Scripts/Build/Build*.ps1`). Tests choose Host or QEMU with `-Target`. See [Documentation/Build/PartScripts.md](../Build/PartScripts.md).

## When QEMU is required

- Kernel, bootloader, or image packing changed.
- Guest-visible desktop or display behaviour changed.
- A public runtime interface changed (then also run `TestIntegrationQemu.ps1`).

Unit tests for HTML/CSS/layout/paint stay on the host. Do not send them through QEMU.

## Pipeline

`Scripts/pipeline/robo.ps1 test-all` remains the full orchestrator. `TestEverything.ps1` calls it. Per-component `test-html` commands still exist on the pipeline for the same filters.
