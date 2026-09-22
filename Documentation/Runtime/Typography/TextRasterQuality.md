# Text Raster Quality — Phase 12.1

Coverage-antialiased, gamma-aware glyph fills for the RoboticelaOS runtime.

**The one architectural rule this phase exists to establish:**

> **Glyph rasterization is independent of text shaping.**

A *rasterizer* turns one outline, chosen by somebody else, into pixels. A *shaper* decides which
outlines to choose, in what order, at what positions. This phase builds the first and does not
touch the second: the code-point walk through the cmap that Phase 11.6 left in place is still the
thing that decides which glyph is drawn, and Phase 12.4 onward replaces *that* decision without
replacing anything below it. The seam between the two is a `GlyphId` plus a device-space baseline
origin — the same `GfxGlyphDraw` record Phase 6.12 already had.

## The before and the after

Phase 6.12's `RoboGfxGlyphModeOutlineStroke` draws a glyph by converting its outline to a path and
**stroking that path with one-pixel Bresenham lines**. At UI sizes a 1-px stroke is roughly as wide
as the stem it is tracing, so 12 px text reads as a hard-edged blob with no intermediate values at
all; at 32 px it reads as a hollow double outline, because the contour is drawn and the interior is
not filled. That is the mode every caller inherited from `GfxGlyphDrawInit`.

Nothing about the *placement* of text was wrong. The pen, the baseline, the advances and the
cmap lookup are unchanged by this phase. Only the paint changed:

| | Phase 11 (stroke) | Phase 12.1 (coverage) |
|---|---|---|
| Ink | 1-px contour trace | filled outline |
| Distinct gray levels, 12 px band | **2** | **65** |
| Distinct gray levels, 32 px wordmark | **2** | **65** |
| Partial-coverage pixels, 12 px band | 0 | 2 660 |
| Partial-coverage pixels, 32 px wordmark | 0 | 2 050 |

Those numbers are the fixture's own output (`graphics.typography.*`), not an estimate. "Two gray
levels" is the whole of the stroke path's vocabulary: the ink's exact bytes or the page's, never
anything between. The before/after PNGs the fixture writes beside the build are the same two
pictures a person can look at.

## Where the code lives

```text
RoboFont                      outlines (TT quad / CFF cubic), cmap, metrics   ← unchanged
    ↓
RoboGraphicsText.c            glyph id → outline → device geometry → mask     ← the bridge
    ↓
RoboGraphicsGlyph.c/.h        outline → coverage mask → coverage → alpha      ← Phase 12.1
    ↓
RoboGraphicsComposite.c       source-over, 8-bit, the module's existing blend ← unchanged
    ↓
RoboRasterizer → framebuffer → RoboWindow → RoboDesktop                       ← unchanged
```

`RoboGraphicsGlyph.h` is the phase's only new public interface. Two properties of its placement are
deliberate:

* the coverage layer lives **inside RoboGraphics**, not in a new module, because it is a raster
  operation over an outline, and RoboGraphics is the module that owns rasters. Creating a
  `RoboGlyph` subsystem for it would have been the premature half of `RoboText`;
* the mask is **owned by the caller and readable** (`GfxGlyphMaskCoverage`, `…CoverageAt`) rather
  than opaque-and-draw-only. A quality phase whose output cannot be inspected cannot be tested at
  the pixel level, and Phase 12.4's glyph runs will want the same bytes for caching and for
  sub-pixel composition.

`RoboGraphicsGlyph.c` is written so that the *only* floating point is the font-units → device-units
mapping and the adaptive curve subdivision. Every vertex is quantized to a **1/256 device-pixel
fixed point** lattice and everything downstream — bounding boxes, scanline intersections,
span-to-sample mapping, coverage counts, the coverage → alpha tables — is integer arithmetic. There
is no `sqrt`, no `pow`, no libm call in the file, because it is compiled for the freestanding guest
target as well as the host. Determinism is therefore a property of the code rather than of the
platform's floating-point behaviour: the same input, twice, on the same build, is byte-identical.

## Coverage

A mask is `uint8` per pixel, `width × height`, row-major. It holds **coverage** and nothing else:

```text
  0   the ink does not touch this pixel
255   the ink covers this pixel entirely
 1..254   that fraction of the pixel is ink
```

Coverage is a **linear area fraction**. It is not a colour, it is not an alpha, and it is not
opacity:

```text
effective = coverage × colour.alpha × raster.opacity
```

The three factors meet at exactly one place, in `GfxRasterDrawGlyphMask`, and none of them is
rewritten into another. A half-covered pixel of a half-transparent glyph inside a half-opaque
element composites at `0.5 × 0.5 × 0.5 = 0.125`, which is why folding opacity into the mask — the
obvious shortcut — is the mistake this section exists to forbid. `RoboPaintDecoration.c` passes a
glyph's colour with its own alpha; the paint stack's opacity stack multiplies in through the raster,
one layer down; neither knows about the mask.

## Antialiasing: 8×8 supersampling, evaluated as scanlines

A pixel's coverage is the number of covered samples in an **8×8 grid** inside it, quantized to
`0..255` by `(count * 255 + 32) / 64` — 65 possible values, which is why the fixture's 65 distinct
levels are the *maximum* the model can produce and the coverage path reaches it on both specimen
rows.

The grid is *not* evaluated by testing 64 samples against every edge. Each of the `8 × height`
sub-scanlines is intersected with the outline's flattened edges **once**, the crossings are sorted,
and the row is walked with a **non-zero winding rule**, so a span of covered samples falls out of
the sorted crossings directly. That is the same answer for a fraction of the work, and it is where
the winding rule earns its keep: a TrueType or CFF counter — the hole in an `o` — is a contour wound
the other way, and it *subtracts* from the winding, so the span ends exactly where the hole begins.
Horizontal edges are dropped rather than processed (they can contribute no winding change over a
row) and degenerate edges are dropped by a predicate that cannot be satisfied.

Curve flattening is **adaptive**: a quadratic or cubic is subdivided in half until its control
polygon is flat to within a quarter device pixel, or until a depth cap of 12 is reached. Small
sizes therefore cost a handful of segments and large sizes stay smooth, without the fixed
subdivision count `GfxGlyphOutlineToPath` uses for stroking. A glyph that flattens to more than
262 144 edges draws **nothing** rather than growing a builder without a bound, and a coordinate
further than ~4 000 000 px from the origin is treated the same way — a limit, not a crash.

`RoboGfxGlyphSamplesPerPixel` is 8. `GfxGlyphRasterParams.supersample` accepts 1, 2, 4, 8 or 16 (a
power of two, so the lattice divides a pixel exactly) for a caller that wants to trade cost for
smoothness; 0 means "the default". Nothing in the tree needs a non-default value today, and the
parameter exists so that the *choice* is a parameter rather than a constant buried in a loop.

## Gamma: three documented models, one conversion point

Coverage is a linear area fraction. The framebuffer stores **sRGB-encoded 8-bit values and the
compositor blends those values directly**, so writing linear coverage into that blend as if it were
an sRGB alpha is a *choice*, not a fact — and the choice is made in one function,
`GfxGammaCoverageToAlpha`.

| Model | Formula | Exact when |
|---|---|---|
| `RoboGfxCoverageGammaLinear` *(default)* | `alpha = coverage` | never exactly; both polarities wrong by a comparable, opposite-signed amount |
| `RoboGfxCoverageGammaDarkOnLight` | `alpha = 1 − Esrgb(1 − coverage)` | the glyph is the darker of the two colours |
| `RoboGfxCoverageGammaLightOnDark` | `alpha = Esrgb(coverage)` | the glyph is the lighter of the two |

`Esrgb` is the sRGB opto-electronic transfer function
(`12.92u` below `0.0031308`, `1.055·u^(1/2.4) − 0.055` above it), evaluated at table build time and
embedded as a `const` 256-entry table. There is no runtime initialization, no global mutable state
and no floating point in the conversion, and no magic correction factor: each row above *is* the
transfer function its derivation names.

**Why linear is the default.** In sRGB-space compositing the exact linear-light alpha is *below*
coverage for dark ink on a light surface and *above* it for light ink on a dark one. The two errors
are of comparable size and opposite sign, so the linear model is the polarity-neutral one — the same
reason every mainstream rasterizer that blends grayscale coverage into an sRGB surface does the same
thing. A caller that knows which polarity it has can do better, which is why the other two models
exist; a caller that has not thought about it gets the model that is not wrong in one direction.

Every model satisfies `0 → 0`, `255 → 255` and monotonicity, and the suite pins the endpoints, the
monotonicity of all three and the *ordering* of the two polarity models against linear in the middle
of the range. An unrecognized model value is treated as the default rather than refused, because a
caller's bad configuration must not be able to draw a **darker** pixel than the glyph's own colour —
which is what indexing a table with an out-of-range enum would do.

## Coordinate model and clipping

The geometry is exactly what `GfxGlyphOutlineToPath` documents — font units scaled by
`sizePx / unitsPerEm`, `+Y` up becoming `+Y` down about the baseline origin — with one deliberate
difference: **nothing is snapped to an integer pixel**. A fractional origin places the glyph at a
fractional device position, which is what lets a run sit on its accumulated advances rather than on
rounded copies of them. `GfxGlyphSnapPoint` remains the module's whole-pixel policy for a caller
that wants it.

Clipping is the raster's own clip, which is by construction always inside the surface's pixel bounds.
The mask rectangle is the intersection of the ink's bounds with that clip, so:

* a glyph whose ink runs off the framebuffer costs only the pixels that are on it;
* a pixel outside the surface can never be written, even when the mask's rectangle overhangs it;
* a glyph entirely outside the clip rasterizes to an **empty mask** and returns `RoboOk`.

Negative left side bearings, ink above the ascender and ink below the baseline need no special
handling for the same reason: the bounds come from the outline's own control points, not from the
advance or the line box, so they are simply where the ink is. `GfxGlyphRasterStats.clipped` reports
when the clip narrowed the mask, so a caller can tell "nothing was drawn" from "the interesting part
was off-screen".

## Empty, missing and malformed glyphs

| Input | Result |
|---|---|
| whitespace / zero-area / degenerate contour | empty mask, `RoboOk` — nothing is drawn, which is the truthful answer |
| `.notdef` (`RoboGlyphIdNone`) | whatever RoboFont has for glyph 0; usually a box, drawn |
| invalid glyph id | `RoboInvalidArgument` |
| malformed outline | `RoboInvalidArgument` |
| outline this build cannot use | `RoboNotSupported` |
| coordinates far outside the viewport | empty mask, `RoboOk` |
| more edges than the builder allows | empty mask, `RoboOk` |

The rule behind the last three rows: a glyph that cannot be *represented* is a failure the caller
should hear about, and a glyph that simply has no ink where it was asked to draw is not. A pass that
turned "this glyph is a space" into `RoboError` would make every text frame a failure.

## Outline formats and colour fonts

TrueType quadratic outlines and CFF/CFF2 cubic outlines both reach the rasterizer through
**RoboFont's existing normalized `FontOutline`**, so the phase adds no outline parser and no second
geometry model: `GfxGlyphMaskRasterizeOutline` consumes the same structure
`GfxGlyphOutlineToPath` does, and the two differ only in what they do with it. Composite glyphs,
variations and the rest are RoboFont's business and are already resolved before the outline is
handed over.

**Colour fonts are not rendered.** `RoboGfxGlyphModeColor` (COLR, bitmap, SVG) still answers
`RoboNotSupported` rather than approximating a colour glyph with its monochrome outline, which would
be a *worse* answer than admitting the gap. Colour-font rendering belongs to the emoji/colour glyph
work in Phase 12.19, where the colour layers are what the phase is about.

## Hinting

RoboFont exposes **no TrueType bytecode interpreter**, so this build has no hinting, and Phase 12.1
does not implement one — a full graphics-state-machine interpreter is not a prerequisite for
coverage antialiasing, and blocking the quality work behind it would have been the wrong order.

The consequence is real and worth stating plainly: at 12 px an unhinted stem is whatever the outline
scaled to, so stems land on fractions and the coverage path renders them **gray rather than crisp**.
That is the honest cost of no hinting, and it is still strictly better than the stroke path, which
had no intermediate values at all. The architecture does not prevent hinting later: hinting would
move *vertices* before the quantizer, and nothing downstream of the quantizer assumes the vertices
came from an unhinted outline. The suite's `graphics.typography.small_size_coverage` case exists to
fail if a future change makes small text *disappear*, which is the property this build can actually
guarantee.

## Caching

**There is no mask cache in this release**, and its absence is a decision rather than an omission:

* the outline fetch is already cached one module down (`FontFaceGlyphOutline`), so the expensive
  parse is not being repeated;
* a mask is produced in a single pass over its own ink — a 12 px glyph is roughly 100 samples, not a
  scan of the surface;
* a cache keyed on (face, glyph, size, mode, gamma) would need lifetime, invalidation and
  mutation-isolation rules that Phase 12.1 has no consumer to justify. A cache with no consumer is
  a correctness risk bought with no measured speed.

When Phase 12.4's glyph runs make the same glyph render thousands of times per frame, the cache
belongs there, keyed by the run's own font-and-size handle, and `GfxGlyphMaskRetain`/`…Release` are
already the ownership primitives it needs.

## Performance

CPU-only, one pass over the ink, no allocation per pixel: the builder allocates its edge array
geometrically (amortized growth, not per-edge), the mask is one allocation, and the composite is a
row walk bounded by the mask's own rectangle. Per-pixel allocation and per-glyph font reloads are
both absent by construction. The architecture is deliberately the kind that *can* be optimized later
— the scanline structure is what a span-based renderer would want, and the mask is a value the
renderer could cache — rather than the kind that has to be rewritten to be fast. GPU rendering is
out of scope for this phase and for this module.

## Integration

The phase is reached through **three call sites and no more**:

| Caller | Change |
|---|---|
| `RoboPaintDecoration.c` (document text) | `draw.mode = RoboGfxGlyphModeAlpha` — one line, plus its argument |
| `RoboDesktopChrome.c` (shell labels) | the same, and the origin is no longer snapped |
| `RoboGraphicsText.c` | `ALPHA` now routes to the coverage path instead of `RoboNotSupported` |

Text remains in the ordinary rendering pipeline: display list → paint → raster → framebuffer. There
is no second text output path, no pre-rendered glyph images and no bitmap text anywhere in this
phase, and `GfxRasterDrawGlyph`'s stroke mode is *kept* rather than deleted, because it is what the
before/after fixture compares against.

## Tests

| Suite | What it pins |
|---|---|
| `graphics.glyph.*` (`Tests/Graphics/SuiteRoboGraphicsGlyph.c`) | coverage values (full, none, partial), edge coverage at a known angle, empty/small/large glyphs, bounds (negative bearing, ascender/descender overflow, framebuffer edge), gamma endpoints + monotonicity + ordering, alpha (opaque, transparent, over an existing destination, nested opacity, source-over), determinism of mask and pixels, and the font → mask → raster → framebuffer integration |
| `graphics.typography.specimen` (`Tests/Graphics/SuiteRoboGraphicsTypography.c`) | the visual regression: the OS's own face at 12/14/16/18/24/32 px, dark on light and light on dark, measured for partial-coverage pixels (coverage > 1 000, stroke exactly 0), for ink in every size band and in the dark panel, for byte-identical repeat renders, and for the two modes producing *different* pictures. Writes `Build/Typography-Coverage.png` and `Build/Typography-Stroke.png` as evidence for a person — best effort, never an assertion, so a read-only tree does not fail on a picture it cannot save |
| `graphics.text.*` | the bridge: `ALPHA` now implemented, `MASK`/`COLOR` still `RoboNotSupported`, and the stroke path unchanged |

The specimen uses the system typeface pack's face — **`System/Fonts/UI/Inter.ttf`**, SIL Open Font
License, redistributed with its licence at `System/Fonts/licenses/Inter-OFL.txt`. That pack, its
manifest and the roles that name the system faces are **Phase 12.2**
(`Documentation/Development/SystemTypefacePack.md`). No proprietary font is copied into this
repository and no font was downloaded because it resembles one: the design target is SF-*class*
typography. The face is Inter Variable, so the pack exposes real `wght` 100–900 and `opsz` 14–32
axes; Phase 12.1 draws one instance, because **variation interpolation is not executed by the
rasterizer** (`FontOutlineVariationApplied` is still 0, RoboFont's own 5.18 boundary). The specimen
therefore draws the face as loaded, and a heavy weight is a matter for the phase that applies axis
coordinates, not for a synthesized emboldening here.

**Which face a surface draws with is not this phase's question.** Phase 12.3 binds every shell and
document surface to a semantic typography role and hands the rasterizer the face that role resolved to
(`Documentation/Runtime/Typography/TypeBinding.md`). The seam is unchanged: 12.1 fills one glyph at a
baseline origin, 12.3 chooses the face and the numbers, and neither knows what the other decided.

## Limitations

* **No shaping of any kind.** No kerning, no ligatures, no GSUB/GPOS, no bidi, no script runs, no
  fallback, no variation selectors. The glyph sequence is still one cmap lookup per code point.
* **No hinting** (see above), and no autohinter.
* **No colour fonts**, no SVG-in-OpenType, no bitmap glyphs.
* **No mask cache** (see above).
* **No gamma selection at the call sites.** Both call sites take the default (linear). The
  polarity models are implemented, tested and documented for a caller that knows its polarity;
  wiring a call site to one is a decision for the phase that owns the surface's colour tokens.
* **No subpixel (RGB) rendering.** Coverage is grayscale, so LCD subpixel geometry is not exploited.
  That is a deliberate choice at this phase — subpixel AA needs a compositor that knows the
  destination is untransformed and opaque, which Phase 15 is where that becomes true.
* **Y-axis, grayscale.** Vertical text, text selection and text editing are later phases.

## QEMU and host

Host tests run the *same* implementation: the coverage rasterizer is compiled into
`Build/Runtime/RoboTest.host.exe` from the same `RoboGraphicsGlyph.c` the guest image builds. The
host is a test *target*, not a second renderer, and there is no host-only text path to drift from
the guest one. The guest image boots the desktop through the same `DesktopChromeLabel` →
`GfxRasterDrawGlyph` → coverage path the host suite exercises, which is why
`Scripts/Test/TestDesktop.ps1 -Target Qemu` is the visual acceptance for this phase.

## Where the next phases plug in

```text
today:   text → cmap per code point → GlyphId → outline → coverage → pixels
12.4+:   text → RoboText shaping → glyph runs → (GlyphId, position) → outline
                                                                       → coverage → pixels
```

Everything below the `GlyphId` line is this phase and does not change. That is the whole point of
separating rasterization from shaping: Phase 12.4 replaces the *chooser* of glyphs and inherits a
rasterizer that already fills at a fractional position, already handles the bounds it will be handed,
already keeps coverage separate from colour, and already produces the mask a glyph-run cache wants.
