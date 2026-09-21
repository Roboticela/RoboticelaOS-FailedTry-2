# Testing architecture

Three surfaces:

1. **Host unit tests** — `Build/Runtime/RoboTest.host.exe` and kernel/bootloader/libc `test_*.exe` on the development OS.
2. **QEMU acceptance** — the real machine under UEFI, asserted against serial markers.
3. **Integration** — `test-e2e` / `TestIntegrationQemu.ps1` when a public contract changed.

## Commands

| Want | Script |
|---|---|
| HTML on host | `Scripts/Test/TestHtmlHost.ps1` |
| CSS on host | `TestCssHost.ps1` |
| Layout / paint / rasterizer / desktop on host | `TestLayoutHost.ps1`, `TestPaintHost.ps1`, `TestRasterizerHost.ps1`, `TestDesktopHost.ps1` |
| Kernel host tests | `TestKernel.ps1` |
| Kernel in QEMU | `TestKernelQemu.ps1` |
| Desktop in QEMU | `TestDesktopQemu.ps1` |
| Everything on host | `TestEverything.ps1 -SkipQemu` |
| Everything including QEMU | `TestEverything.ps1` |

`TestHtml.ps1` is the same as `TestHtmlHost.ps1`. The Host suffix makes the execution target explicit.

## When QEMU is required

- Kernel, bootloader, or image packing changed.
- Guest-visible desktop or display behaviour changed.
- A public runtime interface changed (then also run `TestIntegrationQemu.ps1`).

Unit tests for HTML/CSS/layout/paint stay on the host. Do not send them through QEMU.

## Pipeline

`Scripts/pipeline/robo.ps1 test-all` remains the full orchestrator. `TestEverything.ps1` calls it. Per-component `test-html` commands still exist on the pipeline for the same filters.
