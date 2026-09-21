# RoboticelaOS — Agent and contributor rules

This is the primary development instruction file. Architecture detail lives in `Documentation/`. Color tokens live in `Documentation/Development/ColorPalette.md`.

There is **one** RoboticelaOS Runtime. Host vs QEMU is a **test execution target**, not a second runtime.

---

## Repository structure

| Folder | Owns |
|---|---|
| `Bootloader/` | UEFI bootloader (`Source/`, `Include/`, `Assets/`, `Config/`) |
| `Kernel/` | Kernel. Internal include paths (`mm/`, `fs/`, …) are the kernel's C include namespace. |
| `Runtime/` | Independent web/UI runtime. Public headers: `Runtime/Include/`. Implementation: `Runtime/Source/Robo*/`. |
| `Libc/` | Freestanding C library for the guest. |
| `User/` | Guest userland programs and the linker script. |
| `Tests/` | All tests, grouped by subsystem. |
| `Documentation/` | All project documentation. |
| `Scripts/Build/` | Per-subsystem build entry points. |
| `Scripts/Test/` | Per-subsystem test entry points. |
| `Scripts/Qemu/` | QEMU run/test entry points. |
| `Scripts/pipeline/` | The existing compile/test/run engine (`robo.ps1`). |
| `Build/` | **Generated only.** Never edit. Host, QEMU, intermediate objects, artifacts. |
| `Tools/` | Shared repo tools. |
| `Sdk/` | Guest SDK headers. |

Empty stub trees (`apps/`, `graphics/`, `Drivers/`, `Network/`, `Userspace/`, `Filesystem/`, `Configs/`, `Libsystem/`) were removed. Do not recreate placeholder folders. Kernel drivers live in `Kernel/`; guest programs live in `User/`.

On Windows the filesystem is case-insensitive. Git's canonical names are PascalCase (`Runtime/`, `Scripts/`). `scripts/` and `Scripts/` are the same directory.

---

## Naming rules

| Kind | Convention | Example |
|---|---|---|
| Variables, fields, parameters | camelCase | `windowWidth`, `displayList` |
| Functions | PascalCase | `HtmlParse()`, `BuildDisplayList()` |
| Structs / classes / types / enums | PascalCase | `InputEvent`, `HtmlDocument`, `LayoutContext` |
| Enumerators and project macros | PascalCase with a `Robo` prefix when the name would otherwise collide | `RoboOk`, `RoboInputEventKeyDown` |
| Files | PascalCase | `DisplayList.c`, `HtmlParser.h` |
| Folders | PascalCase | `Runtime/Html/`, `Tests/Layout/` |

The public C API follows the same rules (`HtmlParse`, `LayoutContext`, `RoboOk`). Include guards stay `ROBO_*_H`.

Do not rename POSIX libc names, EFI/Win32/compiler symbols, syscall numbers (`SYS_*`), or third-party / generated identifiers. If a mapped PascalCase name collides with a platform API (`CreateWindow`, `DebugBreak`), prefix `Robo`.

Kernel `#include "mm/page.h"`-style paths stay until a dedicated include migration.

---

## Subsystem ownership

| Work | Location |
|---|---|
| HTML | `Runtime/Source/RoboHTML/` + `Runtime/Include/RoboHTML/` |
| CSS | `Runtime/Source/RoboCSS/` + `Runtime/Include/RoboCSS/` |
| Layout | `Runtime/Source/RoboLayout/` |
| Paint | `Runtime/Source/RoboPaint/` |
| Rasterizer (RoboRender) | `Runtime/Source/RoboRender/` |
| Graphics | `Runtime/Source/RoboGraphics/` |
| Window | `Runtime/Source/RoboWindow/` |
| Input | `Runtime/Source/RoboInput/` |
| Desktop shell | `Runtime/Source/RoboDesktop/` |
| Font | `Runtime/Source/RoboFont/` |
| Unicode / I18N | `Runtime/Source/RoboUnicode/`, `Runtime/Source/RoboI18N/` |
| Kernel | `Kernel/` |
| Boot | `Bootloader/` |
| Tests | `Tests/<Subsystem>/` |
| Documentation | `Documentation/` |
| Build script | `Scripts/Build/` |
| Test script | `Scripts/Test/` |
| QEMU tooling | `Scripts/Qemu/` |

Public interface = `Runtime/Include/<Module>/`. Other subsystems include those headers, never `Runtime/Source/.../Internal`.

---

## Dependency rules

Direction (runtime), downstream may depend on upstream, never the reverse:

```text
RoboBase → RoboMem → RoboContainers → RoboUnicode / RoboI18N
       ↘ RoboFont → RoboGraphics → RoboDisplay → RoboWindow
                                    ↘ RoboInput
RoboHTML  ┐
RoboCSS   ┴→ RoboLayout → RoboPaint → RoboRender → RoboDesktop
```

- **Circular dependencies are prohibited.**
- RoboDesktop must not parse HTML/CSS/layout except through the named document-bridge files.
- RoboRender must not include document-layer headers in module sources.
- RoboInput may not include RoboPaint, RoboRender, or RoboDesktop.
- Do not add Chromium, Skia, V8, or Blink as a build or link dependency. `Chromium/` is reference material only.
- Kernel must not include Runtime headers. Runtime must not include Kernel headers.
- Bootloader must not include Runtime or Kernel implementation files.

Stable APIs: versioned headers under `Runtime/Include/`. New APIs belong in the owning module's public header, not a grab-bag.

---

## Test rules

Host tests run `Build/Runtime/RoboTest.host.exe` on the development machine. QEMU tests boot the real OS.

| Change | Build | Host test | QEMU |
|---|---|---|---|
| HTML | `Scripts/Build/BuildHtml.ps1` | `Scripts/Test/TestHtmlHost.ps1` | not required |
| CSS | `BuildCss.ps1` | `TestCssHost.ps1` | not required |
| Layout | `BuildLayout.ps1` | `TestLayoutHost.ps1` | not required |
| Paint | `BuildPaint.ps1` | `TestPaintHost.ps1` | not required |
| Rasterizer | `BuildRasterizer.ps1` | `TestRasterizerHost.ps1` | not required |
| Desktop | `BuildDesktop.ps1` | `TestDesktopHost.ps1` | `TestDesktopQemu.ps1` if guest-visible |
| Window | `BuildWindow.ps1` | `TestWindowHost.ps1` | display QEMU if needed |
| Input | `BuildInput.ps1` | `TestInputHost.ps1` | not required |
| Kernel | `BuildKernel.ps1` | `TestKernel.ps1` | `TestKernelQemu.ps1` |
| Bootloader | `BuildBootloader.ps1` | `TestBootloader.ps1` | bootloader QEMU selftest |
| Public interface change | owning build + dependents | owning host tests | `TestIntegrationQemu.ps1` |
| Everything | `BuildEverything.ps1` | `TestEverything.ps1 -SkipQemu` | `TestEverything.ps1` |

Do not force unit tests through QEMU.

---

## Development rules

- Put source files in the owning subsystem. Do not create parallel implementations.
- Do not create a second build system. `Scripts/Build/*.ps1` call `Runtime/Tools/Build.ps1` or `Scripts/pipeline/robo.ps1`.
- Do not bypass subsystem APIs.
- Do not edit generated files under `Build/`.
- Do not introduce circular dependencies.
- Do not add Chromium dependencies.
- Do not scatter documentation outside `Documentation/` except a subsystem `README.md` pointer.
- Do not create a fake Host Runtime / FakeOS / BrowserRuntime.
- Do not make the entire project rebuild for a single-module change. Use the module script. Objects are incremental.
- Do not create unnecessary global state.

---

## New feature workflow

1. Identify the subsystem.
2. Open its `Runtime/Source/` (or `Kernel/`, `Bootloader/`) folder.
3. Change the public header only if the contract must change.
4. Implement inside the owning subsystem.
5. Add or update tests under `Tests/<Subsystem>/`.
6. Run the subsystem build script.
7. Run the subsystem host tests.
8. Run QEMU tests if the guest image or boot path is affected.
9. Run integration tests if a public interface changed.
10. Update `Documentation/` if architecture or API changed.

---

## Color palette

Product UI color tokens: `Documentation/Development/ColorPalette.md`.
