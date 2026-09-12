# RoboCompositor (Phase 18)

RoboCompositor is a ring-3 service at `/System/services/compositor`.
It owns visual layers, z-order, clipping, opacity, damage, CPU
composition, and presentation. It is **not** RoboWindowServer and
**not** a desktop shell.

```text
Application  →  Window SDK  →  RoboWindowServer  →  SYS_IPC  →  RoboCompositor
                                                              ├── CPU backend
                                                              └── RoboOutput
                                                                     ↓
                                                              SYS_DISPLAY PRESENT
                                                                     ↓
                                                              RoboGraphics
```

## Pixel format

Stored `uint32` little-endian, matching RoboGraphics:

```text
pixfmt 0  BGRX8888  byte0=R  byte1=G  byte2=B  byte3=A
pixfmt 1  RGBX8888  byte0=B  byte1=G  byte2=R  byte3=A
```

Logical colour is `0xAARRGGBB`. The compositor does not silently swap
channels. Scanout is opaque; alpha is used only while blending.

## CPU backend

`user/lib/compose.c` is the CPU composition core (no syscalls). A
`RoboCompositionBackend` vtable (`initialize` / `compose` / `present` /
`shutdown`) is the swap point for a future GPU backend. Applications
and the Window API do not change.

## Output / double buffering

Two offscreen buffers (back/front) live in the compositor process.
Composition writes the back buffer; the damaged rectangle is copied to
the front buffer and then to the UEFI framebuffer via `SYS_DISPLAY
PRESENT` with `dest_x`/`dest_y`. There is **no hardware page flip**.

The output object is not hard-coded to a single monitor. v1 uses one
output.

## Damage

A single merged rectangle is maintained. Move/resize/hide/destroy
damage both the old and new geometry. Incremental composition fills
only that rectangle, then blits intersecting layers in z-order.
Correctness may expand the rectangle to a bounding box.

## Security

Only the first IPC client (Window Server, by init order) may mutate
layers. Applications cannot talk to the compositor port for
composition. Window IDs and SHM handles are validated. Surface pages
are mapped SHM owned by Window Server.

## Headless

If `SYS_DISPLAY` output claim finds no framebuffer, the compositor
keeps a 64×48 offscreen target. Host tests (`test_compositor.exe`) run
the same CPU core with no display.

## Init order

```text
compositor → window-server → window-test → compositor-test
```

Window Server claims `SYS_DISPLAY` **input**. Compositor claims
**output**.
