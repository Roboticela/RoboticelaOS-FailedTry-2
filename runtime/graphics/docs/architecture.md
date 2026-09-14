# RoboGraphics Architecture (Goal 6.1)

## Core modules

```text
RoboGraphics API
      │
      ├── Geometry     points, sizes, rects, affine
      ├── Color        RGBA8 / float foundation
      ├── PixelFormat  byte layouts, pack/unpack, convert (6.2)
      ├── Framebuffer  owned / borrowed 2D pixel store (6.3)
      ├── Surface      owned / borrowed pixel buffers
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
               Framebuffer → Surface (6.4+)
                    ↓
          RoboWindow / QEMU presentation
```

## Raster vs presentation

Goal 6.3 framebuffers are the explicit CPU pixel store with stride and row/pixel
addressing. Surfaces (6.1 foundation; deepened in 6.4+) and presentation
(`robo_gfx_present_*`, future window/QEMU) build on that store.
The same buffer path may be used for:

* headless tests
* screenshots
* CPU framebuffer
* QEMU display
* native OS window

## Design priorities

Goal 6.1 deliberately stays small so later goals can reach **visible pixels**
quickly. Prefer explicit types and ownership over premature standards completeness.
