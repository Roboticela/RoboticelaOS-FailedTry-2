# CPU Raster Backend (Goal 6.11)

Immediate-mode CPU drawing into an existing `robo_gfx_surface_t`. This is the
first reusable raster backend for later path fill/stroke, text, paint records,
HTML/CSS rendering, screenshots, and compositor integration.

Chromium/Skia are temporary reference only (e.g. SkCanvas → device immediate
draw). RoboGraphics owns this implementation entirely.

## Purpose

```text
geometry / color
      ↓
robo_gfx_raster_t   (clip + opacity state)
      ↓
pixel / rect / line ops
      ↓
surface pixels  (+ Goal 6.10 source-over when needed)
```

Not a GPU backend, display-list recorder, or paint engine.

## Raster target

The target **is** an existing write-capable surface (Goals 6.3–6.5). The raster
context retains it and does not own framebuffer storage.

Required:

* write-capable access (`WRITE` or `READ_WRITE`)
* implemented CPU pixel format
* dimensions representable as int32 pixel bounds (including zero size)

## State

| Field | Default | Notes |
|---|---|---|
| target surface | from `create` | retained until `destroy` |
| clip | full surface bounds | half-open; always ⊆ surface |
| opacity | 255 | multiplies source alpha (Goal 6.10) |

Clip APIs: `reset_clip`, `set_clip` (∩ surface), `intersect_clip`.
Empty clip → draws succeed as no-ops.

## Supported operations

| Op | Behavior |
|---|---|
| `clear` | fill current clip |
| `draw_pixel` | one pixel; outside clip → no-op |
| `fill_rect` | half-open rect ∩ clip |
| `draw_line` | Goal 6.7 Bresenham ∩ clip |
| `fill_path` / `stroke_path` | boundary only → `ROBO_NOT_SUPPORTED` |

## Alpha / compositing

Uses Goal 6.10 source-over. Global opacity multiplies source alpha before blend.
When color alpha is 255 and opacity is 255, opaque paths may replace pixels
(mathematically identical to src-over). Transparent clear via src-over is a
no-op (use an opaque clear color to erase).

## Clipping

Rectangular only. Geometry is clipped to the effective clip before writes.
Never writes outside the destination surface. No path clipping.

## Operation ordering

Immediate mutation. Draw A then B then C → destination reflects that order.
No command buffer. No hidden temporary buffering for these ops (line/rect
write destination pixels directly).

## Pixel formats

All implemented CPU formats from Goal 6.2 (RGBA8, BGRA8, RGB8, Gray8, Alpha8).
Channel order follows pack/unpack; opaque formats treat destination alpha per
the existing pixel-format contract.

## Errors

| Condition | Result |
|---|---|
| NULL / non-writable target | `ROBO_INVALID_ARGUMENT` |
| unsupported format | `ROBO_NOT_SUPPORTED` |
| invalid clip/rect | `ROBO_INVALID_ARGUMENT` |
| path fill/stroke | `ROBO_NOT_SUPPORTED` (after arg check) |
| OOM on create | `ROBO_OUT_OF_MEMORY` |

## Performance expectations

* No heap allocation in per-pixel loops
* Clip resolved before row/pixel loops
* Opaque fill uses bulk `fill_rect` when possible
* No SIMD / threads / tiles in this goal

## Limitations (later goals)

Path fill/stroke, Bézier flatten, AA, gradients, transforms during draw,
GPU raster, display lists, HTML/CSS. Glyph outline stroke via Goal 6.12
(`TextBridge.md`); filled text AA later.

## Tests

`graphics.raster.*` in `graphics/tests/SuiteRoboGraphicsRaster.c`.
