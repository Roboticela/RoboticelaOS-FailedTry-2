# Coordinates (RoboGraphics 6.1 / 6.6)

## Spaces

| Space | Meaning |
|---|---|
| Integer pixel | Device/surface pixel indices (`robo_gfx_pixel_x_t` / `_y_t`) |
| Floating graphics | Sub-pixel drawing coordinates (same axes) |
| Surface | Relative to a surface origin |
| Window | Relative to a native window client area (later) |
| Display | Relative to a display / framebuffer (later) |

All of the above use the **same axis convention** in RoboGraphics:

* **Origin:** top-left
* **+X:** right
* **+Y:** down

(This matches Chromium Views/`ui/gfx` and typical UI/framebuffer conventions —
reference only, not a dependency. Font design-space Y-up remains RoboFont-only
and is converted at text raster.)

## Geometry vs storage

```text
Point/Rect coordinates
    ↓
signed logical graphics coordinates   (negative positions allowed)

Framebuffer memory indices
    ↓
validated non-negative storage offsets
```

A rectangle such as `(-20, -10, 100, 80)` is valid **geometry**. Accessing
pixels requires clipping or enclosure checks against `pixel_bounds` first.

## Rectangles

Integer `robo_gfx_rect_t` is **half-open**:

```text
[x, x+width) × [y, y+height)
```

`Contains(x+width, y)` is false. Pixel `(i, j)` covers continuous
`[i, i+1) × [j, j+1)`.

### Empty

`width <= 0` or `height <= 0`.

### Overflow

Edge and translate arithmetic must not silently wrap. Use `_checked` APIs or
helpers that return `robo_result_t`. See [Geometry.md](Geometry.md).

## Rounding

| Helper | Rule |
|---|---|
| `point_f_floor` | Floor toward −∞ |
| `point_f_round` | Half away from zero |
| `rect_f_floor` | Floor origin and opposite corner independently |
| `rect_f_round_out` | Floor min, ceil max (outer cover) |

Floating precision: IEEE-754 `float` is the public graphics scalar for early
Phase 6. Higher precision (double) is not required for the early visual path.
