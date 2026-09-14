# Backends & Presentation (RoboGraphics 6.1)

## Backend kinds

| ID | Kind | Available in 6.1 |
|---|---|---|
| `CPU_RASTER` | raster | yes (foundation; drawing ops arrive later) |
| `GPU` | raster | no (stub) |
| `FRAMEBUFFER_PRESENT` | present | no (QEMU/raw FB path) |
| `NATIVE_WINDOW` | present | no (host OS window path) |

## Boundary

```text
Raster backends produce / mutate surfaces
        ↓
Presentation backends show / export surfaces
```

`robo_gfx_present_null` validates a surface for headless tests without display.

## Forbidden couplings

No Vulkan, OpenGL, Direct3D, Metal, Skia, or Chromium graphics APIs in the
public or internal RoboGraphics tree. Platform code must sit behind these
Roboticela identifiers when added later.
