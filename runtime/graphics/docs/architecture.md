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
