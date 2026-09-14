# Transforms (Goal 6.9)

Canonical 2D **affine** transforms for RoboGraphics. Used by future path
rasterization, layout, paint, and compositing. No perspective / projective
transforms.

```text
robo_gfx_affine_t  (column-vector 3×3 affine)
        ↓
compose / invert
        ↓
map point · map rect (AABB) · map path
```

## Representation

Same type as Goal 6.1 geometry:

```text
| m00 m01 m02 |   | x |
| m10 m11 m12 | × | y |
|  0   0   1  |   | 1 |
```

Fields: `m00, m01, m02, m10, m11, m12` (float).

Identity: `m00 = m11 = 1`, others `0`.

## Coordinate convention

Matches RoboGraphics geometry:

- Origin top-left of the device/surface
- +X right, +Y down

## Composition order

```text
robo_gfx_affine_multiply(A, B, out)   // out = A * B
robo_gfx_affine_compose(A, B, out)    // same
```

Point mapping:

```text
(A * B) · p  =  A · (B · p)
```

**Apply B first, then A.**

| Helper | Effect |
|---|---|
| `pre_concat(dst, other)` | `dst = dst * other` (other applied first) |
| `post_concat(dst, other)` | `dst = other * dst` (dst applied first) |

This matches Chromium `gfx::Transform` PreConcat / PostConcat semantics
(reference only — no Chromium dependency).

## Angles / rotation

| API | Units |
|---|---|
| `robo_gfx_affine_rotate_deg` | degrees (primary) |
| `robo_gfx_affine_rotate_rad` | radians |

**Positive angle** = counterclockwise in the mathematical plane (matrix form
compatible with Chromium/Skia). With Y-down device axes this appears
**clockwise on screen**.

Exact matrices for multiples of 90° (including ±180, ±270, ±360):

| Angle | Map `(x,y)` |
|---|---|
| 0° / ±360° | `(x, y)` |
| 90° | `(−y, x)` |
| −90° / 270° | `(y, −x)` |
| 180° | `(−x, −y)` |

## Shear

`robo_gfx_affine_shear(shx, shy)`:

```text
x' = x + shx * y
y' = y + shy * x
```

## Point mapping

| API | Behavior |
|---|---|
| `map_point` / `map_point_f` | Float result |
| `map_point_i` | Float map, then `point_f_round` (half away from zero) |

Does not mutate the input point.

## Rectangle mapping

Transforms the four continuous corners of a half-open rect, then returns the
**axis-aligned enclosing** bounds:

- Float: `map_rect_f`
- Integer: `map_rect` — floor(min), ceil(max) → half-open `robo_gfx_rect_t`

Empty / invalid input → empty output, `ROBO_OK`.

No oriented-rectangle type.

## Path mapping

| API | Behavior |
|---|---|
| `robo_gfx_path_transformed` | New path; source unchanged |
| `robo_gfx_path_transform` | In-place; on failure restores original |

- MoveTo / LineTo points are mapped with `map_point_i`
- Close verbs are preserved (no extra geometry)
- `QUAD_TO` / `CUBIC_TO` → `ROBO_NOT_SUPPORTED` (extensible later)

## Inverse

`robo_gfx_affine_invert` / `is_invertible`:

- Uses `|det| > 1e-12` on the linear 2×2
- Singular (e.g. zero scale) → `ROBO_INVALID_ARGUMENT`, output unchanged
- No division by zero

## Construction helpers

| Helper | Role |
|---|---|
| `identity` | Identity |
| `translate` | Translation |
| `scale` | Scale / reflection (`sx`/`sy` may be 0 or negative) |
| `rotate_deg` / `rotate_rad` | Rotation about origin |
| `shear` | Shear |
| `set` | Arbitrary affine |

## Current limitations

- Float32 precision only; freestanding sin/cos (no libm) for non-90° angles
- Exact matrices for multiples of 90°
- No perspective
- No curve-control-point transform beyond reserved verbs
- Path points snap via round — subpixel path storage is a later concern

## Deferred

Alpha compositing is Goal 6.10 (`Compositing.md`). CPU raster is Goal 6.11
(`Raster.md`). Path fill/stroke, GPU/compositor transforms, CSS/DOM transforms,
animation remain later.
