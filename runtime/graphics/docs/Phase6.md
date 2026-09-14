# RoboGraphics Phase 6 — Completion

| Field | Value |
|---|---|
| API | **0.15.0** (`ROBO_GRAPHICS_API_FROZEN=1`) |
| Branding | **RoboGraphics API v1** |
| Goals | **6.1–6.15** complete |

## Implemented components

| Goal | Component |
|---|---|
| 6.1 | Architecture, color, image/frame/backend stubs |
| 6.2 | Pixel formats (RGBA8/BGRA8/RGB8/GRAY8/ALPHA8) |
| 6.3 | Framebuffer (owned/borrowed) |
| 6.4 | Surface + CPU map |
| 6.5 | Bitmap get/set/clear/fill/copy |
| 6.6 | Geometry (points/rects/clip) |
| 6.7 | Bresenham lines |
| 6.8 | Path MoveTo/LineTo/Close |
| 6.9 | 2D affine transforms |
| 6.10 | Source-over alpha compositing |
| 6.11 | CPU raster context |
| 6.12 | RoboFont text/glyph bridge |
| 6.13 | PNG/BMP image encode |
| 6.14 | Screenshot / framebuffer capture |
| 6.15 | Tests, freeze, documentation |

## Dependency direction

```text
RoboBase / RoboMem / RoboError
        ↓
RoboGraphics geometry / pixels / surfaces
        ↓
RoboGraphics raster / composite / paths / transforms
        ↓
RoboGraphics encode / screenshot
```

RoboFont → text bridge → raster (RoboFont does **not** depend on graphics).

## Chromium independence

* No Chromium/Skia headers, libraries, or namespaces in RoboGraphics.
* Build.ps1 `Assert-RoboGraphicsChromiumIndependence` must pass.
* Capabilities report no GPU / native window / framebuffer present.

## Known limitations (post–Phase 6)

* No GPU / Vulkan / GL path
* No window / QEMU / OS display capture (Phase 7)
* Path fill/stroke rasterization not implemented (`ROBO_NOT_SUPPORTED`)
* Text bridge: outline stroke only (no fill/AA/shaping/bidi)
* PNG encode is zlib store (deterministic, larger files)
* FRONT/BACK screenshot sources reserved until present/swap

## Readiness for Phase 7

RoboGraphics is independently usable for:

* CPU drawing into surfaces
* capture → PNG/BMP for visual debugging
* glyph outline stroke via RoboFont

Phase 7 (RoboWindow / QEMU) can present surfaces without redesigning Phase 6.

## Public headers (frozen surface)

`RoboGraphics.h` umbrella plus:

`RoboGraphicsVersion`, `Types`, `Color`, `Geometry`, `PixelFormat`,
`Framebuffer`, `Surface`, `Bitmap`, `Line`, `Path`, `Transform`,
`Composite`, `Raster`, `Text`, `Encode`, `Screenshot`, `Image`, `Frame`,
`Backend`, `Capabilities`.

See [TestStrategy.md](TestStrategy.md) and [architecture.md](architecture.md).
