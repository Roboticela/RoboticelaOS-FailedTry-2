# RoboRender

Rasterization subsystem for Roboticela Runtime (**Phase 11.12 — the first release that ends in
pixels**, extended by **Phase 11.13 — presentation**, which gives those pixels a destination).

**Version:** 0.2.0 — `ROBO_RENDER_API_FROZEN=0`

`ROBO_RENDER_RASTERIZATION_IMPLEMENTED=1` since this module's first release: a **display list
executed into an owned pixel target**, in the order the list states, bounded by its clip scopes and
at the strengths its opacity scopes establish, with an execution record a caller can read and output
that is byte-identical for identical input. `ROBO_RENDER_PRESENTATION_IMPLEMENTED=1` since 11.13: a
frame **composited into a destination surface** — a window's render surface, a display framebuffer, an
offscreen surface a caller owns — at an offset, with a record whose two pixel counts sum to the
frame's own. The claim is **narrow**, and the fields beside it say how narrow: `rasterizer`,
`pixel_targets`, `command_execution`, `region_runs`, `clipping`, `opacity`, `text`, `images`,
`determinism`, `frame_records` and `presentation` are each **1**, while `compositor`, `tiles`,
`damage_tracking`, `frame_scheduling` and `gpu` are each **0** and name the phase that would make them
a 1. Each flag is reported by a function that reads the constant, and the conformance suite asserts
the constant, the function and the capability field together.

RoboRender is the sixth module in the layout stack: it sits directly above RoboPaint and directly
below the window/display path (`DEPENDENCIES.md`). RoboPaint answers **which work, in what order,
under which boundary and at which strength**; RoboPaint also fills a box's decorations into a surface
when it is walked; RoboRender is what makes a **frame** out of that walk — an owned target, a run over
the whole vocabulary, a record of what the run did, and the ability to re-execute only the pixels a
paint invalidation set says went stale.

```text
HTML / DOM → RoboCSS → RoboLayout → RoboPaint → display list → RoboRender target → present → window / display / framebuffer
```

11.12 stopped at the target: a frame is pixels in a buffer this module owns. 11.13 gave that buffer an
exit — `robo_render_present` composites one frame into one destination surface at one offset, which is
the *door* 11.12's release note named, taken (`render/docs/Presentation.md`). Where a frame should go,
and when, is still the caller's: a window's render surface and a display framebuffer are both just
surfaces to this module, and which one a caller hands over is the reason it reaches into neither.

## The first structural decision: a target is owned, and it is the module's only allocation

`robo_render_target_create` makes a write-capable **RGBA8** surface and remembers its facts (width,
height, format, stride, byte size); `robo_render_target_surface` lends that surface out for the three
things this module deliberately does not do — reading a pixel, encoding a PNG, and presenting. A
target is created here rather than accepted as a bare `robo_gfx_surface_t` because a door that took
one could be handed a read-only surface, a surface of another format, or a window's surface about to
be freed. The format is stated once (`ROBO_RENDER_TARGET_FORMAT`) rather than negotiated: a
determinism claim is a claim about **bytes**, and a claim about bytes is only worth making about a
layout a caller can predict. RoboGraphics already packs and unpacks five formats, so a target in a
window's layout is a boundary to cross rather than a gap to fill — and crossing it belongs to the
phase that has the window.

A fresh target is **zero-filled** (transparent black in every pixel), which is the one initial state a
caller can predict without knowing anything about an allocator. `ROBO_RENDER_MAX_DIMENSION` (16384)
is checked before anything is allocated, so the stride, the byte size and every pixel offset a door
computes for an accepted target fit in `size_t` on a 64-bit target.

## The second structural decision: one run, not a session

There is no `begin`/`draw`/`end` and there never was: `robo_render_rasterize` executes the whole list,
`robo_render_rasterize_regions` executes the same list bounded by a set's regions, and both are one
call because there is **no state to hold between calls**. Everything a drawing needs — its rectangle,
the boundary in force, the strength in force, its colour or its characters or its resource — is
carried *by the command it belongs to*, which is what 11.3's display list was designed for. A
rasteriser with a real state stack is a rasteriser with transforms and a transform stack, and both
are named as absent rather than half-built. A one-shot call is also what makes "two runs over the
same list produce the same bytes" a statement about the **inputs** rather than about a session.

## The third structural decision: the bound is a *clip*, not a *filter*

A region run executes **every** command of the list and narrows the raster's own clip to the region.
It does not skip the commands whose rectangle misses the region, and the difference is a wrong pixel:
a text run's rectangle is its **advance** box, so a glyph's ink can land inside a region its rectangle
never reaches. Bounding with the clip needs no per-kind proof of that kind, and it is what makes a
region run equal to a full frame *inside* the region — which `render.scene.region_matches_full`
asserts byte by byte rather than argues.

## The fourth structural decision: one run per set, over the set's own bounds

A paint invalidation set can name **overlapping** regions, and drawing the list once per region would
composite a semi-transparent drawing **twice** where two regions overlap — two half-transparent fills
over one pixel do not make one fill, and no counter would catch it. So a region run executes **once**,
over `robo_paint_invalidation_set_bounds`: 11.11 documents that rectangle as *sufficient* (it can only
be too large), and a repaint over a superset of the damage is right for the same reason a repaint of
the whole frame is right, at a fraction of the pixels. Merging the regions into disjoint rectangles and
running once per rectangle needs the region tree 11.11 deliberately did not build; it is named as the
optimisation it is rather than implied.

An **empty or NULL** set is not an error: `regions == 0`, `limited == 1`, an empty region,
`region_pixels == 0`, `pass.commands == 0`, and the target's bytes untouched. That is 11.11's own
answer to "the picture is current", and a caller should not have to read a diagnostic to tell
"nothing was owed" from "something went wrong".

## The fifth structural decision: all-or-nothing, and therefore a preflight

Before the first pixel, a run walks the list **once**, counting scope depth and checking that every
command is a record this build executes. A list that fails — a command kind this build does not know,
a `pop` that closes a scope nothing opened, a scope left open at the end — is **refused** with a
diagnostic and the target is left exactly as it was found. Two things follow, and both are
load-bearing:

- an unsupported command is never **silently reinterpreted**, which is the brief's §5 stated as
  behaviour: half a picture is not a picture;
- a run that returns `ROBO_OK` **drew the whole list it was given** — which is what makes the region
  equality claim checkable at all, because a bounded pass and a full pass differ in the clip and in
  nothing else.

The preflight is one pass over values and allocates nothing. The equivalence it does *not* attempt is
proving a list is a well-formed **document** (a `content` marker under the wrong box, a scope wider
than the boundary outside it); those are the display list builder's refusals, where the tree is still
available. A rasteriser that re-derived them would be a second authority on what a list may contain.

## The sixth structural decision: clearing is the caller's

A run **does not clear the target**. `robo_render_target_clear` does, explicitly, and the two are
separate because a region run **must not** clear: clearing would erase the pixels the regions were
computed to preserve. So the frame contract is: clear once if the frame is new, then execute the list
wholly or by regions. Conflating the two would make the correct partial repaint unexpressible.

## The seventh structural decision: the walk is not duplicated

RoboRender owns what a **frame** is; RoboPaint owns what the list **means**. So 11.12 added one door
*below* this module — `robo_paint_decoration_paint_bounded`, the same walk with the raster's clip
narrowed by a rectangle — and everything else lives here. Two implementations of one rule agree until
a document makes them disagree, and there is no test that can tell you which one is right; the
decoration pass's buckets (`filled`, `empty`, `clipped`, `no_font`, `unshaped`, `bounded_out`) are
therefore this module's evidence too, embedded in `robo_render_frame_t::pass` by value rather than
copied field by field.

## The eighth structural decision: determinism stated as bytes, and a record that can be compared

`robo_render_target_bytes` hands back the whole contiguous buffer, `robo_render_target_checksum` is
an FNV-1a over it, and `robo_render_target_equal` compares two targets field by field. Nothing in the
pipeline reads a clock, an address, an allocation order or a hash iteration order, and the execution
record is a struct of **counts and rectangles** so two runs can be compared without a picture. The
frame dump (`robo_render_frame_write`, seven lines) contains **no address** — asserted rather than
promised.

## Geometry is borrowed, not defined

There are no new coordinate types, and no rounding is left to perform here: every rectangle that
reaches this module was resolved to integers by RoboLayout and RoboPaint, and the one continuous
quantity in the pipeline — a text run's pen, accumulated in `float` — is snapped one layer down by
RoboGraphics' own `robo_gfx_glyph_snap_point` policy. Paint coordinates *are* device pixels in this
build: origin top-left, `x` right, `y` down, a rectangle half-open, and the target's pixel `(0, 0)` is
paint coordinate `(0, 0)`. A device-pixel-ratio or a zoom would break that identity, which is why both
are named as absent rather than assumed to be 1 forever.

## Public API

`Include/RoboRender/RoboRender.h` is the umbrella; the headers it includes are the surface:

| Header | Surface |
|---|---|
| `RoboRenderVersion.h` | Version macros, `ROBO_RENDER_API_FROZEN`, `ROBO_RENDER_RASTERIZATION_IMPLEMENTED`, and the coordinate statement |
| `RoboRenderDiagnostics.h` | `robo_render_diag_t`, `robo_render_diagnostic_domain_t` (5, `NONE` included), the `ROBO_RENDER_DIAG` macro and the two domain doors |
| `RoboRenderRasterizer.h` | `robo_render_target_t` and its fifteen doors, `robo_render_frame_t` and its three, `robo_render_rasterize`, `robo_render_rasterize_regions`, `robo_render_frame_write` |
| `RoboRenderPresenter.h` | `robo_render_present_record_t` and its four doors, `robo_render_present_region`, `robo_render_present`, `robo_render_present_framebuffer`, `robo_render_present_write`, `robo_render_presentation_is_implemented` |
| `RoboRender.h` | `robo_render_capabilities_t` (16 flags), `robo_render_get_capabilities`, `robo_render_version_string`, `robo_render_get_version`, `robo_render_rasterization_is_implemented` |

## Lifecycle

**There is none, deliberately.** RoboRender holds no global state, so it has no `init`/`shutdown` pair
and no `is_initialized` — the same argument RoboGraphics makes for itself. A **target** is created and
destroyed by its caller, a **frame record** is a value by value, and a **run** borrows the list, the
target and the face for the length of one call and retains nothing: the display list carries no
pointer, the record keeps no reference, and the face is the caller's (which is why a textless document
never needs one — `face` may be NULL, and then every text command is counted in `pass.no_font` and
skipped).

## Documentation

| Document | Contents |
|---|---|
| `render/docs/Rasterization.md` | The target and its pixel buffer, the coordinate statement, the walk and how the boundary and the strength reach the raster, text and images, the region path, the error model, and **what is absent** — each of the six 0s with what it would need |
| `render/docs/Presentation.md` | The presenter: why the destination is a surface and not a window, the three doors, the clip-not-refuse rule and the record's exhaustive counts, one authority for where pixels land, the four-line dump, and the five 0s beside `presentation` |
| `render/docs/Tests.md` | Suite map (three suite files, 31 cases), fixtures, what the suites assert and do not, running, sanitizers, and what the phases' suites found |

## Building and testing

```bash
# from runtime/
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Debug -Verify -Test
./Out/RoboTest.host.exe --filter render.
```

The filter is a substring match, so it reports this module's **31** cases and nothing else: no other
module's case name contains `render.`.

## Boundaries

RoboRender must not depend on RoboWindow, RoboDisplay, RoboHTML, RoboCSS or RoboLayout: a display list
is its input contract, and the boundary scan in `Tools/Build.ps1` (`Assert-RoboRenderBoundaries`)
refuses a higher-layer or document-layer name in this module's headers or sources. It reads RoboPaint
(a display list, an invalidation set, the bounded decoration walk), RoboGraphics (a clear, a byte view,
a surface, the checksum's bytes) and RoboFont (a face it is *handed*).

- **Executing a list, and presenting one frame.** `rasterizer`, `pixel_targets`,
  `command_execution`, `region_runs`, `clipping`, `opacity`, `text`, `images`, `determinism`,
  `frame_records` and `presentation` are 1. `presentation` is a **call and not a system**: one frame
  into one destination at one offset, with the destination always the caller's. The row that would
  describe a presentation *system* is `compositor`, still 0 beside it.
- **One list into one target.** `compositor` is 0 because a compositor is a *set* of frames with an
  ordering and a place to put the result; there is no layer tree, no stacking context and no z-index
  in this build.
- **No tiles and no damage history.** `tiles` needs a target divided into independently rasterisable
  pieces and a decision about which a change touched — which needs the region *subtraction* 11.11
  refused. `damage_tracking` needs a history of what a scene was, and 11.11's set is the
  *per-transition* answer with no lifetime.
- **No clock, no deadline, no queue.** `frame_scheduling` is 0, and this release's determinism depends
  on its absence: there is nothing here that could depend on *when* it ran. VSync, frame pacing and
  animation are not stubbed, they are named.
- **No GPU.** `gpu` is 0: a device, a command buffer, a resource manager and a fallback path make a
  *module*, not a branch of this one.
- **No transforms, filters, masks, blend modes or `border-radius`** — not as capability rows here,
  because they are properties the display list would have to carry and the graphics raster would have
  to implement, and both are named where they belong (`RoboGraphics`' reserved doors, RoboPaint's
  absent records). A row here would suggest this module is where they would arrive, which is the one
  thing that is not true.
- **No text shaping.** A run's glyphs are resolved through a caller's face and drawn through
  RoboGraphics' glyph bridge, one call per character: no GSUB/GPOS, no clusters, no bidi reordering,
  no ligatures, no fallback. That is Phase 14's, and the display list already carries the layout's
  decision about what the run *is*.
- **No image loading, decoding or filtering.** An image payload is a **retained resource** the
  document already resolved (11.7); drawing it is RoboGraphics' own scaled nearest-neighbour sampler.
  Encoding a target to a file is the test fixture's business (`robo_gfx_surface_encode`), not this
  module's.
- **No partial display list.** The list is rebuilt and re-executed whole; the set says how little of
  the *pixels* may be re-executed, not how little of the *list*.
- **Chromium is reference only.** No source in this module names a Chromium or Blink type, and
  `robo check`'s independence scan is clean with this module present.
