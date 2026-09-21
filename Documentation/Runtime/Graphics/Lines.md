# Lines (Goal 6.7)

One-pixel-wide deterministic integer line rasterization onto CPU surfaces.

```text
geometry (Point / Line)
    ↓
clip to surface pixel_bounds (inclusive window)
    ↓
Bresenham rasterize
    ↓
Goal 6.2 pixel_pack → direct pixel replace
```

## API

| Symbol | Role |
|---|---|
| `GfxLine` | `{ start, end }` integer endpoints |
| `GfxLine` | Constructor |
| `GfxSurfaceDrawLine` | Draw `start`→`end` |
| `GfxSurfaceDrawLineSeg` | Draw a `GfxLine` |

## Semantics

| Rule | Behavior |
|---|---|
| Width | Exactly one pixel |
| Endpoints | **Inclusive** (start and end both plotted) |
| Reversed endpoints | Same pixel set (endpoints are canonicalized) |
| `start == end` | Exactly one pixel |
| Color | Logical RGBA8 packed via Goal 6.2 |
| Write | Direct replace — **no** alpha blending |
| Antialiasing | **None** |
| Caps / joins / dashes / thick strokes | **Not implemented** |

Example: `draw_line((0,0), (3,0))` writes `(0,0)(1,0)(2,0)(3,0)`.

## Clipping / out of bounds

1. Query `GfxSurfacePixelBounds` → half-open `[0,w)×[0,h)`.
2. Clip the segment to the inclusive pixel window `[0,w-1]×[0,h-1]`
   (Cohen–Sutherland, int64 edge math).
3. Rasterize the clipped segment.

| Case | Result |
|---|---|
| Fully outside | `RoboOk`, no pixels written |
| Partially outside | Only in-bounds pixels written |
| Empty surface | `RoboOk` no-op |

Negative logical coordinates are allowed as **geometry**; storage access only
occurs for clipped in-bounds pixels.

## Determinism

Integer Bresenham with `int64` deltas/error — no floating slopes. Same pixel
set across architectures/compilers for the same endpoints and surface.

## Errors

| Condition | Code |
|---|---|
| NULL / non-writable surface | `RoboInvalidArgument` |
| Unsupported format | `RoboNotSupported` |

## Deferred

AA, variable width, GPU lines. Path **data model** is Goal 6.8 (`Paths.md`);
path stroke rasterization remains later. Clip-aware / blended lines: `GfxSurfaceDrawLineClipped` (replace or
source-over via @p composite). Goal 6.11 raster uses composite when needed.
