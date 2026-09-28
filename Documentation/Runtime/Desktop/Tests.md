# RoboDesktop Test Organisation (Phase 11.13)

Seven suite files, **46 cases**, one shared fixture header. The suites live in `desktop/tests` for the
reason the render suites live in `render/tests`: a desktop fixture **is** a render fixture plus a shell,
so the fixtures belong beside the layer they compose rather than in `Tests/Suites`, and
`RoboDesktopTestSupport.h` builds on `RoboRenderTestSupport.h` rather than restating it.

```bash
# from runtime/
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Debug -Verify -Test
./Out/RoboTest.host.exe --filter desktop.
```

`--filter` is a substring match; no case outside this module contains `desktop.`, so the filter reports
these 46 and nothing else.

---

## 1. Suite map

### `SuiteRoboDesktop.c` — the module, the record, the dump (9 cases)

The assertions a *release* makes about itself, split from the picture suites for the reason RoboRender
splits its own: those ask "is the picture right?", and these ask "is this the build it says it is?".

| Case | What it pins |
|---|---|
| `desktop.version_and_capabilities` | version string and fields against the macros; `shell_is_implemented() == 1`; `document_is_implemented() == 1`; `API_FROZEN == 0`; every NULL argument of the version door refused |
| `desktop.conformance.capabilities` | the capability table read **by name**, all 18 fields, the eight presences and the ten absences — a phase that flips one has to edit this case deliberately; `capabilities_init` zeroes everything |
| `desktop.conformance.vocabulary` | the six area names and the two state names, `unknown` for undefined values, and the two count constants |
| `desktop.conformance.diagnostics` | `diag_reset`/`diag_set`/`diag_record`, the domain round-trip, that a diagnostic is cleared rather than accumulated, and that a failed `create` reports `SHELL`/`INVALID_ARGUMENT` with a message and leaves the out-pointer NULL |
| `desktop.config_defaults` | the default configuration: 1280×800, no native window, nothing owned by a caller (title, labels, theme, metrics, face all NULL) and a positive label size |
| `desktop.lifecycle` | `is_live`, the attached-window door, the default status text, the computed geometry at 1280×800, and the zeroed statistics of a fresh shell |
| `desktop.lifecycle.refusals` | nine refusals from one `create` (NULL config, NULL out, zero and oversized dimensions, a panel that leaves no screen, an invalid theme/metrics record, an over-long label) and that a refused create leaves the out-pointer NULL |
| `desktop.frame_record_rules` | `frame_record_init`, `is_valid`'s closed rule (a zero or oversized dimension, `windows > windows_total`, `content_pixels` above the screen, a `bytes` that disagrees with the size are each refused), and `frame_record_equal` field by field including NULL handling |
| `desktop.dump` | the **seven lines exactly**, the line bound, a short buffer reporting the length it needed, and the no-address rule |

### `SuiteRoboDesktopGeometry.c` — arithmetic (7 cases)

Every case is a size and a rectangle: no display, no window, no frame, no document. The numbers are
stated rather than derived from the constants on purpose — a case that computed its expectation from the
metrics would pass with the metrics wrong, so `metrics` pins the metrics and the rest pin what follows
from them.

| Case | What it pins |
|---|---|
| `desktop.geometry.metrics` | the panel and window metric records (30-pixel panel, 24-pixel title bar, the two computed minima), the theme's validity, and the refusals a metrics record earns (no height, a negative inset, a close button taller than its bar, a zero minimum) |
| `desktop.geometry.compute` | the four rectangles of a 1280×800 screen, stated as numbers: screen, panel, work area, and the work area starting exactly where the panel ends |
| `desktop.geometry.small_screen` | a 64×40 screen producing a **thin** work area (64×10) rather than a negative one, collapsed identity/status bands instead of inverted ones, and the refusals (zero size, negative size, a panel as tall as the screen, NULL arguments, an oversized dimension) |
| `desktop.geometry.panel_areas` | `panel_area`'s three alignments (left at the mark, centred, right-aligned), the zero-width case that leaves the area untouched, a text wider than its area clamping to it, and an unknown area being refused |
| `desktop.geometry.place` | centred placement with the **upward nudge** (`y == panel + (work - height) / 3`, asserted as arithmetic), a frame larger than the work area answering with the work area's origin rather than an error, and the refusals |
| `desktop.geometry.contain` | all three repairs: an inside frame unchanged, a frame past the edges moved, a frame larger than the work area shrunk |
| `desktop.geometry.follows_screen` | that a created desktop's geometry and work area are its screen's, and that a `resize` recomputes both rather than leaving the old layout in place |

### `SuiteRoboDesktopWindow.c` — the model (9 cases)

| Case | What it pins |
|---|---|
| `desktop.window.add.defaults` | the description record's defaults, its validation, and that the default "place me" window lands where the placement arithmetic says (including the derived client rectangle rather than a restated one) |
| `desktop.window.add.minimum_and_bounds` | a 4×4 request **grown** to the decoration minimum; a 4000×4000 request shrunk into the work area; a request past the right edge moved inside; `set_bounds` refusals and the `NOT_FOUND` answers for an unknown id |
| `desktop.window.no_work_area` | the `RoboDesktopMinDimension` rule from **both sides**: a 64×10 work area refuses with a `SHELL` diagnostic and leaves the list empty and the id untouched, and a 64×16 work area — the threshold itself — accepts |
| `desktop.window.capacity` | eight windows accepted, the ninth `RoboAlreadyExists`, every slot addressable by index, and an out-of-range index `NOT_FOUND` |
| `desktop.window.title_and_content` | title copy and clear, an over-long title **refused while leaving the old one in place**, the borrowed content door (NULL means none), and every NULL/unknown-id answer |
| `desktop.window.raise_and_focus` | the list **is** the z-order: add order, `raise` moving an entry to the front and taking focus with it, `focus` moving the accent without touching the order, and `NOT_FOUND` for an unknown id |
| `desktop.window.close_and_destroy` | `request_close` moving a window to `CLOSING`, hiding it while keeping it in the list, being idempotent, and `destroy` removing it and handing focus to the front remaining window |
| `desktop.window.hit_test` | all six areas: outside the screen, the panel, the background, a window's title bar, its close button, and its client area — with the close button **inside** the bar it sits in, which is the ordering the resolver gets wrong if it asks the bar first |
| `desktop.window.pointer_actions` | a move recording the point and what is under it, a press raising and focusing, the `pressed` flag following the caller's own state, and a press on a close button producing `CLOSING` |

### `SuiteRoboDesktopFrame.c` — the picture (8 cases)

| Case | What it pins |
|---|---|
| `desktop.frame.sequence` | the triple and its refusals (paint without a begin, a nested begin, a double paint, a double end), a windowless desktop's own record (`fills == 6`, `windows == 0`), the embedded checksum agreeing with the module's own door, and the **shorthand being equal to the sequence it abbreviates** |
| `desktop.frame.shell_pixels` | the shell's pixels through RoboGraphics' own read: the desktop surface, the panel, the hairline, the two falloff rows (as the graphics module's own blend rather than a hand-computed constant), the identity mark at the mark's column, and a **restyled** desktop producing the restyled colours |
| `desktop.frame.window_pixels` | a window's frame on screen: the border at its edges, the title bar's band, the body, the **focused** border in the accent, and the close button's fill and cross |
| `desktop.frame.window_content` | real markup in a window: `content_runs == 1`, `content_pixels == client.width * client.height`, the document's ink landing inside the client rectangle, the statistics following the record, and clearing the content turning runs back to 0 |
| `desktop.frame.labels` | the font pair in both directions: a desktop with no face draws **no** labels and counts them as unshaped, and one with a face draws the identity, applications, status and a window title; also that a hidden window's title is not drawn |
| `desktop.frame.determinism` | **two independently created desktops**, each with a window whose contents came from the same markup, rendered and compared byte for byte, with the records and checksums compared beside them |
| `desktop.frame.resize` | a resize changing the picture's checksum, the screen following the window's clamping, the statistics counting the resize, and a **no-op resize** neither reallocating nor counting |
| `desktop.frame.no_window_state` | the headless desktop's contract: `present` is `NOT_SUPPORTED` while a frame is still real, `present_to_surface` with a NULL destination is refused, and every frame door answers NULL/zero safely |

### `SuiteRoboDesktopDocument.c` — the bridge (5 cases)

The bridge tested as a bridge: markup and an author sheet in, a display list out. Nothing here needs a
desktop, which is the property the whole design rests on.

| Case | What it pins |
|---|---|
| `desktop.document.build_report` | the config defaults, the five report counts climbing stage by stage for real markup and a real sheet, and the display list being non-empty |
| `desktop.document.build_refusals` | NULL arguments, missing markup, a non-positive viewport, the `DOCUMENT` domain on each, and that the out-pointer stays NULL |
| `desktop.document.without_style` | markup with **no** author sheet still producing boxes and commands — "I forgot the CSS" reads as a report rather than as a build error |
| `desktop.document.with_face` | the same document built with and without a face, comparing the two reports: the text is measured and drawn in one, and counted as absent in the other, with no failure either way |
| `desktop.document.destroy_releases` | the live-block count returning to its baseline across repeated builds, run **twice** and compared end to end, so the case measures the module's ownership rather than what the harness had already allocated |

### `SuiteRoboDesktopPresent.c` — where a frame goes (3 cases)

| Case | What it pins |
|---|---|
| `desktop.present.to_surface` | a frame into a destination surface at an offset with a complete record, the destination's own pixels asserted through `GfxSurfaceGetPixel`, and a destination that clips reporting `presented_pixels + clipped_pixels == the frame` |
| `desktop.present.needs_a_window` | a desktop with no native window: `present` is `NOT_SUPPORTED` with `presented == 0` while `render` still works and the frame surface is real |
| `desktop.present.end_to_end` | **the capstone**: markup through the whole chain into a window's contents, the shell's frame composited under it, presented into a destination surface, with `complete == 1`, the presented rectangle equal to the screen, and pixels read back at two named coordinates — the frame's own pixel and a pixel inside the client area — asserted equal to what the frame holds |

### `SuiteRoboDesktopScene.c` — the checkpoint fixture (5 cases)

The scene is what both launchers drive (`RoboDesktopScene.h`), so these cases are about the two things that
makes it worth having: it is *one* definition, and it is *deterministic*.

| Case | What it pins |
|---|---|
| `desktop.scene.open` | the config defaults (1280×800, no face, asks for its own window), then a 640×480 window-only scene: **two** application windows (System Overview + Input Lab), Input Lab focused, both inside the work area, documents built for each client size, one painted frame with nothing clipped and no presentation claimed — and every NULL argument answered rather than assumed |
| `desktop.scene.words` | the markup and the sheet the scene reports are the document it builds (the length is the same object's), every class the markup names is styled by the sheet beside it — the drift check a picture cannot show — and `markup_length()` > 64 |
| `desktop.scene.step` | the iteration: a step with nothing to do paints nothing, an invalidate makes the next one paint, a resize is followed by a rebuilt document, and two steps of one state produce one frame record |
| `desktop.scene.system_face` | the **shipped** face is read (the system typeface pack's `System/Fonts/UI/Inter.ttf`), parsed through cmap → metrics → outlines, covers every character the shell's labels are made of, and then the two counts that are the text verdict: with the face `labels=5 unshaped=0` (identity, applications, status, two titles); without it `labels=0` and every label counted, while the geometry still paints |
| `desktop.scene.lifetime` | three open/close round-trips return every block they took; with the document layers down, opening a scene is refused with a reason and leaves no scene behind; the layers come back and the same scene paints again. Registered **last** in this suite, because it restarts modules |

### `SuiteRoboDesktopTypography.c` — the shell speaks in roles (5 cases)

What a role *means* is `Tests/Typography`'s subject. These five cases are the other half: that the
**desktop** actually asks for roles. They live here because they need the shell, and every one of them
builds its desktop with **no** `face` at all — so a chrome surface still reading the legacy field would
draw nothing and every case would fail on the unshaped count.

| Case | What it pins |
|---|---|
| `desktop.typography.chrome_roles` | a desktop with no single face draws **four** labels and counts none unshaped; the ink is on the panel; and two renders of one state are byte-identical — a role binding must not cost determinism |
| `desktop.typography.roles_change_picture` | the binding is not a no-op: the same family, the same window, the same titles through the role path and through the legacy single face, four labels each and **different** checksums, because one drew 13 px semibold titles and the other 11 px regular ones |
| `desktop.typography.document_defaults` | an `h1` with no author type rule is display-sized: the same markup and face with and without the resolver's generated sheet, measured as ink — the system's heading puts down more than three times the layout engine's initial 16 px text |
| `desktop.typography.document_face` | precedence: the resolver's `DocumentBody` face wins when a resolver is configured, the caller's `face` is used when it is not, and neither means a **textless document that is still a document** rather than an error |
| `desktop.typography.missing_family_fallback` | a pack that declares only the UI Text role leaves the panel identity (`DesktopTitle`, a display role) with no family at all — and the shell still draws it, through the caller's face, with zero unshaped |

### `SuiteRoboDesktopTypographyGallery.c` — three deterministic pictures (3 cases)

The galleries are the visual half of the phase, and each writes a PNG. Three things are asserted beside
the picture, which is what keeps a screenshot test from being decoration: **two renders of one state are
one frame** (`DesktopFrameRecordEqual` *and* the checksum), **the scale is the authority** (a second
resolver over the same pack with a role resized — or several — must produce a *different* checksum, which
is the check that catches a surface still reading a size from anywhere but the scale), and **the type
ladder reaches the pixels** (the lowest inked row).

| Case | Evidence | What it pins |
|---|---|---|
| `desktop.typography.gallery.desktop` | `Build/Typography-Desktop.png` | the shell's own panel with no window at all: `DesktopTitle`, `ChromeLabel` and `ChromeStatus` are the three labels, the ink is on the panel, and resizing both labels 13/12 px → 20 px changes the frame |
| `desktop.typography.gallery.chrome` | `Build/Typography-Chrome.png` | three windows, so the title band is on screen at three titles: six labels (three titles plus the panel's three), three windows, and a `WindowTitle` resized to 20 px changing the frame at the same label count |
| `desktop.typography.gallery.document` | `Build/Typography-Document.png` | the specimen page — `h1`/`h2`/`h3`, the alphabet, digits, punctuation, a caption, a code line and the four weight names — built with an author sheet that carries **no** `font-size` at all. Flattening the three headings to the body's size shortens the page, and removing the generated sheet shortens it further, so the ladder is measured end to end and the same face is reported for measurement and paint |

The four weight lines render alike, and that is correct rather than a defect: the packed family is a
**variable** face, the selector resolves the requested weight and records it in `RoboTypographyStyle.weight`,
and applying the `wght` axis to an instance is Phase 12.4's job. The lines are in the picture so that the
day the axis *is* applied, the change is visible in one place.

---

## 2. Fixtures (`RoboDesktopTestSupport.h`)

| Helper | What it gives a case |
|---|---|
| `desktop_fixture()` | every layer brought up once, through `render_fixture()` rather than beside it — a desktop fixture *is* a render fixture plus a shell |
| `desktop_open(w, h)` / `desktop_open_with_face(w, h, face)` / `desktop_close` | an owned desktop with no native window, so a case never needs a display session |
| `desktop_add_window(...)` | a window asserted to have been added |
| `desktop_theme()` / `desktop_panel_metrics()` / `desktop_window_metrics()` | the build's defaults by value, so pixel cases assert "the panel is the panel colour" rather than a hex a restyle would break |
| `desktop_pixel`, `desktop_pixel_is`, `desktop_pixel_is_not`, `desktop_region_has_ink`, `desktop_region_has_ink_is`, `desktop_region_is_clear` | pixel reads through `DesktopFrameSurface` and RoboGraphics' own read — this module has **no pixel door of its own**, because reading a pixel is what the graphics layer is for |
| `desktop_set_window_content(...)` | a **real** application document for a window, at the client size the *shell* computed, returned so the caller can release it after the window is destroyed — the lifetime `set_content` states |
| `desktop_write_png(...)` / `desktop_checkpoint_path()` | the visual checkpoint: `Out/Desktop.png` unless `ROBO_DESKTOP_PNG` names somewhere else |
| `desktop_live_blocks()` | `MemDebugLiveCount()`, the number a leak case asserts returns to its baseline |

Every helper **asserts** rather than returning a status, the convention the layout, paint and render
suites set: a suite that had to check each step of its own scaffolding would drown its subject in harness.

---

## 3. The visual checkpoint

`desktop.present.end_to_end` writes `Out/Desktop.png` — the shell with a panel, an identity mark, a
window with a title bar, a close button and a real document inside it. The write is deliberately **not an
assertion**, for the reason the render fixture's checkpoint gives: a checkpoint is evidence for a person
to look at, and a suite whose verdict depended on the filesystem would fail on a machine with a read-only
tree while the pixels it verified were perfectly good. The verdict in that case is the pixels it asserts
directly, and the PNG is what a reader opens when a pixel assertion fails.

---

## 4. What the suites assert, and what they do not

**They assert:** geometry as numbers at several sizes; the six hit areas; the record's closed validity
rule; the dump's exact text; that the shorthand equals the sequence; that two independent desktops agree
byte for byte; the theme's colours at named pixels; the report of a real document build; the pixel-level
end-to-end presentation; and that a build/destroy cycle returns the allocator to its baseline.

**They do not assert:** anything about a native backend (a suite that created a window would need a
display session to check a copy — the QEMU display module's own markers verify the backend where it
lives), anything about pacing or frame rate (there is no clock to assert against), and anything about
"the picture looks right" beyond the named pixels — a golden image would make the suite's verdict a file
rather than a claim, which is the opposite of what the pixel cases are for.

---

## 5. What this phase's suites found

The findings worth keeping are the ones that only a *test* could have produced:

1. **A dead constant, and a comment describing a rule the code did not implement.** A build's
   documentation can be wrong in exactly the way a wrong answer is: `RoboDesktopMinDimension` was read
   by nothing, and `RoboDesktopGeometry.h` claimed a screen below it "leaves an empty work area" while
   `compute` — deliberately total — returns whatever the panel leaves. Writing the geometry document is
   what surfaced both: the constant now decides the one door where "fits" is a question
   (`DesktopWindowAdd`), the comment states what the code guarantees, and
   `desktop.window.no_work_area` pins the rule from the failing *and* the passing side, because a rule
   asserted only where it refuses is a rule that could be a different rule.
2. **A capability table's own prose can drift from its fields.** The suite comment called the table
   sixteen fields and the absences nine, while the struct had eighteen and ten. The counts in prose are
   not the assertion (the case reads every field by name, which is the assertion) — but a reader trusts
   the prose first, which is why the numbers are computed rather than remembered here.
3. **The minimum a window may be is computed, not literal.** `metrics.min_width`/`min_height` are derived
   from the decoration fields, so a restyle gets a consistent minimum for free — and the case asserts the
   *arithmetic* rather than 10 and 34, which is what makes a restyle safe rather than a test to update.
4. **The two host window cases are environment-dependent.** `window.host.keyboard`/`mouse` need the
   process to be granted foreground focus (`SetForegroundWindow`), so a full run reports all **46**
   desktop cases passing either way while those two (both in `window.host.*`, not this module's) pass or
   fail depending on whether the developer's desktop was in front. It is pre-existing, it is not this
   module's, and it is recorded here so a future run's number is not misread as a regression.

5. **The scene suite needs the parsed font doors named.** `FontFaceGlyphCount`,
   `FontFaceLayoutMetrics` and `FontFaceGlyphOutline` all answer `RoboNotSupported`
   until the part of the font they need has been built, so `desktop.scene.system_face` parses cmap,
   metrics and outlines in that order before asking. The shell's own chrome never has to: it looks glyphs
   up *after* the document's text pass has prepared both of the face's tables — cmap and metrics, at the
   top of the pass (`RoboPaintDecoration.c`) — which is why a face is usable by the time the first label
   is drawn.
