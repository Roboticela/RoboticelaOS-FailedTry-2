# Alpha Compositing (Goal 6.10)

CPU source-over compositing for RoboGraphics. Foundation for later raster
and paint — not a general blend engine or GPU compositor.

```text
straight RGBA8 (canonical storage)
        ↓
premultiply (blend intermediate)
        ↓
source-over
        ↓
unpremultiply → straight RGBA8
        ↓
Goal 6.2 pack → surface
```

## Alpha representation

| Property | Contract |
|---|---|
| Storage | **Straight** (unassociated) RGBA8 — same as Goal 6.1 |
| Range | Channels `0..255`; `0` = transparent / empty, `255` = opaque / full |
| Float colors | Nominal `[0,1]` (unchanged; compositing APIs use 8-bit) |
| Rounding | `mul_div255(a,b) = (a*b + 127) / 255` |
| Clamp | Premul blend intermediates clamp to `255` before unpremultiply |

Do **not** mix straight and premultiplied values in the same API call.
Explicit helpers:

| API | Role |
|---|---|
| `robo_gfx_color_premultiply` | Straight → premul |
| `robo_gfx_color_unpremultiply` | Premul → straight |

## Source-over

Only Porter–Duff **SrcOver** is implemented.

With straight colors, effective source alpha `sa`, destination alpha `da`:

```text
src_p = premultiply(src with alpha sa)
dst_p = premultiply(dst)
out_p = src_p + dst_p * (1 - sa)
out   = unpremultiply(out_p)
```

Fast paths:

| Case | Result |
|---|---|
| `sa == 0` | destination unchanged |
| `sa == 255` | source RGB with `a = 255` |
| `opacity == 0` | destination unchanged |

## Global opacity

`opacity` is `uint8_t` (`0` = invisible, `255` = full).

```text
effective_sa = mul_div255(src.a, opacity)
```

Source **RGB are not scaled** (straight-alpha / CSS opacity style).
Opacity is applied before source-over.

## Pixel formats

Compositing always works in straight RGBA8 logical colors (unpack → blend →
pack via Goal 6.2).

| Format | Destination alpha on read | Write |
|---|---|---|
| RGBA8 / BGRA8 | Stored A | Stored A |
| RGB8 | Treated as `A=255` | Alpha discarded |
| GRAY8 | Treated as `A=255` | Luminance of result RGB |
| ALPHA8 | `R=G=B=0`, A stored | Writes A only |

Channel order is never reinterpreted — pack/unpack own the layout.

## Surface API

| API | Role |
|---|---|
| `robo_gfx_composite_src_over` | Pixel math |
| `robo_gfx_composite_src_over_opacity` | Pixel math + opacity |
| `robo_gfx_surface_composite_pixel` | One destination pixel |
| `robo_gfx_surface_composite_rect` | Clipped rectangle blit |
| `robo_gfx_surface_composite` | Full source → destination |

### Clipping

`composite_rect` **clips** to source and destination pixel bounds (unlike
`copy_rect`, which requires full containment). Negative destinations and
partially outside regions are safe. Fully outside / empty → `ROBO_OK` no-op.
Never reads outside source or writes outside destination.

No scaling, filtering, or transforms.

### Self-compositing

When source and destination share the same pixel storage, the clipped source
region is **snapshotted** into a temporary buffer before blending. Overlapping
self-blits are therefore deterministic and do not read partially written
pixels. OOM → `ROBO_OUT_OF_MEMORY`.

## Performance

- No per-pixel heap allocation in the normal (distinct storage) path
- Temporary buffer only for same-storage blits (size = clipped region)
- Inner loop: get → blend → set via existing pack/unpack
- Suitable as a correctness foundation for Goal 6.11 CPU raster; not SIMD

## Deferred

Other Porter–Duff operators, CSS blend modes, filters, masks, antialiasing,
GPU/compositor layers, transforms during blit, image scaling.
CPU raster draw context is Goal 6.11 (`Raster.md`).
