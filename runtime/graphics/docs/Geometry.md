# Geometry — Pixels and Rectangles (Goal 6.6)

Canonical integer geometry for RoboGraphics. Extends the Goal 6.1 types; does
not introduce a parallel geometry library.

## Coordinate model

| Axis | Convention |
|---|---|
| Origin | Top-left of the space |
| +X (`robo_gfx_pixel_x_t`) | Right |
| +Y (`robo_gfx_pixel_y_t`) | Down |

```text
Point / Rect coordinates
    ↓
signed logical graphics coordinates  (negative allowed)

Framebuffer / surface memory indices
    ↓
validated non-negative storage offsets
```

Logical rectangles may sit partially or wholly outside a surface. Pixel
read/write validates storage indices before memory access.

## Types

| Type | Role |
|---|---|
| `robo_gfx_point_t` | Integer point `(x, y)` |
| `robo_gfx_size_t` | `(width, height)` |
| `robo_gfx_rect_t` | Half-open `x, y, width, height` |
| `robo_gfx_vector_t` | Translation delta `(dx, dy)` |

## Rectangle semantics

Half-open coverage:

```text
left   = x
top    = y
right  = x + width     (exclusive)
bottom = y + height    (exclusive)

pixels: [left, right) × [top, bottom)
```

Example: `Rect(10, 20, 100, 50)` contains `(10,20)` and `(109,69)`, not
`(110,20)` or `(10,70)`.

### Empty

`width <= 0` **or** `height <= 0` ⇒ empty.

Empty intersection / clip results are success with an empty rect.
`fill_rect` / `copy_rect` treat empty as a successful no-op.

### Invalid

`width < 0` or `height < 0` ⇒ invalid (`robo_gfx_rect_is_valid` is false).
Operations return `ROBO_INVALID_ARGUMENT`.

## Operations

| API | Behavior |
|---|---|
| `from_point_size` / `from_ltrb` | Safe constructors |
| `contains_point` | Half-open containment |
| `encloses` | Rect fully inside another (empty inner ⇒ true) |
| `intersect` / `clip` | `max(left/top)`, `min(right/bottom)` |
| `bounding` | Smallest cover; empty inputs ignored |
| `translate` | Overflow-checked |
| `inset` / `outset` | Uniform shrink / grow; over-shrink ⇒ empty |

## Overflow

Checked helpers return `ROBO_INVALID_ARGUMENT` on int32 overflow.
Unchecked `right()` / `bottom()` use int64 math and **saturate** (never signed wrap). Prefer `_checked` when overflow must be an error.

## Surface bridge

`robo_gfx_surface_pixel_bounds(surface)` → `Rect(0, 0, width, height)`.

Bitmap ops (Goal 6.5) use enclosure against these bounds. Automatic clipping is
not applied — call `robo_gfx_rect_clip` first when geometry may leave the surface.

## Deferred

Curves (construction), floating transforms beyond the affine API in
`Transforms.md`, regions.
