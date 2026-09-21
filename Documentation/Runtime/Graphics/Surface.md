# Surface / Buffer (Goal 6.4)

A **surface** is a rectangular graphics resource used as a draw / raster /
present target. A **framebuffer** (Goal 6.3) is the concrete CPU pixel store.

```text
Framebuffer  →  concrete CPU pixel storage
Surface      →  general graphics resource abstraction
```

Goal 6.4 CPU surfaces are **backed by a framebuffer** (owned or borrowed).
Future surfaces may use other storage (GPU, display, shared) without changing
the public surface API.

No separate public `Buffer` type is exposed — raw CPU storage remains the
framebuffer. Keeping buffer internal avoids an extra layer for CPU-only work.

## Pipeline

```text
RoboGraphics
    ├── Pixel Formats (6.2)
    ├── Framebuffer   (6.3)
    └── Surface       (6.4)  → later bitmap / raster / window
```

## Creation

```c
GfxSurfaceDesc desc;
GfxSurfaceDescInit(&desc);
desc.width = 800;
desc.height = 600;
desc.format = RoboGfxFormatRgba8;
GfxSurfaceCreate(&desc, &surface);
```

Validation, stride, and overflow checks reuse the framebuffer path.

Owned surfaces are zero-filled (same semantics as Goal 6.3).

## Access modes

| Mode | Meaning |
|---|---|
| `READ` | Map for CPU read |
| `WRITE` | Map for CPU write |
| `READ_WRITE` | Map for either / both |

Declared on create. `map()` requests must be allowed by that declaration.

## Mapping

```text
map(access) → GfxSurfaceMap → CPU use → unmap
```

* One active map at a time (`RoboBusy` if already mapped).
* Double `unmap` → `RoboInvalidArgument`.
* After `unmap` / final `release`, map pointers are invalid.
* Final `release` ends an outstanding map safely.

`pixels()` / `row()` remain CPU conveniences; prefer `map`/`unmap` when writing
code that must later support non-CPU backends.

## Addressing

Row and pixel APIs delegate to the framebuffer:

* `row_checked` / `row_const`
* `pixel` / `pixel_const` / `pixel_offset`

Bounds and overflow handling are identical to Goal 6.3.

## Clear

`GfxSurfaceClearRgba8` packs the color with Goal 6.2 helpers and writes
every pixel. Requires write-capable access. No blending / clipping / drawing.

## Ownership

| Kind | Storage |
|---|---|
| OWNED | Framebuffer owns RoboMem pixels |
| BORROWED | Framebuffer borrows caller bytes |

Future (not implemented): shared / external / GPU-backed.

## Thread safety

A surface is **not** concurrently mutable unless the caller synchronizes.
Retain/release and map/unmap are not atomic.

## Errors

Uses `RoboError`: invalid dims/format/access, overflow, OOM, busy map,
invalid unmap, out-of-bounds addressing, clear without write access.
