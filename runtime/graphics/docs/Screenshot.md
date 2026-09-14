# Screenshot / Framebuffer Export (Goal 6.14)

Graphics-surface capture for debugging, visual tests, and later presentation
pipelines. **Not** a native window, desktop, or QEMU screenshot API.

## Architecture

```text
Source (surface | framebuffer | future front/back)
        │
        ▼
 Region resolve (full | STRICT | CLIP)
        │
        ▼
 Owned pixel snapshot (RoboSurface copy)
        │
        ├── RoboImage wrapper (optional)
        └── Phase 6.13 encode → memory / sink
```

Capture owns pixel selection and immutability. Encoding is always delegated
to Goal 6.13 (`RoboGraphicsEncode.h`). No second PNG/BMP implementation.

## Capture sources

| Source | Behavior (6.14) |
|---|---|
| `ROBO_GFX_CAPTURE_SURFACE` | Caller-selected surface (current CPU pixels) |
| `ROBO_GFX_CAPTURE_FRAMEBUFFER` | Caller-selected framebuffer |
| `ROBO_GFX_CAPTURE_FRONT_BUFFER` | Reserved → `ROBO_NOT_SUPPORTED` |
| `ROBO_GFX_CAPTURE_BACK_BUFFER` | Reserved → `ROBO_NOT_SUPPORTED` |

There is **no** double-buffer / present swap chain yet. Capture always reads
the explicit buffer the caller supplies — never “guesses” front vs back.

When present/QEMU arrives, FRONT/BACK will map to the presented vs render
target; until then callers must pass SURFACE or FRAMEBUFFER.

## Region semantics

`robo_gfx_capture_options_t`:

* `use_full_extent = 1` (default): capture entire source bounds.
* Otherwise apply `region` with:

| Policy | Behavior |
|---|---|
| **STRICT** | Region must be non-empty and fully enclosed; else `ROBO_INVALID_ARGUMENT` |
| **CLIP** (default when not full) | Intersect with source bounds; empty after clip → `ROBO_INVALID_ARGUMENT` |

Negative sizes are invalid. Partial out-of-bounds with CLIP yields the
intersection only (deterministic).

## Pixel fidelity

Snapshot copies exact packed pixel bytes for the source format (via
`robo_gfx_surface_copy_rect`). No scaling, filtering, color correction, or
compositing during capture.

Stride/padding outside the active width is not required in the snapshot
(tight packing is fine); visible pixels match the source.

## Alpha

Capture preserves the source pixel format and alpha bits.

Opaque export (RGB layout) uses Phase 6.13 rules: explicit `background_set`
when the source format stores alpha. Capture itself never flattens alpha.

## Synchronization

Surfaces/framebuffers are **not** concurrently mutable unless the caller
synchronizes (Goal 6.4). Capture runs synchronously on the calling thread
and memcpys rows while it holds the source pointers.

Contract: do not mutate the source during `screenshot_capture_*`. There is
no compositor fence or multithreaded render lock in 6.14.

## Immutable capture

The screenshot retains an **owned** surface independent of the source.
After capture returns, modifying the source must not change screenshot
pixels. Verified by tests.

## Encoding integration

```c
robo_gfx_screenshot_export(shot, &encode_opts, &buf);
robo_gfx_surface_export_screenshot(surface, &cap, &enc, &buf);
```

Uses 6.13 memory encode / sink helpers. Deterministic when 6.13 is.

## Errors

| Condition | Result |
|---|---|
| NULL / missing source for kind | `ROBO_INVALID_ARGUMENT` |
| Empty / invalid / unresolvable region | `ROBO_INVALID_ARGUMENT` |
| Zero-size source / missing pixels | `ROBO_INVALID_ARGUMENT` |
| Unimplemented format | `ROBO_NOT_SUPPORTED` |
| FRONT/BACK buffer | `ROBO_NOT_SUPPORTED` |
| OOM | `ROBO_OUT_OF_MEMORY` |

## Limitations

* No OS window / monitor / QEMU capture
* No GPU readback
* No front/back swap-chain capture yet
* No video / recording
* No decoding, scaling, or color management
