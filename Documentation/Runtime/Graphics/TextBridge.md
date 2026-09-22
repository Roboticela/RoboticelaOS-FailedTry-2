# Text Rendering Bridge (Goal 6.12)

Integration boundary between **RoboFont** and **RoboGraphics**.

RoboGraphics does **not** shape text, segment Unicode, run bidi, select fonts,
or fall back. Those belong to RoboText / RoboFont / I18N. This bridge accepts
**glyph IDs** + placement and talks to surfaces through the CPU raster backend.

```text
RoboFont (faces, metrics, outlines)
        ↓
RoboGraphics Text Bridge
        ↓
RoboGraphics Path / Raster / Composite
```

RoboFont must not depend on RoboGraphics.

## Glyph-centric model

Primary unit: `GlyphId` + `FontFace` + size + baseline origin.

`GfxGlyphDraw` carries face, glyph id, `size_px`, device origin,
color, and render mode. Opacity comes from `GfxRaster`.

## Coordinate systems

| Space | Origin | +X | +Y |
|---|---|---|---|
| Font design (FUnits) | baseline | right | **up** |
| RoboGraphics device | surface top-left | right | **down** |

Conversion:

```text
scale = size_px / units_per_em
device_x = origin_x + font_x * scale
device_y = origin_y - font_y * scale
```

`origin` is the glyph **baseline** point in device space.

Integer snap uses `GfxPointFRound` (half away from zero), consistent
with RoboFont nearest rounding.

## Metrics

`GfxGlyphMetricsDevice` scales RoboFont glyph metrics (advance, LSB,
outline bbox when present). Line ascent/descent come from
`FontFaceLayoutMetrics` — not recomputed by graphics.

Device bounds are Y-down relative to baseline origin (0,0).

## Outline → path

`GfxGlyphOutlineToPath` / `GfxOutlineToPath`:

* MOVE / LINE / CLOSE → RoboGraphics path verbs
* QUAD / CUBIC → fixed subdivision into LineTo (geometry only; not AA Bezier
  rasterization)
* Empty outline → empty path, success

## Drawing

### `RoboGfxGlyphModeOutlineStroke` (Goal 6.12)

`GfxRasterDrawGlyph`:

* Converts outline → path
* Strokes with 1-pixel Bresenham lines through the raster, origin snapped to a whole pixel
  (clip + opacity)
* Empty / fully outside clip → successful no-op
* No fill, no antialiasing

Kept rather than retired: it is cheap to audit, and it is what the Phase 12.1 quality fixture
compares the coverage path against. It is **not** what UI text should use.

### `RoboGfxGlyphModeAlpha` (Phase 12.1)

`GfxRasterDrawGlyph` / `GfxRasterDrawGlyphCoverage`:

* Converts outline → device geometry (adaptive flattening, **no snapping**)
* Rasterizes the outline into a grayscale **coverage mask** — 8×8 supersampling evaluated as
  scanline coverage with a non-zero winding rule, in 1/256 px fixed point
* Converts coverage → blend alpha through the documented gamma model (`RoboGfxCoverageGamma*`)
* Composites through the same source-over blend as the stroke mode
* Empty outline / fully outside clip → successful no-op, empty mask
* This is the mode the paint stack's text path and the desktop shell's labels select

`GfxRasterDrawGlyphCoverage` is the same door with explicit `GfxGlyphRasterParams` (gamma model,
supersample count). The mask itself is a first-class, readable value (`RoboGraphicsGlyph.h`);
not-yet-implemented modes (return `RoboNotSupported`):

* `MASK` — binary monochrome coverage
* `COLOR` — COLR / bitmap / SVG

## Alpha contract

Draw color uses Goal 6.10 source-over when translucent / opacity &lt; 255. No second alpha model.

A coverage mask's byte is **coverage**, not alpha: the effective contribution of a pixel is
`coverage × colour.alpha × raster.opacity`, and the three stay separate facts joined only at
`GfxRasterDrawGlyphMask`. Opacity is never folded into the mask.

## Clipping

Uses the raster context clip only. Never writes outside the destination
surface.

## Errors

| Condition | Result |
|---|---|
| NULL face / raster / path | `RoboInvalidArgument` |
| size ≤ 0 / non-finite | `RoboInvalidArgument` |
| invalid glyph id | `RoboInvalidArgument` |
| unsupported outline / mode | `RoboNotSupported` |
| malformed outline | `RoboInvalidArgument` |

## Limitations / later

Shaping, bidi, kerning, ligatures, fallback, hinting, colour fonts, subpixel (RGB) rendering,
mask caching — later goals. Glyph coverage rasterization itself is **landed** (Phase 12.1): see
`Documentation/Runtime/Typography/TextRasterQuality.md` for the coverage model, the supersampling
scheme, the gamma models, the coordinate model and the measured before/after.

## Tests

`graphics.text.*` (`Tests/Graphics/SuiteRoboGraphicsText.c`) for the bridge,
`graphics.glyph.*` (`Tests/Graphics/SuiteRoboGraphicsGlyph.c`) for the coverage rasterizer and
`graphics.typography.*` (`Tests/Graphics/SuiteRoboGraphicsTypography.c`) for the visual fixture.
