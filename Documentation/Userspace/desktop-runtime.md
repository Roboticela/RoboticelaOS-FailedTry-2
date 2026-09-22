# The desktop runtime

`/System/runtime/robo-runtime` (source: `user/robo-runtime/main.c`) is the program
RoboticelaOS boots to. It is the first thing on this system that is *applications
code* rather than a service or a test: init starts it, it takes the display, and it
draws the desktop.

```text
init (user/init/config.c)
   │  the only service a normal boot starts
   ▼
robo-runtime
   │  the shared checkpoint scene (Include/RoboDesktop/RoboDesktopScene.h)
   │     markup + author sheet
   │        → RoboHTML tokenizer and tree builder
   │        → RoboCSS style context → RoboLayout → RoboPaint (display list)
   │        → RoboRender rasteriser (a frame)
   │  the shell (RoboDesktop): background, panel, one window and its decorations
   │  RoboWindow begin / end / present_frame
   ▼
SYS_DISPLAY → the UEFI GOP framebuffer
```

Input travels back along the same path: `SYS_DISPLAY` `INJECT` → the kernel's input
queue → this program's RoboWindow backend → the window queue →
`DesktopObservePointer` → the shell's action (a raise, a focus, a close).

## What it is not

It is not a demo page, and not a second renderer. The layer that used to live here
painted one HTML page straight into the framebuffer with its own throwaway adapter
and its own bitmap font; it was deleted in Phase 11.13, and the desktop shell
replaced it. The program now links the *real* stack — RoboHTML, RoboCSS, RoboFont,
RoboLayout, RoboPaint, RoboRender, RoboGraphics, RoboDisplay, RoboWindow and
RoboDesktop — and the picture is whatever that stack produces.

## One scene, two launchers

The scene (a panel, a window, one document) is defined once, in
`RoboDesktopScene.c`, and driven by two launchers:

| Launcher | Backend | Starts with |
|---|---|---|
| `user/robo-runtime/main.c` | QEMU/UEFI framebuffer | `init`, on every boot |
| `runtime/Source/RoboDesktop/platform/host/RoboDesktopHost.c` | a native Win32 window | `RoboTest.host.exe --desktop` |

They are separate programs because a launcher is the one part that *must* differ:
one registers the QEMU window backend, the other the Win32 one. Everything visible is
shared, so what `scripts/robo run-runtime` shows in a window is what
`scripts/robo run-runtime-qemu` shows on the framebuffer — and the frame counters
agree because they are the same frame. The host launcher's offscreen mode
(`--desktop --no-window`) prints them:

```text
frame: 1280x800 fills=12 labels=4 unshaped=0 windows=1 content_runs=1 content_pixels=652912
```

## The system face

The shell's labels and the document's text are drawn with a real font:
`desktop/assets/Inter.ttf`, packed into the image at `/System/Fonts/Inter.ttf` by
`scripts/roboboot-build-common.ps1`. The program reads it, builds a `FontFace`
from the bytes, and hands it to the scene.

Without that file the desktop still paints — panel, window chrome, document fills —
but every label is *counted* instead of drawn (`ROBO_QEMU_DESKTOP_UNSHAPED`), which is
why the display gate asserts that count is zero rather than only that a picture exists.
Loading reports the step it failed at (`ROBO_QEMU_FACE_FAIL <stage> <path>`: `stat`,
`alloc`, `open`, `read`, `source` or `face`), so a missing file reads differently from
an unparseable one.

## Serial markers

Serial markers (`ROBO_QEMU_*`) are a **test checkpoint**, not a production
boot. Init starts `robo-runtime` with `checkpoint` only when the ESP has
`dtest` (display tests) or `selftest` (kernel tests). A normal boot opens
the shell and pumps input with no serial dump and with log level at warn.

When a checkpoint is requested, the program reports facts rather than
verdicts: the pipeline's marker list is what pins their values
(`scripts/pipeline/commands.test.ps1`), because a guest that checked
its own arithmetic against itself would be checking nothing.

| Marker | Means |
|---|---|
| `ROBO_QEMU_DISPLAY_OK` `_FB_W` `_FB_H` `_FB_PITCH` `_FB_PIXFMT` | the display came up, and at what size and format |
| `ROBO_QEMU_FACE_OK` / `ROBO_QEMU_FACE_FAIL <stage> <path>` | the system face was read, parsed and mapped, or where that stopped |
| `ROBO_QEMU_DESKTOP_OK` `_WINDOW` `_STATUS` | the shell, its window and the document came up |
| `ROBO_QEMU_DESKTOP_WINDOWS` `_CONTENT_RUNS` `_LABELS` `_UNSHAPED` `_FILLS` `_PIXELS` | what the frame carried |
| `ROBO_QEMU_PRESENT_OK` `_COMPLETE` `_PIXELS` | the frame reached the backend, and how much of it landed |
| `ROBO_QEMU_DESKTOP_FRAMES` `ROBO_QEMU_INPUT_ACTIONS` | the shell's own counters |
| `ROBO_QEMU_INPUT_OK` / `_INPUT_PARTIAL` | an injected press reached the shell as an action or did not |
| `ROBO_QEMU_DESKTOP_LIVE` | checkpoint done; the shell keeps pumping keyboard and mouse |
| `ROBO_QEMU_DISPLAY_FAIL <stage>` | a stage refused; nothing after it runs |

## Building it

`robo-runtime` is the one guest program whose sources are *discovered* rather than
listed: `scripts/roboboot-build-common.ps1` names the modules it needs
(`RuntimeModules`) and compiles every `.c` under each module's source directory, minus
the test directories and the platform backends of the platforms a guest is not
(`win32`, `host`). The rule exists because a copied list drifts: the previous one went
on compiling the deleted demo layer for two phases after the host had a real shell.
A module added to `RuntimeModules` now reaches the guest image by having a source file.

The program is packed twice, at `/bin/robo-runtime` and `/System/runtime/robo-runtime`,
because init starts it by the second path and a shell would find it on the first.
