# RoboWindowServer (Phase 17)

RoboWindowServer is a ring-3 service at `/System/services/window-server`.
It owns window objects, geometry, focus, z-order, per-window event
queues, and surface SHM. It is **not** the future RoboCompositor or
desktop shell.

```text
Application  →  Window SDK  →  SYS_IPC  →  RoboWindowServer
                                              ├── windows / focus / events
                                              └── SYS_IPC → RoboCompositor
                                                              ├── CPU composition
                                                              └── SYS_DISPLAY PRESENT
                                                                     ↓
                                                              RoboGraphics framebuffer
```

## Coordinates

Window `x,y` are **screen coordinates** (display origin at top-left).
Surface pixels are **surface-local** (0,0 is the window top-left).
Input mouse coordinates are screen coordinates.

## Surfaces

v1 uses `SYS_IPC` SHM create/map so the client and the server share the
same physical pages. That is the migration path to a later `MAP_SHARED`
buffer-handle protocol; the public `RoboWindowSurface` width/height/
stride/pixfmt fields stay.

## Input

The kernel Input Manager remains the source of generic events. The
window server claims `SYS_DISPLAY` **input** and pops events, then routes:

* keyboard → focused window
* mouse → topmost visible window containing the pointer (z-order)

Applications never see PS/2 bytes.

The v1 CPU compositor (a separate process) presents damaged rectangles
through `SYS_DISPLAY`. Tiny-only scenes are still composed; the
compositor owns scanout.

## Headless

If no framebuffer is bound, the compositor still listens and Window
Server still creates windows. `SYS_DISPLAY` output claim succeeds in
HEADLESS mode; present is a no-op.

## ABI

Robo Window ABI major 1, minor 0. Headers: `sdk/include/robowindow/`.
Window IDs are opaque integers starting at 1001. Internals are not ABI.

Ownership is the kernel-provided IPC peer pid. Destroying or mapping
another process's window returns `ROBO_EPERM`. Process exit closes the
channel; the server destroys remaining owned windows.
