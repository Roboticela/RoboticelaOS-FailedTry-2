# Backends & Presentation (RoboGraphics)

## Backend kinds

| ID | Kind | Available |
|---|---|---|
| `CPU_RASTER` | raster | yes — Goal 6.11 draw context (`Raster.md`) |
| `GPU` | raster | no (stub) |
| `FRAMEBUFFER_PRESENT` | present | no (QEMU/raw FB path) |
| `NATIVE_WINDOW` | present | no (host OS window path) |

## Boundary

```text
Raster backends produce / mutate surfaces
        ↓
Presentation backends show / export surfaces
```

`robo_gfx_raster_t` is the CPU raster entry point: bind a surface, set clip /
opacity, draw pixels/rects/lines. Path fill/stroke remain stubs.

`robo_gfx_present_null` validates a surface for headless tests without display.

## Forbidden couplings

No Vulkan, OpenGL, Direct3D, Metal, Skia, or Chromium graphics APIs in the
public or internal RoboGraphics tree. Platform code must sit behind these
Roboticela identifiers when added later.
