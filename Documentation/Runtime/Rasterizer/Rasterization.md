# Rasterization (Phase 11.12, extended by 11.13)

RoboRender turns a **display list** into **pixels in a buffer it owns**, and reports what it did. This
document is the design: the boundary the module keeps, the target and its buffer, the coordinate
statement, the walk and how a drawing's boundary and strength reach the raster, what text and images
amount to here, the region path, the error model, and — last, and longest, because a reader is owed the
list rather than a shrug — **what is absent and what each absence would need**.

**11.13 changed one thing in this document and no other:** the buffer gained an exit. Presentation is a
second direction rather than a second rasteriser, so it has a document of its own
(`render/docs/Presentation.md`) and §12 below now reports `presentation` as **present** rather than as
the next phase. Everything else here — the target, the walk, the region path, the coordinate statement —
describes the release exactly as it shipped, and the version that carries both is **0.2.0**.

Chromium's rasterization stack (`cc::RasterSource`, `PaintOpBuffer`, tile managers, the GPU raster
path) was read as temporary technical reference for what a rasteriser is asked to answer. Nothing in
this module is a translation of it: no type, no file and no decision here is derived from Chromium's
architecture, and the boundary scan refuses even the names.

---

## 1. The boundary, in one paragraph

```text
RoboDisplayList        what to draw, in order, with a boundary and a strength per command
      ↓
RoboRender             THIS MODULE: a frame — an owned target, a run, a record, bytes
      ↓
RoboPixelBuffer        a RoboGraphics surface this module owns, RGBA8
```

Above it: nothing in this module knows what a DOM node, a computed style or a layout box is. A display
command carries the one rectangle it acts on, the boundary in force, the strength in force and — for a
run or an image — its payload, so an executor needs no tree. `Assert-RoboRenderBoundaries` in
`Tools/Build.ps1` enforces that mechanically: a `#include` of `RoboHTML`, `RoboCSS`, `RoboLayout`, a
higher layer or `chromium`/`skia` in this module's headers or sources fails the build.

Below it: **the walk is RoboPaint's**, not this module's. `RenderRasterize` calls
`PaintDecorationPaintBounded`; the command vocabulary, the two scope stacks, the per-drawing
boundary and strength, and the buckets that say why a drawing did not land are all one layer down. What
11.12 added below this module is exactly one door — the *bounded* walk — and the reason is not
tidiness: a second walk in this module would be a second answer to "what does this command do", and
when the two disagreed the rasteriser's answer would be the one a person believed.

---

## 2. The target and its pixel buffer

`RenderTarget` is opaque. Its facts are read through doors, never through fields:

| Fact | Door |
|---|---|
| width, height | `RenderTargetWidth` / `_height` |
| format | `RenderTargetFormat` — always `RoboRenderTargetFormat` |
| stride (bytes per row, padding included) | `RenderTargetStride` |
| total bytes | `RenderTargetByteSize` |
| pixel count | `RenderTargetPixelCount` |
| bounds as a rectangle | `RenderTargetBounds` → `(0, 0, width, height)` |
| the surface (borrowed) | `RenderTargetSurface` / `_surface_const` |
| the bytes (contiguous, read-only) | `RoboRenderTargetBytes` |
| a cheap identity | `RenderTargetChecksum` (FNV-1a, 64-bit) |
| equality | `RenderTargetEqual` |

**One format, stated.** `RoboGfxFormatRgba8`, 8 bits per channel with an alpha channel. Not because
a second format is hard — RoboGraphics implements five — but because determinism is a claim about
bytes, and a claim about bytes is only worth making about a layout a caller can predict. `stride` is
still reported rather than assumed to be `width * 4`: a caller that writes a byte offset is entitled to
the number the surface actually uses.

**Ownership.** The target owns its surface; `RenderTargetDestroy` releases it. A surface borrowed
through `RenderTargetSurface` must not be released by the caller and is valid for as long as the
target is. Nothing in the module retains a target, a list, a face or a set: a run borrows for the length
of one call.

**A fresh target is zero-filled** — transparent black in every pixel, the one initial state a caller can
predict without knowing anything about an allocator. `RenderTargetClear` is the explicit
baseline and it **replaces**, never blends: a clear that composited would make a frame's starting state
depend on what was there before, which is the opposite of what a baseline is for. A zero dimension is
refused (a target with no pixels cannot hold a frame), which is the one place this module's rectangle
rule does not apply — an *empty rectangle* is a document's shape, an empty *target* is a caller's
mistake.

**The largest edge is `RoboRenderMaxDimension` (16384).** A bound rather than an overflow guard
bolted on afterwards: `16384 × 16384 × 4` is 2^30 bytes, so every stride, byte size and pixel offset a
door computes for an accepted target fits comfortably in `size_t` on the 64-bit targets this build has,
and a caller that asks for more is told before an allocation is attempted.

**Clearing is the caller's, and a region run must not clear.** This is the one integration rule a
caller can get wrong, so it is stated in three headers as well as here. The frame contract is: create
(or clear) once for a new frame, then execute the list wholly or by regions. A region run that cleared
would erase the pixels its regions were computed to preserve.

---

## 3. The coordinate statement

Paint coordinates **are** device pixels in this build and the mapping is the identity:

```text
origin        top-left of the page
x             grows right
y             grows down
rectangle     half-open: x inclusive, x + width exclusive  (RoboGraphics' own rule)
target (0,0)  paint coordinate (0,0)
```

There is **no rounding left to perform here**, and that is a property worth stating rather than leaving
to be discovered: every rectangle that reaches this module was resolved to integers by RoboLayout and
RoboPaint, and the one continuous quantity in the pipeline — a text run's pen, accumulated in `float`
as advances are added — is snapped by RoboGraphics' own `GfxGlyphSnapPoint` policy, one layer
down, in the same place the bridge's scaled metrics are computed. All raster writes are bounds-checked
by the graphics raster against the surface and against the clip, so an off-surface rectangle is a
smaller drawing area rather than a memory write.

A device-pixel-ratio or a zoom would break the identity; both are named as absent rather than assumed
to be 1 forever. There is also no scroll offset (nothing scrolls) and no transform (the display list
cannot carry one), which is why a region needs no coordinate mapping on its way in.

---

## 4. The walk

One pass, in the list's order, with **two stacks** — a clip stack and an opacity stack — owned by the
walk and driven by the list's own scope commands:

```text
begin-node            (structural: counted, not drawn)
draw-background       fill the command's rectangle with its colour
draw-border           fill the band the command states
draw-text             resolve the run's code points and draw its glyphs
draw-image            draw the command's resource into its destination
content               (structural)
push-clip / pop-clip  narrow / restore the boundary in force
push-opacity / pop-opacity  multiply / restore the strength in force
end-node              (structural)
```

Per drawing, the effective state is computed, not inherited: the **clip** is the command's own
boundary intersected with every enclosing scope's, and the **strength** is the product of the enclosing
opacity factors with the command's own. That arrangement is what makes nested scopes, a scope with
nothing inside it, a box whose own decoration is drawn *outside* the scope its contents sit in (CSS's
arrangement for `overflow`), and a nested `opacity` all properties of the walk rather than special
cases in a rasteriser.

**There is no `SAVE_STATE`/`RESTORE_STATE` command and there never was** (11.3 declined to add one for
the same reason): there is no graphics state to save, because every drawing writes the boundary and the
strength it is drawn under. A state stack appears when a rasteriser has transforms, and transforms are
absent.

**The clip is a clip, never a filter.** For a region run, the bound is passed down as a narrowed raster
clip, so *every* command is executed and every write is clipped per pixel. Skipping commands whose
rectangle misses the bound is the same picture only until a text run's ink overhangs its advance box.

**Rounding, alpha and colour are the graphics module's.** A colour goes through
`PaintColorToRgba8` (the one-way bridge) and a fill through `GfxRasterFillRect`; the
source-over blend, the per-channel clamping and the effective-alpha arithmetic are RoboGraphics'. There
is exactly one blend mode in this build, and it is not a parameter.

### What a drawing can turn out to be

The pass's record is embedded in the frame record by value, and its buckets are how "the picture is
right" becomes a number a suite can assert:

| Bucket | Meaning |
|---|---|
| `commands` | every command walked; `commands = draws + skipped` exactly, bounded or not |
| `draws` / `skipped` | a drawing attempt / a structural command, which draws nothing by definition |
| `backgrounds` / `borders` / `texts` / `images` | drawings per kind |
| `filled` | fills that reached the raster — the pair `filled` against `draws` is "how many drawings landed" |
| `contextual` / `transparent` / `empty` | `currentColor`, a fully transparent colour, a zero-area destination |
| `no_font` / `no_resource` | a text command with no face / an image payload this build cannot draw |
| `glyphs` / `glyphs_missing` / `unshaped` / `rtl` | glyph draws attempted, code points the face has no glyph for, pattern border bands counted, and runs carrying right-to-left characters |
| `clipped` | drawings whose clip excluded them — a **subset of `draws`**, because the drawing *was* executed |
| `bounded_out` | drawings whose rectangle missed a region run's bound: a statement about **work**, not pixels |
| `clip_scopes` / `clip_depth_max` / `opacity_scopes` / `opacity_depth_max` / `faded` | the two scope pairs and the strength they established |

---

## 5. All-or-nothing, and the preflight

Before the first pixel, the run walks the list **once**, counting scope depth and checking every
command against `PaintDisplayCommandIsValid`:

- a command kind this build does not execute → refused, `RoboNotSupported`, domain `LIST`;
- a `pop-clip`/`pop-opacity` that closes a scope nothing opened → refused;
- a scope left open at the list's end → refused.

A refusal means the target keeps **exactly** the bytes it had, and a returned frame is therefore a
statement that *the whole list was drawn*. That is what lets a suite compare a region run against a
full frame and mean it: a run that could stop early would make "identical inside the region" unprovable
rather than merely untested. The preflight costs one pass over values and allocates nothing.

What it deliberately does **not** attempt is proving a list is a well-formed document — a `content`
marker under the wrong box, a scope wider than the boundary outside it. Those are the display list
builder's refusals, where the tree is still available; re-deriving them from a flat list would be a
second authority on what a list may contain, and the two would disagree about a document before they
disagreed about a pixel.

---

## 6. Text

A `draw-text` command carries the run's code points, its rectangle, its `font-size`, its baseline
offset and its colour. The pass resolves each code point through the **caller's face** (`cmap`), draws
the glyph through RoboGraphics' glyph bridge (which scales the outline by the face's metrics for the
requested size) and advances a pen by the glyph's device advance. The pen is accumulated in `float` and
only the **drawn** origin is snapped, so the glyphs of a line cannot drift apart from the width the line
was justified to; a glyph may sit within a sub-pixel of that width, and the policy is stated rather than
hidden.

- The face is the caller's: this module retains nothing, resolves no `font-family` and falls nothing
  back. It may be **NULL**, and then every text command is counted in `no_font` and skipped — a
  textless document paints identically either way, so a caller that draws no text never needs a font.
- A code point the face has no glyph for is drawn at glyph 0 (`.notdef`) and counted in
  `glyphs_missing`, exactly as the layout measurer counted the same miss one layer down.
- A glyph is drawn as a **stroked outline**, one layer down. This is a fact about pixels that a test
  author needs: the *interior* of a glyph's outline is background. `render.raster.no_font_is_a_number`
  scans a run's box for ink rather than naming a pixel in the middle of a square, and
  `render/docs/Tests.md` §4 records the finding.

Absent, and named rather than implied: shaping (GSUB/GPOS), clusters, ligatures, kerning, bidi
reordering, script-specific shaping, fallback, and any glyph cache. That is Phase 14's, and the display
list already carries layout's decision about what the run *is*.

---

## 7. Images

A `draw-image` command carries an image payload: a **retained resource** the document already resolved
(11.7), its source rectangle, its destination rectangle and its snapshot facts. The pass hands the
resource, the source rectangle and the destination to `GfxRasterDrawImage` — RoboGraphics' own
scaled sampler — under the same clip and the same strength as any fill.

- The destination is the element's **content box**, the rectangle layout gave it.
- A zero-area destination is counted `empty`; a payload this build cannot draw is counted
  `no_resource`; neither fails the pass, because both are statements about the document.
- Sampling is nearest-neighbour (the graphics module's rule), so the mapping is deterministic. Scaling
  is therefore *supported* for every payload kind layout can place, and there is no filtering, no
  `object-fit`, no `srcset`, no animation and no fetching. Layout's larger limitation is one layer up
  and stated in `paint/docs/Images.md`: replaced elements are not intrinsically sized in this build, so
  a document must size its images in CSS, and an unsized image produces a zero-area destination the
  pass counts.

---

## 8. The region path

```text
RoboPaintInvalidationSet  (11.11: which rectangles went stale, and why)
        ↓  PaintInvalidationSetBounds
      one rectangle (sufficient — it can only be too large)
        ↓  PaintDecorationPaintBounded
      the same walk, with the raster's clip narrowed
        ↓
      only those pixels re-executed; every other byte is the previous frame's
```

Four decisions, each of which has a wrong version that looks right:

1. **One run over the set's bounds, not one run per region.** Regions may overlap, and a second pass
   over an overlap would composite a semi-transparent drawing twice. Two half-transparent fills over
   one pixel do not make one fill.
2. **The union is used, not the regions.** `PaintInvalidationSetBounds` is documented by 11.11
   as sufficient; a repaint over a superset of the damage is correct for the same reason a repaint of
   the whole frame is. The tighter answer needs the region tree 11.11 declined to build.
3. **The bound is a clip.** Every command is still executed; only the writes are bounded.
4. **An empty or NULL set is success with nothing done.** `regions == 0`, `limited == 1`, an empty
   `region`, `region_pixels == 0`, `pass.commands == 0`, bytes untouched. Not an error: that is 11.11's
   own answer to "the picture is current".

The frame record says which of the three runs happened, from two fields rather than from a
diagnostic: `regions` against `limited` (a full frame, a region run, or a run that was asked for
nothing), and `region_pixels` against `target_pixels` (how much of the frame was re-executed).
`region` is the rectangle actually executed, **clamped to the target** — so "did my region reach the
surface?" is answerable from the record alone.

---

## 9. The error model

| Situation | Answer |
|---|---|
| NULL list or NULL target | `RoboInvalidArgument` |
| a command kind this build does not execute | `RoboNotSupported`, domain `LIST` |
| a scope pair that does not balance | `RoboNotSupported`, domain `LIST` |
| a target whose surface cannot be rasterised | `RoboNotSupported`, domain `FRAME` |
| a zero or over-maximum target dimension | `RoboInvalidArgument`, domain `TARGET` |
| pixels could not be allocated | `RoboOutOfMemory`, domain `TARGET` |
| a drawing failed one layer down | `RoboError` with that layer's diagnostic |
| a dump buffer too small | `RoboNotSupported`, domain `DUMP`, with the length it needed |
| an empty list | `RoboOk` — a frame that drew nothing |
| an empty or NULL region set | `RoboOk` — nothing was owed |
| NULL `face` with text in the list | `RoboOk`, every run counted in `no_font` |

Five diagnostic domains: `target`, `list`, `frame`, `dump`, and `none` for a generic failure. A
refusal never half-fills a target, and a NULL `out_frame` is legal — a caller that only wants pixels is
not made to hold a record it will not read.

---

## 10. Safety

Every one of these is either refused before a write or answered as a smaller question:

- **zero-size targets** — refused (`RoboInvalidArgument`); a zero *rectangle* is accepted as empty.
- **dimensions where the allocation would overflow** — refused above `RoboRenderMaxDimension`,
  before any arithmetic on them.
- **negative or inverted rectangles** — geometry arrives as integers from RoboPaint, whose own rules
  accept an empty rectangle and refuse a negative extent; a region is clamped to the target.
- **NaN or infinite geometry** — cannot reach this module as a rectangle: layout and paint resolve
  geometry to `int32_t`, and the one `float` quantity (a text pen) is snapped by RoboGraphics before it
  becomes an origin.
- **invalid colours and opacity** — refused one layer down at record time (11.1/11.9's validity rules):
  a display list cannot carry a colour a rasteriser cannot use or an opacity outside `[0, 1]`.
- **missing glyphs and missing images** — counted (`glyphs_missing`, `no_resource`), never a failure.
- **an empty display list** — a successful frame that drew nothing.
- **unsupported commands** — refused, never reinterpreted (§5).
- **clip regions outside the target, source rectangles outside an image** — the graphics raster owns
  the intersection; nothing here computes an offset by hand.
- **out-of-bounds writes** — impossible by construction: every write goes through a RoboGraphics raster
  that clamps to the surface and to the clip.

---

## 11. Determinism

For identical input — the same list, the same target size and format, the same initial pixels — the
target's **bytes** are identical. Nothing in this module reads a clock, an address, an allocation order
or a hash iteration order; the run's only state is the display list's own values, and the record is a
struct of counts and rectangles that can be compared field by field (`RenderFrameEqual`) or
written as text (`RenderFrameWrite`). The dump contains **no address**, which is asserted
(`render.dump`, `render.scene.dump`) rather than promised.

The suites hold this the strongest way available to them: the same list rasterised twice into two
independently created targets, compared byte by byte, with the frame records and the dumps compared
beside them; and a document rebuilt from scratch compared against the first build.

---

## 12. What is absent, and what each absence would need

The capability table (`RoboRender.h`) reports **six 0s**. Each is a phase, not a switch:

| Flag | What it would need |
|---|---|
| ~~`presentation`~~ | **Closed in 11.13** — `RenderPresent`, `RenderPresentFramebuffer` and the record beside them (`render/docs/Presentation.md`). What remains a 0 is the *system* half: a paced handoff, a VBlank signal, a queue. Those are `frame_scheduling`, still 0 below. |
| `compositor` | more than one surface, an ordering, and a place to put the result. A frame here is one list into one target; a compositor is a *set* of frames with stacking — which needs CSS's stacking contexts, a layer tree and a blend/opacity group model that this build does not have (11.9 names the group-opacity gap it would close). |
| `tiles` | a target divided into independently rasterisable pieces and a decision about which pieces a change touched. Needs region **subtraction**, which 11.11 refused, and a tile lifetime. |
| `damage_tracking` | a history of what a scene was, so "unchanged" is answerable without a comparison. 11.11's set is the *per-transition* answer and has no lifetime; a history is a different structure with one. |
| `frame_scheduling` | a clock, a deadline and a queue. This module has none of the three, and that is exactly why it is deterministic. |
| `gpu` | a device, a command buffer, a resource manager and a fallback path: a module, not a branch here. |

Also absent, and deliberately **not** rows in the table, because they are not capabilities of a
rasteriser in this build's shape: transforms, filters, masks, blend modes beyond source-over,
`border-radius`, gradients, antialiasing (the graphics raster's business, and it has none), scrolling,
animations, transitions, and a partial display-list **patch** (the list is rebuilt; the *pixels* are
what regions restrict). Each is named where it belongs — `RoboGraphics`' reserved doors, RoboPaint's
absent records — because a capability row here would suggest this module is where they would arrive.

One more absence is worth stating plainly, because 11.13 changed half of it: **this module presents, and
it does not pace.** A frame reaches a destination in one synchronous call, and the two questions this
document used to leave open were both settled in favour of the *copy* — a presentation composites the
frame's pixels into a destination surface the caller owns, so a target is never a window's back buffer
and two destinations can be handed the same frame (`render/docs/Presentation.md` §1 and §4 state the two
reasons: the module must not depend on RoboWindow, and one authority must apply the clip). What is still 
absent is everything a presentation *system* has: no swap chain, no VSync, no double-buffer management,
no frame queue and no notion of "the previous frame". A frame is finished when the call returns, and
`frame_scheduling` beside `presentation` in the capability table is the 0 that says so.
