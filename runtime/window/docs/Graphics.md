# RoboWindow + RoboGraphics

```text
Application
    ↓ draw
RoboGraphics Surface / Framebuffer
    ↓ attach (borrow) / pass directly
RoboWindow present API
    ↓ validate + synchronous present (Goal 7.4)
Backend present_surface
    ↓
mock verification  |  future host / QEMU display
```

Goal 7.4 implements validation and a working mock presenter. Windows do not
allocate pixel buffers or redefine formats. See `Present.md`.
