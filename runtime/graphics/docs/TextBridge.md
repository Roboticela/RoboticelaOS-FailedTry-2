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

Primary unit: `robo_glyph_id_t` + `robo_font_face_t` + size + baseline origin.

`robo_gfx_glyph_draw_t` carries face, glyph id, `size_px`, device origin,
color, and render mode. Opacity comes from `robo_gfx_raster_t`.

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

Integer snap uses `robo_gfx_point_f_round` (half away from zero), consistent
with RoboFont nearest rounding.

## Metrics

`robo_gfx_glyph_metrics_device` scales RoboFont glyph metrics (advance, LSB,
outline bbox when present). Line ascent/descent come from
`robo_font_face_layout_metrics` — not recomputed by graphics.

Device bounds are Y-down relative to baseline origin (0,0).

## Outline → path

`robo_gfx_glyph_outline_to_path` / `robo_gfx_outline_to_path`:

* MOVE / LINE / CLOSE → RoboGraphics path verbs
* QUAD / CUBIC → fixed subdivision into LineTo (geometry only; not AA Bezier
  rasterization)
* Empty outline → empty path, success

## Drawing (Goal 6.12)

`robo_gfx_raster_draw_glyph` with `ROBO_GFX_GLYPH_MODE_OUTLINE_STROKE`:

* Converts outline → path
* Strokes with 1-pixel Bresenham lines through the raster (clip + opacity)
* Empty / fully outside clip → successful no-op
* No fill, no antialiasing

Reserved modes (return `ROBO_NOT_SUPPORTED`):

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
| NULL face / raster / path | `ROBO_INVALID_ARGUMENT` |
| size ≤ 0 / non-finite | `ROBO_INVALID_ARGUMENT` |
| invalid glyph id | `ROBO_INVALID_ARGUMENT` |
| unsupported outline / mode | `ROBO_NOT_SUPPORTED` |
| malformed outline | `ROBO_INVALID_ARGUMENT` |

## Limitations / later

Filled glyph rasterization, AA, subpixel, color fonts, shaping, bidi,
kerning, ligatures, fallback — RoboText and later graphics goals.

## Tests

`graphics.text.*` in `graphics/tests/SuiteRoboGraphicsText.c`.
