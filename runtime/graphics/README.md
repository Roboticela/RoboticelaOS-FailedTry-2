# RoboGraphics

Independent graphics subsystem for Roboticela Runtime (**Goals 6.1–6.3**).

**Version:** 0.3.0 (API not frozen)

RoboGraphics must never depend on Chromium headers/libraries, Skia, Vulkan,
OpenGL, Direct3D, Metal, or OS window APIs. Chromium may be consulted as a
temporary technical reference only.

## Layout

| Path | Role |
|---|---|
| `runtime/Include/RoboGraphics/` | Public API |
| `runtime/Source/RoboGraphics/` | Implementation |
| `runtime/graphics/tests/` | Unit + smoke tests |
| `runtime/graphics/docs/` | Architecture docs |

## Pipeline

```text
RoboGraphics API
      │
      ├── Geometry / Color
      ├── Pixel Formats   (6.2 — RGBA8/BGRA8/RGB8/Gray8/Alpha8)
      ├── Framebuffer     (6.3 — owned/borrowed 2D pixel store)
      ├── Surface / Image / Frame
      ├── CPU Raster (next)
      └── Presentation → RoboWindow / QEMU
```

## Goal status

| Goal | Scope |
|---|---|
| **6.1** | Architecture foundation |
| **6.2** | Pixel formats, addressing, conversion |
| **6.3** | Framebuffer storage & addressing |
| **6.4+** | Surface depth, bitmap ops, … |

## Docs

* [architecture.md](docs/architecture.md)
* [PixelFormats.md](docs/PixelFormats.md)
* [Framebuffer.md](docs/Framebuffer.md)
* [coordinates.md](docs/coordinates.md)
* [ownership.md](docs/ownership.md)
* [threading.md](docs/threading.md)
* [backend.md](docs/backend.md)

## Tests

```powershell
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test -Filter "graphics."
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter "graphics.fb"
```
