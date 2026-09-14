# Basic Bitmap Operations (Goal 6.5)

Bitmap operations act on **CPU-accessible surfaces** (Goal 6.4). There is no
separate public `Bitmap` type — a surface already carries width, height,
format, stride, and storage.

```text
Pixel Formats → Framebuffer → Surface → Bitmap ops (this goal)
```

## Operations

| API | Behavior |
|---|---|
| `get_pixel` | Unpack logical RGBA8 via Goal 6.2 |
| `set_pixel` | Pack + replace (no blending) |
| `clear` | Fill entire surface |
| `fill_rect` | Fill a contained rectangle |
| `copy` | Full-surface copy (same dimensions) |
| `copy_rect` | Rectangular blit to a destination origin |

Legacy `robo_gfx_surface_clear_rgba8` calls `clear`.

## Bounds semantics

* Coordinates are surface indices: `0 <= x < width`, `0 <= y < height`.
* `fill_rect` / `copy_rect` require the rectangle to be **fully inside** the
  relevant surface. **No automatic clipping.**
* Empty rect (`width==0` or `height==0`) → success, no-op.
* Negative origins or overflow past edges → `ROBO_INVALID_ARGUMENT`.

## Overlap (same surface)

When source and destination share storage, rows are copied with `memmove` and
vertical direction is chosen so overlapping regions stay correct (same idea as
Chromium’s canvas scroll blit — not a code dependency).

## Format conversion

Same format → bulk row copy. Different implemented formats → per-pixel
`robo_gfx_pixel_convert`. Unsupported pairs → `ROBO_NOT_SUPPORTED`.

## Deferred (later goals)

Lines, paths, transforms, compositing, views/sub-bitmaps, SIMD, GPU.

## Thread safety

Caller must synchronize concurrent mutation of a surface.
