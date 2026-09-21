# RoboDesktop

The native shell for the RoboticelaOS Runtime (**Phase 11.13 — the first release in this tree that
puts a picture on a screen**).

**Version:** 0.1.0 — `ROBO_DESKTOP_API_FROZEN=0`

`ROBO_DESKTOP_SHELL_IMPLEMENTED=1` since this module's first release: **an owned screen target, a
desktop surface, a panel with the platform's name and the running applications, a work area, shell
windows with real decorations, a deterministic frame lifecycle with a record and a checksum, a resize
path, and a presentation door into a native window's render surface or any writable surface a caller
names.** `ROBO_DESKTOP_DOCUMENTS_IMPLEMENTED=1` is the honest companion, and the pair is the module's
central distinction: markup *can* become a window's contents (through the bridge in
`RoboDesktopDocument.h`), while the **shell itself builds no document** — `documents` is **0** in its
capability record because the shell paints with graphics fills and never reads a page.

RoboDesktop is the first module above the layout stack that owns a **screen** rather than a page.
Everything below it answers *what does this document look like*; a desktop answers a different
question — *what is on the display* — which is why it is a module and not a mode of the renderer.

```text
RoboHTML → RoboCSS → RoboLayout → RoboPaint → display list
                                                   ↓
                                          RoboRender (a frame)          ← a window's contents
                                                   ↓
                 RoboDesktop (surface, panel, windows) → the screen target
                                                   ↓
             RoboRender (present) → RoboWindow's render surface → Win32 / QEMU framebuffer
```

`RoboRender`'s presenter is the other half of this phase (`render/README.md`,
`render/docs/Presentation.md`); this module is the half that decides *what* is on the screen.

## The boundary this module exists to hold

A desktop that was a web page would be the easiest thing in the world to build here and the wrong
thing: the shell would then need a document pipeline, a layout pass and a paint pass to draw a
taskbar, and a broken document would take the screen with it. So the split is stated once and enforced
by a source scan (`Assert-RoboDesktopBoundaries` in `Tools/Build.ps1`):

- the **shell** (`RoboDesktopChrome.c`, and the rest of `Source/RoboDesktop`) draws with RoboGraphics
  primitives and knows nothing about HTML, CSS or layout;
- an **application window's contents** are a `robo_paint_display_list_t` produced elsewhere — by
  `RoboHTML → RoboCSS → RoboLayout → RoboPaint`, in this build — and rasterised by RoboRender into the
  window's own target before being composited under the chrome;
- the **screen** is a target this module owns, presented into whatever destination the caller's window
  or framebuffer gives it.

**One file is exempt by name:** `RoboDesktopDocument.c`/`.h`, the bridge that turns markup into a
display list. The exemption is a file rather than a convention precisely so that it is visible in the
build: a build that removed the bridge would keep a working shell, and a build that let the shell read
markup would have to change the scan.

The consequence worth naming: a desktop with **no applications** still draws, still presents, and still
reads as a desktop, because nothing about the shell depends on a document existing. The phase's own
smoke test is exactly that desktop (`desktop.frame.sequence`: `windows == 0`, `fills == 6`).

## The first structural decision: the screen target is a RoboRender target, and there is no third framebuffer type

`desktop->target` is a `robo_render_target_t` — owned, RGBA8, zero-filled at creation — and a frame
paints *into* it from its first fill. The tree already has two framebuffer types
(`robo_gfx_framebuffer_t` from RoboGraphics' Goal 6.3, `robo_qemu_fb_info_t` from RoboDisplay's Goal
7.12) and each has an owner and a reason; a third would be a third name for the same bytes. So the
phrase "the desktop's framebuffer" in this module means **the screen target its frames are painted
into**, and the interesting question — "can a frame reach a display's mapping?" — is answered by
`robo_render_present_framebuffer`, which takes RoboGraphics' own type.

There is also **no clear step**. The first thing a frame does is fill the whole screen with the
desktop surface, which is opaque and therefore *is* the clear; a separate clear would be a second
full-surface write to reach the same state, and a frame that cleared and then painted a translucent
desktop would be relying on the clear's colour rather than on its own.

## The second structural decision: a frame is three calls, and a frame does not present itself

`begin_frame` → `paint` → `end_frame` is the same triple RoboWindow uses, and for the same reason: a
record of the frame is worth having, and a record can only be honest if the caller controls where the
frame starts and stops. `robo_desktop_render` is the one-call shorthand for the caller with nothing to
interleave, and `desktop.frame.sequence` asserts that the shorthand's record **equals** the
sequence's — a shorthand that could drift from what it abbreviates would be a second behaviour.

Painting twice in one frame is refused (`ROBO_BUSY`) rather than silently nested, because a frame whose
halves were painted twice would composite every translucent thing twice, and the record would say so
only if the counters happened to be watched. An **unpainted** frame — `begin_frame`, `end_frame`, no
paint — is legal and produces a record of nothing (`frames_without_windows` counts it), which is the
honest reading of "the frame never painted".

`robo_desktop_present` is a *separate* door, and the separation is deliberate: it is what lets a caller
render a frame and look at it without a display (a test, a screenshot, a headless build), and what lets
one rendered frame be presented twice or into two destinations. A frame that presented itself would
make both impossible and would hide the "did it reach the screen?" answer inside a call that also drew
things.

## The third structural decision: a window borrows its contents, and its contents are one picture

`robo_desktop_window_set_content` takes a `const robo_paint_display_list_t *` — borrowed, never copied,
and it must outlive the window's last paint. A window **without** contents is a legitimate window: it
paints its body and decorations and its record says `content_runs == 0`.

Each window with contents owns a target the size of its client area, created **lazily** and recreated
only when the client size changed, so a resize of eight windows allocates nothing until the next paint.
The list is executed into that target and the result is composited into the screen at the client
rectangle's origin. Three consequences, and all three are why the contents are not composited straight
into the screen:

- a window's contents are **one picture** — they share no clip and no opacity scope with the shell, so a
  `push-clip` inside a document cannot reach the panel;
- a window whose contents fail to rasterise still leaves a **window** on screen — its body and
  decorations are painted before the composite — and the failure is *reported* (`ROBO_NOT_SUPPORTED`,
  `WINDOW` domain) rather than swallowed, while the painting continues so the panel and the other
  windows are still drawn;
- the same content target is what a future compositor would take as a layer, which is the boundary this
  build stops at rather than one it crosses.

The client target is cleared to the window's body colour before the document runs, so a document that
draws nothing leaves a window rather than a hole showing the desktop through it.

## The fourth structural decision: the z-order is the array's order

Eight slots (`ROBO_DESKTOP_MAX_WINDOWS`), `windows[0]` furthest back, `windows[count - 1]` in front. A
list rather than a per-window z field, because the two have to agree: with an order field, "paint in
z-order" is a sort whose tie-breaking is a second decision and "raise" is a renumbering of everything it
moved past; with the array, raising is one `memmove` to the end and the painter is a loop. Focus is
**the front window** — `raise` focuses as it raises, and `focus` is the door that focuses without
raising — so there is no second focus model to drift from the first, and removal repairs the focused
index rather than letting it dangle.

A window below the decoration minimum is **grown** rather than refused: the minimum is a property of the
decorations the shell draws (not of the caller's request), so the smallest drawable window is the
closest thing to what was asked for. That is deliberately the opposite of `robo_window_set_size`, whose
constraints are the *caller's* policy — `RoboDesktopWindow.c` states both so the divergence reads as one
decision rather than two.

## The fifth structural decision: closing is a request, not a destruction

`robo_desktop_window_request_close` — what the close button produces — marks a window `CLOSING`, stops
painting it, and leaves it in the list. **The owner destroys it.** A caller that keeps state per window
(an application, a document, an unsaved edit) is therefore told before the object vanishes rather than
after, which is the one place this module deliberately does less than it could.

## The sixth structural decision: the shell is deterministic, and the clock is the caller's

A frame is a function of the target's size, the theme, the metrics and the windows — nothing else.
Nothing here reads a clock, asks for the time, allocates in an order that shows, or iterates a hash: the
status text (a clock, a battery, a version) is a string the **caller** supplies, the z-order is an
array's order, and the record a frame produces is a struct of counts. Two frames of one state are
byte-identical, and `robo_desktop_frame_checksum` is the door that lets a caller check it rather than
believe it; the dump carries **no address** — asserted, not promised.

`desktop.frame.determinism` is the strongest form available: two independently created desktops, each
with a window whose contents came from the same markup, rendered and compared byte for byte with their
records and checksums beside them.

## The seventh structural decision: hit testing answers about *windows*, and the pointer does two things

`robo_desktop_hit_test` walks **front to back** — a point over two windows belongs to the front one,
which is the answer a *user* would give — and reports a `robo_desktop_area_t` rather than a boolean:
background, panel, a window's title bar, a window's client area, a window's close button. That
vocabulary is what an *application* would eventually be handed, and it is stated now because the shell's
own two actions are expressed in it rather than in pixels.

`robo_desktop_observe_pointer` takes a point and whether a button is down *now*, and does exactly two
things: a press on a title bar or client area raises and focuses that window, and a press on a close
button requests that window's close. Nothing is delivered to a window's contents: hit-test delivery to
applications is a later phase's, and a shell that invented a delivery protocol now would be inventing
the ABI those phases have to agree on. A pointer outside the screen is `AREA_NONE` with `inside == 0`
rather than an error — a pointer can leave a window, and that is not a failure.

## The eighth structural decision: geometry is a value, and the labels are not

`RoboDesktopGeometry.h` computes the screen, the panel, the work area and the panel's three label areas
from a size and a metrics record — **arithmetic on rectangles, testable with no display, no window and
no frame**, which is what turns the brief's "use explicit geometry, do not hard-code a resolution" into
an assertion instead of an intention. The three label areas are thirds of the panel's width between the
two insets rather than measured widths, because the areas have to exist *before* a font does; a caller
that has one narrows an area to its measured width through `robo_desktop_geometry_panel_area`.

Placement (`place`) and clamping (`contain`) are two doors rather than one because they answer different
questions: a frame **smaller** than the work area is *moved* until it is inside (a window dragged past
an edge is pulled back, not shrunk) and a frame **larger** than it is *shrunk* to it (a window whose
contents no longer fit the screen must still be visible). `add` and `set_bounds` clamp; `place` is what
a caller uses when it does not know the work area, which is the only state a session's first frame has.

## Presentation

| Door | Destination |
|---|---|
| `robo_desktop_present_to_surface` | any writable RoboGraphics surface, at an offset — the guest path's door (a display framebuffer wrapped as a surface) and a test's door |
| `robo_desktop_present` | the attached native window: RoboWindow's own frame triple around the presenter, ending in `present_frame` and a backend |

`stats.presentations` counts the **window** path, because that is the path with a backend behind it; a
to-surface copy is a caller's own handoff and is described by the `robo_render_present_record_t` it
returns. Nothing is paced — this call waits for no vertical blank — and a frame whose size differs from
the window's is **clipped** and reported rather than refused, because a window that shrank between
rasterising and presenting must show the part of the picture that still fits.

## Reading a pixel, and the door this module deliberately does not have

`robo_desktop_frame_surface`, `_bytes` and `_checksum` are this module's doors; reading a pixel goes through
RoboGraphics' own `robo_gfx_surface_get_pixel`, and encoding a screenshot through
`robo_gfx_surface_encode`. That is why the suites' expectations can be stated in the *theme's* colours
rather than in hand-computed hexes.

## Capability table — the eight presences and the ten absences

`robo_desktop_capabilities_t` has **18 flags**, read by name and in declaration order by
`desktop.conformance.capabilities`, so a phase that flips one has to change that case deliberately.

**Present (8):** `shell`, `windows`, `window_content`, `presentation`, `frame_records`, `resize`,
`pointer_targeting`, `shell_text`.

**Absent (10), and each one is a structure rather than a forgotten flag:**

| Flag | What it would need |
|---|---|
| `documents` | **0** because the *shell* builds no document. The bridge beside it does (`ROBO_DESKTOP_DOCUMENTS_IMPLEMENTED=1`), and that is the whole point of the split |
| `compositor` | a layer tree with its own ordering and damage union; here the shell draws into one target and each window's contents are one picture |
| `tiles` | independently repaintable pieces of the screen — a frame is a frame |
| `damage_tracking` | a history of what was on the screen, so "unchanged" needs no comparison |
| `frame_scheduling` | a clock, a deadline and a queue — absent, and this module's determinism depends on the absence |
| `vsync` | waiting for a display's vertical blank; presentation here is synchronous and unpaced |
| `gpu` | a device, a command buffer and a resource manager; everything here is CPU and software |
| `window_manager` | snapping, tiling, virtual desktops, animation, maximize and minimize |
| `input_delivery` | routing a keyboard and delivering hits to applications (the phase's §16 names where this ends) |
| `desktop_environment` | a launcher, a task manager, settings, notifications, a file manager, a terminal |

## Documentation

| Document | Contents |
|---|---|
| `desktop/docs/Architecture.md` | The module's files and ownership, the limits and the reasoning for each default number, the capability and diagnostic vocabularies, the boundary scan |
| `desktop/docs/Geometry.md` | The four rectangles and the three panel areas, `compute`/`panel_area`/`place`/`contain`, hit testing, resize and `sync_size` |
| `desktop/docs/Windows.md` | The shell window object, the decoration metrics, the content seam and its lifetime, z-order and focus, closing, the pointer's two actions |
| `desktop/docs/Frames.md` | The frame triple, the paint order, the record and its validity rule, the seven-line dump, determinism, the presentation doors |
| `desktop/docs/Documents.md` | The markup bridge: why it is a separate file, what it owns and the order it releases in, the viewport-as-page-box rule, the report, the failure mapping |
| `desktop/docs/Tests.md` | Suite map (seven files, 46 cases), fixtures, the PNG checkpoint, running, and what this phase's suites found |

## The scene, and the two launchers

The module also carries the checkpoint's own **fixture** — the one arrangement somebody looks at to decide
whether the shell is right — because a shell is a product and a scene is what shows it:

| File | Is |
|---|---|
| `Include/RoboDesktop/RoboDesktopScene.h`, `Source/RoboDesktop/RoboDesktopScene.c` | The scene: the checkpoint's markup, author sheet, window title and arrangement, plus the order a scene is opened, stepped and released in. It is compiled into the host suite binary and the guest program and into nothing else — the shipping runtime has no scene |
| `Include/RoboDesktop/RoboDesktopHost.h`, `Source/RoboDesktop/platform/host/RoboDesktopHost.c` | The host launcher: `RoboTest.host.exe --desktop` runs the scene into a native Win32 window (or offscreen, with pixel probes and a PNG) |
| `user/robo-runtime/main.c` | The guest launcher: the program `init` starts on every boot, which drives the same scene into the QEMU framebuffer |
| `desktop/assets/Inter.ttf`, `desktop/assets/Inter-License.txt` | The system face the shell's labels are drawn with, and its licence. Packed at `/System/Fonts/Inter.ttf` |

Two launchers exist because a launcher is the one part that *must* differ (which window backend is
registered); everything visible is shared, so the two backends show one desktop and report one set of frame
counters. `docs/userspace/desktop-runtime.md` is the guest side's own document.

## Building and testing

```bash
# from runtime/
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Debug -Verify -Test
./Out/RoboTest.host.exe --filter desktop.
```

`--filter` is a substring match; no case outside this module contains `desktop.`, so the filter reports
these **46** cases and nothing else.

The checkpoint in a window, or as a picture, without the suite:

```bash
./Out/RoboTest.host.exe --desktop --no-window --shot build/desktop.png   # offscreen + PNG + probes
./Out/RoboTest.host.exe --desktop                                        # a native window
```

## Boundaries

RoboDesktop sits **directly above RoboRender** (a target, a presenter, a raster) and reads RoboWindow
(the native window it presents into), RoboFont (a face it is *handed*), RoboGraphics (fills, lines, the
composite and the glyph bridge) and RoboPaint (the display list a window borrows). It must not name
Chromium, and the shell must not name a document layer — `Assert-RoboDesktopBoundaries` enforces both,
with three named exemptions and no others: `RoboDesktopDocument.c`/`.h` (the bridge), `RoboDesktopScene.*`
(the fixture that consumes it) and `RoboDesktopHost.c` (the launcher that drives the fixture). The filter is
a name list rather than a directory on purpose: a *fourth* file reaching for a document layer is caught
rather than covered by an exception written for these.

Everything above this module (a compositor, a window manager, a desktop environment, input delivery to
applications) is named as absent rather than stubbed, and every absence is a **0** in the capability
table beside the flag for what is present.
