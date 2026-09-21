# Framebuffer (Goal 6.3)

A **framebuffer** is a rectangular, CPU-addressable 2D pixel store with an
explicit stride. It is the storage foundation for later surface / raster /
RoboWindow / QEMU presentation work.

```text
RoboGraphics
    ↓
Framebuffer   ← this goal
    ↓
Surface / Buffer (6.4+)
    ↓
CPU Raster → Window / QEMU
```

## Properties

| Field | Meaning |
|---|---|
| width / height | Pixel dimensions (non-zero) |
| format | Goal 6.2 pixel format (must be **implemented**) |
| bytes_per_pixel | From pixel-format helpers (not hardcoded) |
| stride | Bytes from start of row *n* to start of row *n+1* |
| byte_size | `stride × height` (overflow-checked) |
| ownership | OWNED (RoboMem) or BORROWED (external) |

Default owned stride = tightly packed minimum row bytes.

## Memory layout

```text
+-------------------------------+
| Row 0                         |
+-------------------------------+
| Row 1                         |
+-------------------------------+
| Row 2                         |
+-------------------------------+
| ...                           |
+-------------------------------+

distance between row starts = stride
pixel (x,y) offset = y × stride + x × bytes_per_pixel
```

## Initialization

Owned framebuffers are **zero-filled** via `RoboCalloc`:

| Format | Zero means |
|---|---|
| RGBA8 / BGRA8 | transparent black (R=G=B=A=0) |
| RGB8 | black |
| GRAY8 | zero intensity |
| ALPHA8 | fully transparent |

## Ownership

```text
create → allocate → use → release → free (owned only)
```

Borrowed framebuffers never free the caller’s buffer. Retain/release are not
atomic.

## Thread safety

A framebuffer is **not** concurrently mutable unless the caller provides
synchronization. Shared retain/release also requires external sync.

## APIs

See `RoboGraphics/RoboGraphicsFramebuffer.h`:

* `GfxFramebufferCreate` / `create_borrowed`
* `retain` / `release`
* property getters
* `row` / `pixel` / `pixel_offset` (bounds-checked)
