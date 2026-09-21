# RoboRender Test Organisation (Phase 11.12, extended by 11.13)

Three suite files, **31 cases**, one shared fixture header (24 from 11.12, seven from 11.13's
presenter). The suites live in `render/tests` for the
reason the paint suites live in `paint/tests`: a render fixture *is* a paint fixture plus a target, so
the fixtures belong beside the layer they consume rather than in `Tests/Suites`, and
`RoboRenderTestSupport.h` builds on `RoboPaintTestSupport.h` rather than restating it.

```bash
# from runtime/
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Debug -Verify -Test
./Out/RoboTest.host.exe --filter render.
```

`--filter` is a substring match; no case outside this module contains `render.`, so the filter reports
these 31 and nothing else.

---

## 1. Suite map

### `SuiteRoboRender.c` — the module, the target, the record, the dump (10 cases)

| Case | What it pins |
|---|---|
| `render.version_and_capabilities` | version string and fields against the macros; `rasterization_is_implemented() == 1`; **the flag and the capability field agree**; every NULL argument of the version door refused |
| `render.conformance.capabilities` | the **16 flag names read positionally, by name, in declaration order**, against a row table whose size is asserted to match the struct's (one `int` per row, no padding — or a row would be checking its neighbour); every field a value and every value a 0 or a 1; the **eleven 1s and five 0s**, with the 0s asserted *by name* so a phase that flips one has to change this case deliberately; `RenderCapabilitiesInit` zeroes everything |
| `render.conformance.vocabulary` | the five diagnostic domain names and their predicates, unknown and negative values reporting `unknown`, and the first and last enumerators |
| `render.conformance.diagnostics` | `RenderDiag`: reset, set, the strict and non-strict paths, the domain round-trip, and that a diagnostic is cleared rather than accumulated |
| `render.target_facts` | every fact door on a real target (320x240: width, height, format, stride, byte size, pixel count, bounds) and the zero answers for NULL |
| `render.target_refusals` | zero dimension, dimension above `RoboRenderMaxDimension`, NULL out-target left untouched, and the diagnostic domain for each |
| `render.target_pixels_and_equality` | the zero-filled fresh target, `clear` **replacing** including the alpha channel, `checksum` moving when a byte moves, and `equal`'s rules (NULL == NULL, NULL != a target, differing sizes unequal) |
| `render.record_rules` | `frame_init`, `is_valid`'s closed rule (a pixel count that disagrees with the size, a stride smaller than a row, a zero dimension are all invalid), `frame_equal` field by field including the embedded pass record, and a hand-built record being refused rather than read |
| `render.dump` | the seven dump lines, the line bound, a too-small buffer reporting the length it needed, and the **no address** rule (`=0x` absent) |
| `render.conformance.presenter` | 11.13's release shape, asserted rather than exercised: `RoboRenderPresentationImplemented` and the function that reads it agree, `presentation` is 1 **while `compositor` is still 0** (a frame reaching a destination is not a layer order), the diagnostic vocabulary did **not** grow, and a cleared presentation record is not a valid one |

### `SuiteRoboRenderRaster.c` — pixels (16 cases)

| Case | What it pins |
|---|---|
| `render.raster.empty_frame` | an empty list is a successful frame: a valid record, all counts zero, and the target's bytes exactly what the clear left |
| `render.raster.argument_rules` | NULL list, NULL target, a NULL `out_frame` accepted, and a NULL `face` on a textless list costing nothing |
| `render.raster.background_pixels` | a filled rectangle's pixels, at the named coordinates of a document whose geometry is read from the display list |
| `render.raster.alpha_and_opacity` | semi-transparent over opaque, transparent over opaque, `opacity: 0`, `opacity: 0.5`, and nested opacity — with the arithmetic a browser's source-over produces, asserted at the pixel |
| `render.raster.clip_and_bounds` | inside the clip, outside it, a partial intersection, nested clips, an empty clip, and the `clipped` bucket |
| `render.raster.offscreen_and_edges` | negative and off-surface rectangles, a rectangle crossing the right and bottom edges, an empty region, and a source rectangle reaching past an image's edge |
| `render.raster.determinism` | the same list into two independently created targets compared **byte for byte**, with the records and the dumps compared beside them |
| `render.raster.preflight_refuses` | a list that closes a scope nothing opened and a list that leaves one open, both refused with a `LIST` diagnostic, the target byte-identical afterwards, the record not overwritten — plus the assertion that the *unknown kind* half of the rule is unreachable through the public API |
| `render.raster.no_font_is_a_number` | a run with no face changes nothing and is counted in `no_font`; with the fixture's face, the two characters resolve through its `cmap`, two glyphs are drawn, and **ink lands inside the run's own box** |
| `render.raster.region_clamping` | a region running off the target is executed at the intersection and the record reports the rectangle it executed; a region that misses the target entirely executes nothing; a region inside it repaints exactly the box |
| `render.present_region` | the arithmetic door on its own: the frame's rectangle moved by the origin and cut to the destination, including a frame hanging off an edge and a **negative** origin; an off-destination frame answers with an empty rectangle rather than an error; a read-only destination is refused |
| `render.present_pixels` | a real document's frame presented into a destination at an offset, read back from the **destination**: a named pixel of the picture at the same offset, the destination's own pixels outside the frame untouched, `presented_pixels + clipped_pixels` summing to the frame's count, and `complete == 1`; then the same frame presented twice, which is presenting twice |
| `render.present_clipping` | a frame larger than its destination and a destination larger than the frame, both **clipped rather than refused**, with the destination's pixels proving which survived and the record's rectangle naming where the pixels went |
| `render.present_framebuffer` | the same presentation through `RenderPresentFramebuffer`: a borrowed wrapper over an owned buffer, the pixels landing in it, and nothing retained after the call |
| `render.present_record_and_dump` | `present_record_init`, field-for-field equality, the **closed validity rule** (a count pair that does not sum to the frame, a format that is not this build's, a `complete` that disagrees with the clipped count, an oversized frame), and the four-line dump with no address in it |
| `render.present_refusals` | every refusal: NULL frame, NULL destination, a read-only destination, an unimplemented destination format (with its `FRAME` diagnostic), and a cleared record as the documented "nothing" state |

### `SuiteRoboRenderScene.c` — a document, end to end (5 cases)

| Case | What it pins |
|---|---|
| `render.scene.checkpoint` | the phase's visual checkpoint: one document with a page, a bordered card, a text run inside it, a 4x4 picture scaled to 32x32, a semi-transparent box and an `overflow: hidden` clip, rasterised into one 200x200 target — the four drawing kinds counted, then the page, the border bands, the card's background, the run's ink, the picture's four quadrants (including the transparent one showing the page), the semi-transparent pixel and both sides of the clip boundary asserted at named pixels; then a PNG of the frame is written for a person to look at |
| `render.scene.region_matches_full` | **the capstone**: two states of one document differing in a colour, a *position* (a margin change, so 11.11 emits geometry regions) and an opacity; the regions 11.11 reports are used to re-run the second state's list into the first state's target, and the whole target is compared **byte for byte** against a full render of the second state |
| `render.scene.no_repaint` | a document compared with itself produces no regions, and a region run of an empty set executes nothing, touches no byte, and says so (`regions == 0`, `region_pixels == 0`, `commands == 0`); a NULL set is the same conclusion |
| `render.scene.snapshot` | the record list, the display list and the tree are **released after a render** and the target's bytes do not move; then the same document is rebuilt from scratch, renders the same bytes, and produces an equal frame record |
| `render.scene.dump` | a real frame's dump rendered twice as text and compared string for string, with no address in either |

---

## 2. Fixtures

`render/tests/RoboRenderTestSupport.h` is not a framework (the runtime has one, in
`Tests/Framework`) and it adds no assertion the paint header already provides; it holds five things:

| Fixture | What it is |
|---|---|
| `render_fixture` | the whole bring-up chain in `DEPENDENCIES.md` order, by calling `paint_fixture` |
| `render_scene_t` | the whole chain in three calls — a document through HTML → CSS → layout → records → **display list** — keeping the records *and* the list alive, because a frame runs against the list and an invalidation set is built from the records |
| targets and pixel reads | `render_page`, `render_pixel`, `render_pixel_is`, `render_targets_equal`, and — since this phase — `render_region_has_ink_is` / `render_region_is_clear` |
| a synthetic face | a real SFNT built out of numbers: `cmap` mapping only `'A'` to glyph 1, glyph 1 a square from (0,0) to (100,100) FUnits, units per em 1000, advance 600 units, `.notdef` empty |
| a picture and its assets | a 4x4 PNG with four distinct quadrants, encoded through `GfxSurfaceEncode`, decoded through RoboHTML's image model, and resolved onto an element **before** layout |

The pixel read goes through `GfxSurfaceGetPixel` on the target's own surface deliberately: this
module has no pixel door of its own, because reading a pixel is what the graphics layer is *for*, and a
second door would be a second conversion of the same bytes.

`render_region_has_ink_is` exists because text needs it. A glyph is an **outline**, so the interior of
a 10x10 square is background and its four one-pixel edges are the run: a case pinned to the middle of
the square would be asserting about the way a font designer draws rather than about whether the frame
contains the run. The region is clamped to the target, and an empty region answers "no ink" — the
honest answer to nothing.

---

## 3. What the suites assert, and what they do not

**They assert** every claim the phase's completion criteria name, at the strongest level each claim
admits: a name for the vocabularies and the flags, a value for the target's facts, a *pixel* for a
drawing, a *bucket* for why a drawing did or did not land, a rectangle for a region run, and **bytes**
for determinism and for the region equality. Where a claim can be checked only statistically
(`RenderTargetChecksum`), the suites use it *beside* the byte comparison rather than instead of
it.

**They do not assert** anything about a picture a human would call correct: there is no golden image,
no reference bitmap and no perceptual comparison, because a golden image in a tree this young is a test
that fails whenever a fixture changes and passes whenever the change is copied into it. The checkpoint
PNG is **evidence** (`Out/RenderScene.png`, or `$ROBO_RENDER_SCENE_PNG`), not a verdict: the verdict is
the pixels asserted beside it, and the PNG is reported rather than required — a writable tree is not a
property of this module, so a checkpoint on a read-only tree prints a note and the suite still passes.

**They do not assert** anything about layout, painting or invalidation: those are other modules'
suites, and a render case that re-derived their results would be a second answer to a question that
already has one. Every document a render case lays out is small enough that its geometry is written
down in the case's own comment.

---

## 4. What the phase found

Five things, each of which is now a rule rather than an incident: the first four came out of the suites
and the fifth out of a gate that had not been run. They are recorded because the same mistakes are
available to anyone writing the next rasteriser test, and because three of the four were *the test*
being wrong about the system rather than the system being wrong.

1. **`position` is parsed by RoboCSS and not laid out by RoboLayout.** The scene's first draft placed
   an "overlay" at `position: absolute; left: 110px; top: 10px` and asserted a pixel there. Every box
   in this build is in **normal flow**: the declaration was accepted and ignored, and the box landed
   where its margins put it — 100 pixels lower than the assertion. The fix is the case's own comment,
   which now states the document's flow geometry as numbers and says why: a later layout phase that
   starts honouring `left`/`top` fails a named assertion here instead of quietly moving the picture.
   (`render.scene.checkpoint`, and the coordinate note in `render/docs/Rasterization.md` §3.)
2. **A glyph is an outline, so a pixel in the middle of it is background.** The no-face case asserted
   that the pixel at (8,8) had changed — the centre of a ten-pixel square drawn at `font-size: 100px`.
   The glyph was drawn (the checksum moved) and the pixel was untouched: the *stroke* is the ink.
   Hence `render_region_has_ink_is`, and the rule that a text claim is a claim about a region.
3. **`0x` appears inside `200x200`.** The dump case's "no address in the dump" check was written as
   `strstr(text, "0x") == NULL`, which passed at 16x8 and failed at 200x200 — an assertion that was
   testing the dimensions string. It is now `=0x`, which is what a printed pointer looks like and what
   the claim is actually about.
4. **A clip display command must carry an *active* boundary to be appendable.** The preflight case
   builds a deliberately unbalanced list by hand, and its first draft was refused by the *list's own*
   append (`RoboInvalidArgument`) rather than reaching the preflight — because 11.8's validity rule
   refuses a scope command whose clip bounds neither axis. The case now states the boundary the record
   must carry, which is also the fact that made the rule checkable: the *list* guarantees each record
   is a command, and the *rasteriser* is what guarantees a sequence of them is a frame.

---

5. **`-Verify` had never been run, and it caught two things.** Every build while this phase was written
used `-Test` (which builds and runs the suites) and `robo check` (which scans all runtime sources for
Chromium), so `Assert-RoboRenderBoundaries` did not run until the whole gate list was executed. It
flagged the module's own `#include "RoboLayout/RoboLayoutGeometry.h"` — a document-layer include in the
layer whose input contract is a display list, fixed by taking the rectangle through RoboPaint's geometry
re-export — and then flagged *this fixture*, which includes RoboHTML because a render fixture is a paint
fixture plus a target and must build a document to have a list to rasterise. The rule was split, the
paint scan's asymmetry one layer down: the module is held to both halves (no document layer, no higher
layer, no Chromium), the suites and documents to the upward half only. The lesson is about gates rather
than about layering: a check that is written but not run is a check that has not been made.

---

## 5. Running

```bash
# from runtime/
./Out/RoboTest.host.exe --filter render.            # this module, 31 cases
./Out/RoboTest.host.exe --filter render.present_    # presentation only, 6 cases
./Out/RoboTest.host.exe --filter render.raster.      # pixels only, 16 cases
./Out/RoboTest.host.exe --filter render.scene.       # the document cases, 5 cases
./Out/RoboTest.host.exe --filter render.scene.checkpoint -VerboseTests
```

The checkpoint writes `Out/RenderScene.png` under the out-of-tree build directory; set
`ROBO_RENDER_SCENE_PNG` to put it somewhere else.

## 6. Sanitizers

```bash
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Debug -Sanitize -Test
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Debug -Verify    # the boundary scan, above
```

The render suites are clean under AddressSanitizer and UndefinedBehaviorSanitizer: the module's only
allocation is the target, its arithmetic is bounded before it is performed, and a refusal leaves the
target exactly as it was found. The region path is exercised with regions that overlap the target's
edge, miss it entirely, and are empty, because those are the three shapes an offset computed by hand
would get wrong.
