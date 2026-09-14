# RoboGraphics

Independent graphics subsystem for Roboticela Runtime (**Goals 6.1–6.15**).

**Version:** 0.15.0 — **RoboGraphics API v1 frozen** (`ROBO_GRAPHICS_API_FROZEN=1`)

RoboGraphics must never depend on Chromium headers/libraries, Skia, Vulkan,
OpenGL, Direct3D, Metal, or OS window APIs. Chromium may be consulted as a
temporary technical reference only.

## Layout

| Path | Role |
|---|---|
| `runtime/Include/RoboGraphics/` | Public API |
| `runtime/Source/RoboGraphics/` | Implementation |
| `runtime/graphics/tests/` | Unit + integration tests |
| `runtime/graphics/docs/` | Architecture docs |

## Pipeline

```text
RoboGraphics API
      │
      ├── Geometry / Color     (6.6 — pixels, rects, clip)
      ├── Pixel Formats       (6.2 — RGBA8/BGRA8/RGB8/Gray8/Alpha8)
      ├── Framebuffer         (6.3 — owned/borrowed 2D pixel store)
      ├── Surface             (6.4 — resource + map/access)
      ├── Bitmap ops          (6.5 — get/set/clear/fill/copy)
      ├── Lines               (6.7 — one-pixel Bresenham)
      ├── Paths               (6.8 — MoveTo/LineTo/Close model)
      ├── Transforms          (6.9 — 2D affine)
      ├── Alpha compositing   (6.10 — source-over)
      ├── CPU raster          (6.11 — immediate draw context)
      ├── Text bridge         (6.12 — glyph → path / outline stroke)
      ├── Image output        (6.13 — PNG/BMP encode)
      ├── Screenshot          (6.14 — surface/FB capture → encode)
      ├── Tests / API freeze  (6.15)
      ├── Image / Frame
      └── Presentation → RoboWindow / QEMU (Phase 7)
```

## Goal status

| Goal | Scope |
|---|---|
| **6.1–6.15** | Complete — see [Phase6.md](docs/Phase6.md) |

## Docs

* [Phase6.md](docs/Phase6.md) — completion summary
* [ApiFreeze.md](docs/ApiFreeze.md) — freeze contract
* [TestStrategy.md](docs/TestStrategy.md) — test hierarchy
* [architecture.md](docs/architecture.md)
* [PixelFormats.md](docs/PixelFormats.md)
* [Framebuffer.md](docs/Framebuffer.md)
* [Surface.md](docs/Surface.md)
* [Bitmap.md](docs/Bitmap.md)
* [Geometry.md](docs/Geometry.md)
* [Lines.md](docs/Lines.md)
* [Paths.md](docs/Paths.md)
* [Transforms.md](docs/Transforms.md)
* [Compositing.md](docs/Compositing.md)
* [Raster.md](docs/Raster.md)
* [TextBridge.md](docs/TextBridge.md)
* [ImageOutput.md](docs/ImageOutput.md)
* [Screenshot.md](docs/Screenshot.md)
* [coordinates.md](docs/coordinates.md)
* [ownership.md](docs/ownership.md)
* [threading.md](docs/threading.md)
* [backend.md](docs/backend.md)

## Tests

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test -Filter "graphics."
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Sanitize -Filter "graphics."
```
