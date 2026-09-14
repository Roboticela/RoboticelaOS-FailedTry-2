# RoboGraphics Architecture (Goal 6.1)

## Core modules

```text
RoboGraphics API
      │
      ├── Geometry     points, sizes, rects, clip, affine (6.6)
      ├── Color        RGBA8 / float foundation
      ├── PixelFormat  byte layouts, pack/unpack, convert (6.2)
      ├── Framebuffer  owned / borrowed 2D pixel store (6.3)
      ├── Surface      resource abstraction + CPU map (6.4)
      ├── Bitmap       get/set/clear/fill/copy on surfaces (6.5)
      ├── Line         one-pixel Bresenham + clip (6.7)
      ├── Path         MoveTo / LineTo / Close command model (6.8)
      ├── Transform    2D affine compose / invert / map (6.9)
      ├── Composite    source-over alpha blend (6.10)
      ├── Raster       CPU immediate draw context (6.11)
      ├── Text         glyph bridge to RoboFont (6.12)
      ├── Encode       PNG/BMP image output (6.13)
      ├── Screenshot   surface/FB capture + export (6.14)
      ├── Tests        Phase 6 validation / API freeze (6.15)
      ├── Image        format-agnostic image container
      ├── Frame        finished-raster metadata
      ├── Backend      raster vs present identifiers
      └── Capabilities query what is available now
```

## Future consumer pipeline

```text
RoboHTML → RoboCSS → RoboLayout → RoboPaint
                    ↓
               RoboGraphics
                    ↓
               Surface → Framebuffer (CPU) / future GPU
                    ↓
          RoboWindow / QEMU presentation
```

## Raster vs presentation

Goal 6.3 framebuffers are the explicit CPU pixel store with stride and row/pixel
addressing. Goal 6.4 surfaces wrap that store as a general resource with access
modes and map/unmap. Goal 6.5 adds deterministic bitmap ops (pixel, clear,
fill_rect, copy). Goal 6.6 completes overflow-safe geometry (points, sizes,
half-open rects, intersect/clip/translate) and `pixel_bounds` for bitmap ops.
Goal 6.7 adds inclusive one-pixel Bresenham lines with bounds clipping.
Goal 6.8 adds the owned path command model (MoveTo / LineTo / Close) with
iteration and exact bounds — no fill/stroke/rasterization yet.
Goal 6.9 completes 2D affine transforms (compose order, inverse, map
point/rect/path) on `robo_gfx_affine_t`.
Goal 6.10 adds straight-alpha source-over compositing (pixel + clipped
surface blit; same-storage snapshot for self-composite).
Goal 6.11 adds the CPU raster context (`robo_gfx_raster_t`): clip + opacity
state, immediate pixel/rect/line draws into a surface, path fill/stroke
stubs as the integration boundary.
Goal 6.12 bridges RoboFont glyphs into graphics: font↔device coordinates,
scaled metrics, outline→path conversion, and outline-stroke draw through
the raster (no shaping / fill / AA).
Goal 6.13 encodes surface pixels to PNG/BMP (memory or sink) without
rendering — format conversion + deterministic encoders only.
Goal 6.14 captures an immutable owned snapshot of a surface or framebuffer
(region STRICT/CLIP) and exports via 6.13; front/back buffers reserved.
Goal 6.15 freezes RoboGraphics API v1 (0.15.0), consolidates tests, and
documents Phase 6 completion — no new raster features.
Presentation (`robo_gfx_present_*`, future window/QEMU)
consumes surfaces.
The same buffer path may be used for:

* headless tests
* screenshots
* CPU framebuffer
* QEMU display
* native OS window

## Design priorities

Goal 6.1 deliberately stays small so later goals can reach **visible pixels**
quickly. Prefer explicit types and ownership over premature standards completeness.
