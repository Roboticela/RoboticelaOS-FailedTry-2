# RoboDesktop Architecture (Phase 11.13)

This module's shape: what its files own, the numbers it ships with and why each is that number, the
vocabularies a caller reads, and the boundary the build enforces. `desktop/README.md` states the
module's *decisions*; this document states its *inventory*.

```text
RoboHTML → RoboCSS → RoboLayout → RoboPaint          (a display list; built by RoboDesktopDocument)
                                        ↓
                                  RoboRender          (a target: a window's contents, or the screen)
                                        ↓
RoboDesktop: shell fill → panel → windows in z-order → the screen target
                                        ↓
                            RoboRender's presenter → a surface / a native window
```

---

## 1. Files and what each owns

| File | Owns |
|---|---|
| `RoboDesktop.c` | The shell's lifecycle, the frame triple, the resize path, the presentation doors, hit testing and the pointer, the statistics |
| `RoboDesktopGeometry.c` | The metrics defaults and the arithmetic: `compute`, `panel_area`, `place`, `contain` |
| `RoboDesktopWindow.c` | The window list: add/remove, bounds and clamping, title and contents, z-order, focus, the close request |
| `RoboDesktopChrome.c` | The shell's own painting: the surface, the panel, its labels, a window's decorations, the close button, a title bar's text |
| `RoboDesktopDocument.c` | The markup bridge: HTML + CSS → layout → paint → a display list (the **one** file allowed to name a document layer) |
| `RoboDesktopDump.c` | The seven-line developer dump, written through the public doors only |
| `RoboDesktopDiagnostics.c` | The diagnostic record and the domain vocabulary |
| `RoboDesktopTypes.c` | Area and state names, their predicates, the count constants |
| `RoboDesktopTheme.c` | The default palette |
| `RoboDesktopText.c` | Three string helpers, because the guest target has no hosted C library |
| `RoboDesktopInternal.h` | The shell's state and the chrome's private doors — **not installed**, and no public header includes it |
| `RoboDesktopScene.c` | The checkpoint's own definition: its markup, author sheet, window title and arrangement, and the order a scene is opened, stepped and released in. Compiled into the host suite binary and the guest program only |
| `platform/host/RoboDesktopHost.c` | The host launcher: `RoboTest.host.exe --desktop`, the scene in a native window or offscreen with pixel probes and a PNG. Built for the host alone |

RoboDesktopScene is the module's *fixture* rather than a second shell: a shell is a product and draws what
it is told, while a scene is the one arrangement somebody looks at to decide whether the shell is right.
It is written once and driven by two launchers — this module's host one and `user/robo-runtime/main.c` on
the device — because everything visible must be shared and only the window backend may differ. It is in
`Source/RoboDesktop` rather than a directory of its own for the same reason it is compiled into the suite
binary: the layer it exercises is this one.

### What the shell owns, and what it borrows

**Owned:** the screen target (a RoboRender target), the geometry and metrics copies, the three label
strings, the window list and each window's content target, and a native window **only** if
`config.create_window` asked for one.

**Borrowed, never released:** the chrome face (`config.face`), each window's content display list and
its content face, and an *attached* native window — the same ownership rule
`WindowSetPresentSurface` states for a borrowed surface. A caller that destroys an attached
window while the desktop is live has done what the door's contract allows; the desktop does not defend
against it, because defending would mean retaining a window it was told it does not own.

The **content target** is the one resource created lazily: a window with no contents never allocates
one, and a window whose client size changed gets a new one at the next paint rather than at the resize.
That is deliberate — a resize of eight windows would otherwise allocate eight targets before anything
was drawn — and it is why `DesktopResize` releases every window's target and lets the frame
recreate what it needs.

---

## 2. The default numbers, and the reasoning for each

Metrics are records, not constants, so a caller can restyle the shell without a new API. These are the
defaults this build ships.

### The panel (30 px)

| Field | Value | Why |
|---|---|---|
| `height` | 30 | Room for a separator row, a 12-pixel identity mark and an 11-pixel capitalised label with six pixels above and below it — and, the binding constraint, **taller than a window's 24-pixel title bar**, so the two never read as the same element repeated |
| `separator_height` | 1 | A hairline. The row is inside the band, so the work area starts exactly where the panel ends |
| `identity_inset` | 14 | The distance from the screen's left edge to the identity mark: enough that the mark reads as being *in* the panel rather than at its corner |
| `identity_mark_width` × `height` | 3 × 12 | The accent mark. 3 px is the narrowest bar that survives on a 1× display without antialiasing; 12 is 40% of the band, which is where a mark stops reading as a border |
| `label_inset` | 16 | The distance from the screen's right edge to the status text. Larger than the mark's inset on purpose: a clock hugging the corner the way the mark does reads as a rendering accident |
| `baseline_from_bottom` | 11 | `(height + cap) / 2` with a cap of 8 gives a baseline 19 px from the band's top, i.e. 11 from its bottom. See §4 |

The two falloff rows under the panel (`shadow_near` 0.06, `shadow_far` 0.03 — both black at two alphas,
so the falloff darkens *any* surface beneath it by the same amount) are what a real blur would be if
this build had filters. It does not; a two-row step is honest about that and costs two fills.

### A window

| Field | Value | Why |
|---|---|---|
| `border` | 1 | The cheapest frame that still reads as a frame, on all four sides |
| `title_height` | 24 | One pixel *less* than the panel's label band, which is what keeps the two from looking like one element |
| `title_inset` | 10 | The title text's distance from the frame's inside edge |
| `close_size` | 10, `close_inset` 6 | The button's centre lands 11 px from the window's top-right corner — where a hand expects it — and the button stays inside a 24-pixel bar with 6 px of clearance above and below |
| `content_inset` | 0 | The client area's top is the title bar's bottom edge; a non-zero value here is a restyle, not a default |
| `min_width` | **10** | `2 * border + 8`: the frame plus eight pixels of client area |
| `min_height` | **34** | `2 * border + title_height + 8`: a title bar with eight pixels of client area under it |

Both minima are **computed from the fields above** rather than written as literals, so a caller that
restyles the decorations gets a consistent minimum for free. `desktop.geometry.metrics` asserts the
arithmetic rather than the numbers, and `desktop.window.add.minimum_and_bounds` asserts what follows
from it (a 4×4 request is grown to the minimum).

### Limits

| Macro | Value | Why |
|---|---|---|
| `RoboDesktopMaxWindows` | 8 | Every frame composites every window in order, so the count is also the per-frame cost; eight is what a first desktop needs (a frame, plus room to prove `raise` and `close` change the screen) and small enough that the list's geometry is arithmetic a suite can pin |
| `RoboDesktopTitleMax` | 128 | A copied UTF-8 title including the NUL. An over-long title is **refused**, never truncated: every string this module stores was named by a caller |
| `RoboDesktopStatusMax` | 64 | The same, for a clock or a battery label |
| `RoboDesktopMaxDimension` | 16384 | Bounds an edge before any allocation, so a stride and a byte size fit in `size_t` on a 64-bit target. The same bound RoboRender states, for the same reason |
| `RoboDesktopMinDimension` | 16 | The smallest work area a window may be placed in, per axis. Geometry does not enforce it (a thin work area is a rectangle and stays readable); `DesktopWindowAdd` is the door that refuses, because placing a window the shell cannot draw is the alternative |
| `RoboDesktopDefaultLabelSizePx` | 11.0 | The panel's label size, and the size a window's title uses when the caller does not choose |

### The palette

Three levels of depth and one accent. The desktop surface is `#EDEDEF` (the deepest neutral, so a
window's white body reads as raised without a shadow pixel), the panel is `#FFFFFF`, and the hairline
between them is `#D2D2D7`. The accent `#0E7AFF` appears **exactly twice** — the identity mark and the
focused window's border — because one accent used twice reads as a system and one used five times reads
as decoration. The close button is the danger token `#FF3B30`, deliberately *not* hover-styled: this
build has no hover state, so a colour meaning "the pointer is over it" would be a colour nothing could
show.

`DesktopThemeIsValid` requires the two **surfaces a frame's readability depends on** — the
desktop surface and a window's body — to be opaque, because a translucent desktop would composite over
whatever the target held before. The falloff colours carry alpha by design and are exempt.

---

## 3. The vocabularies

### Areas — what is under a point

`NONE` (outside the screen), `BACKGROUND`, `PANEL`, `WINDOW_TITLE`, `WINDOW_CONTENT`, `WINDOW_CLOSE`.
`RoboDesktopAreaCount` is 6, asserted positionally by `desktop.conformance.vocabulary`. This is the
vocabulary an **application** would eventually be handed, which is why the pointer door speaks it rather
than pixels.

### States — a window's life

`OPEN`, `CLOSING`. The middle of the story is `CLOSING`: a window the user asked to close stops painting
and stays in the list until its owner destroys it. There is no `ICONIFIED`/`MAXIMIZED`/`MINIMIZED` — a
window manager is a 0 in the capability table, and a state with no behaviour behind it would be a state
a caller could not act on.

### Diagnostic domains — *which object* failed

`NONE`, `SHELL`, `WINDOW`, `SURFACE`, `DOCUMENT`, `DUMP` (`RoboDesktopDomainCount` is 6). Organised
by owner rather than by phase of a frame, because a desktop's failures go to different places: a
`SHELL` failure means "ask for something else", a `WINDOW` failure means "change the window", a
`SURFACE` failure means "attach a window or resize", and a `DOCUMENT` failure means the *application's
markup* was refused and the message names which stage refused it.

### Capabilities — 18 flags, 8 present and 10 absent

The eight presences (`shell`, `windows`, `window_content`, `presentation`, `frame_records`, `resize`,
`pointer_targeting`, `shell_text`) and the ten absences are listed with what each absence would need in
`desktop/README.md`'s capability table. Two flags need one sentence each here, because they are the two
a reader is most likely to misread:

- **`documents` is 0 while `RoboDesktopDocumentsImplemented` is 1.** They are not in conflict: the
  flag says the *shell* builds no document (a broken page cannot take the screen with it), and the
  constant says the *runtime* can build one for an application window through
  `RoboDesktopDocument.h`. A build that removed the bridge would keep a working shell and would set the
  constant to 0; a build that let the shell read markup would have to change the boundary scan.
- **`presentation` is 1 while `compositor` and `vsync` are 0.** "A frame can reach a destination" and
  "more than one surface has an order" are different claims, and so are "the pixels arrived" and "they
  arrived in step with a display".

---

## 4. The drawing rules that are architecture, not decoration

1. **The first fill is the clear.** A frame's first operation fills the whole screen with the desktop
   surface — opaque, therefore a clear. There is no separate clear step anywhere: a second full-surface
   write to reach the same state would be waste, and a frame that cleared and then painted a translucent
   desktop would depend on the clear's colour instead of its own.
2. **One baseline rule for both bands.** `(band + cap) / 2` with a cap of 8, reached through the panel's
   `baseline_from_bottom` metric and through the same arithmetic in a title bar. A panel label and a
   window title that disagreed about where a baseline goes would read as two typesettings on one screen.
3. **Labels are drawn one glyph at a time, through the pipeline's own bridge** (the face's cmap, the
   graphics module's glyph scaling, the raster's stroke). A shell that measured its own text would be a
   second font-metrics implementation that disagrees with the first the day a face changes — and the pen
   is snapped by the graphics module's own policy, so shell text and document text are placed by one
   rule.
4. **`clipped` is a statement about a drawing, not about a surface.** The raster's clip is reset to the
   screen for the whole frame, so the count in the record is the number of shell drawings the *screen's
   own bounds* cut — a window partly off the screen is a legitimate state, and the count is how a caller
   learns that a placement shows less than it asked for.
5. **A window's contents are rasterised into the window's own target, then composited.** Not straight
   into the screen: a document's `push-clip` must not be able to reach the panel, a document that fails
   to draw must still leave a *window* on screen, and the target is what a future compositor would take
   as a layer.

---

## 5. The boundary the build enforces

`Assert-RoboDesktopBoundaries` in `Tools/Build.ps1` scans `Include/RoboDesktop` and
`Source/RoboDesktop` and refuses:

- **Chromium** — no source in this module may name a Chromium or Blink type;
- **a document layer in the shell** — RoboHTML, RoboCSS, RoboLayout or layout-tree names are refused in
  every file except three, matched by name (an exemption that is a *file* is one a reader can see in the
  scan's own output): `RoboDesktopDocument.*`, the bridge; `RoboDesktopScene.*`, the fixture that consumes
  it; and `RoboDesktopHost.c`, the launcher that drives the fixture;
- **a higher layer** — anything above this module.

The exemptions are three files and the asymmetry is the same one `Assert-RoboRenderBoundaries` states one
module down: the *module* is held to both halves of the rule, while the suites and the documents are held
to the upward half only — a desktop fixture is a render fixture plus a shell and must build a document to
have a window with contents, and a suite that reached for a compositor to look at its own frame would be
the leak worth catching.

---

## 6. Determinism, as a property of the code

A frame is a function of the target's size, the theme, the metrics and the windows. Concretely:

- nothing in this module reads a clock, and the status text is a value the caller supplies;
- the z-order is an array's order (never a sort, never a hash);
- every counter is derived rather than stored alongside a second authority (the dump's "visible" count
  is computed from the list, for instance);
- the dump contains **no address** — asserted by `desktop.dump`, in the form `=0x`, which is what a
  printed pointer looks like;
- the target has a byte view, a checksum and a byte-level equality, so "two frames are the same" is a
  comparison rather than an argument.

The one thing that *is* stateful across frames is the statistics record, and it is folded in at
`end_frame` — a caller that never ends its frame gets no statistics, which is the honest reading of "the
frame never finished".

### Debug output

The module logs at the four lifecycle points a person debugs from, and at two that are otherwise
invisible:

| Line | Where |
|---|---|
| `Desktop: shell created WxH (window=owned\|attached\|none, face=yes\|none)` | `DesktopCreate` |
| `Desktop: window added id=... bounds=x,y WxH (count=...)` | `DesktopWindowAdd` — the **bounds**, not the request, because a grown or clamped window is the fact that is usually being wondered about |
| `Desktop: window close requested id=...` / `window destroyed id=... (count=..., focused=...)` | the two halves of a window's end, which is where "why is this window still on screen?" is answered |
| `Desktop: resized WxH -> WxH (windows_reclamped=...)` | `DesktopResize`, **only** when the screen actually changed — a caller polling a window's size must not fill a log |
| `Desktop: document built WxH (elements=..., boxes=..., texts=..., records=..., commands=...)` | the bridge, in pipeline order, so a blank window names the stage that produced nothing |
| `Desktop: document refused (code): message` (WARN) and `window contents could not be drawn (id=...): message` (WARN) | the two failures a caller can otherwise only see as an empty rectangle |

The rules the dump keeps hold here too: no addresses, no clock, and everything a log line names is a
fact about the shell's own state. Per-frame events (a paint, a presentation) are deliberately **not**
logged — a driven shell would fill the output — and the failure warnings are per frame rather than once,
because a broken document is a state the caller is in, not an event that happened.

---

## 7. What this architecture deliberately is not

Not a compositor (one target, no layer tree, no damage union), not a window manager (no drag, no snap,
no maximize), not a frame scheduler (no clock, no queue, no VSync), not a desktop environment (no
launcher, no notifications, no settings), not an input system (the pointer does two things, and nothing
is delivered to a window's contents), and not a GPU path. Each of those is a **0** in the capability
table beside a phrase naming what it would need, and `desktop/README.md` lists them in one place.
