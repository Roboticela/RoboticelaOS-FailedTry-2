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

## Drawing (Goal 6.12)

`GfxRasterDrawGlyph` with `RoboGfxGlyphModeOutlineStroke`:

* Converts outline → path
* Strokes with 1-pixel Bresenham lines through the raster (clip + opacity)
* Empty / fully outside clip → successful no-op
* No fill, no antialiasing

Reserved modes (return `RoboNotSupported`):

* `MASK` — monochrome coverage
* `ALPHA` — grayscale AA
* `COLOR` — COLR / bitmap / SVG

## Alpha contract

Stroke color uses Goal 6.10 source-over when translucent / opacity &lt; 255.
No second alpha model.

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

Filled glyph rasterization, AA, subpixel, color fonts, shaping, bidi,
kerning, ligatures, fallback — RoboText and later graphics goals.

## Tests

`graphics.text.*` in `graphics/tests/SuiteRoboGraphicsText.c`.
