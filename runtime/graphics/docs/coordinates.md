# Coordinates (RoboGraphics 6.1)

## Spaces

| Space | Meaning |
|---|---|
| Integer pixel | Device/surface pixel indices |
| Floating graphics | Sub-pixel drawing coordinates (same axes) |
| Surface | Relative to a surface origin |
| Window | Relative to a native window client area (later) |
| Display | Relative to a display / framebuffer (later) |

All of the above use the **same axis convention** in RoboGraphics:

* **Origin:** top-left
* **+X:** right
* **+Y:** down

(This matches Chromium Views/`ui/gfx` and typical UI/framebuffer conventions.
Font design-space Y-up remains RoboFont-only and is converted at text raster.)

## Rectangles

Integer `robo_gfx_rect_t` is **half-open**:

```text
[x, x+width) × [y, y+height)
```

`Contains(x+width, y)` is false. Pixel `(i, j)` covers continuous
`[i, i+1) × [j, j+1)`.

## Rounding

| Helper | Rule |
|---|---|
| `point_f_floor` | Floor toward −∞ |
| `point_f_round` | Half away from zero |
| `rect_f_floor` | Floor origin and opposite corner independently |
| `rect_f_round_out` | Floor min, ceil max (outer cover) |

Floating precision: IEEE-754 `float` is the public graphics scalar for 6.1.
Higher precision (double) is not required for the early visual path.
