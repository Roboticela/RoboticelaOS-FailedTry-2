# Image Output (Goal 6.13)

Image output serializes **already-rendered** RoboGraphics pixels. It does not
draw, scale, filter, or decode images.

## Architecture

```text
RoboSurface / RoboImage (raster)
        │
        ▼
 Pixel sample (get_pixel → straight RGBA8)
        │
        ├── layout RGBA8  → preserve alpha
        └── layout RGB8   → composite over explicit background if source stores alpha
        │
        ▼
 Encoder (PNG | BMP)
        │
        ├── memory buffer (robo_gfx_encoded_buffer_t)
        └── sink callback (optional file / IPC boundary)
```

The raster / composite / text systems never own file-format details. New
containers extend `robo_gfx_encode_container_t` without changing the CPU
raster backend.

Canonical pixel source remains **Surface / Bitmap** — there is no second
bitmap model.

## Supported containers

| Container | Layouts | Notes |
|---|---|---|
| **PNG** | RGB8, RGBA8 | Filter 0 (None); zlib **store** DEFLATE (deterministic) |
| **BMP** | RGB8 → 24-bpp BGR; RGBA8 → 32-bpp BI_BITFIELDS BGRA | Bottom-up rows; 24-bpp rows padded to 4 bytes |

JPEG / WebP / AVIF / animated formats are out of scope for 6.13.

## Pixel-format conversion

Source formats use Goal 6.2 unpack (`get_pixel`) into straight RGBA8:

* Channel order is never silently swapped in the logical color.
* RGB8 / Gray8 sources unpack with A=255.
* Unsupported / architectural formats → `ROBO_NOT_SUPPORTED`.

Encoded PNG pixels are written as R,G,B[,A] bytes. BMP pixels use B,G,R[,A]
as required by the DIB layout (documented, not an accidental swap of the
logical color).

## Alpha behavior

* **RGBA8 layout:** alpha is preserved (including 0 and partial).
* **RGB8 layout:** opaque output. If the source format **stores** alpha
  (RGBA8 / BGRA8 / ALPHA8), `background_set` must be 1 and each pixel is
  composited with source-over over `background` (Goal 6.10). Missing
  background → `ROBO_INVALID_ARGUMENT`. Alpha is never silently discarded.

## Determinism

Repeated encoding of the same pixels with the same options yields identical
bytes. Encoders emit no timestamps, random IDs, or nondeterministic metadata.
PNG compression is store-only (no Huffman/LZ variability).

## Region export

`robo_gfx_surface_encode_rect` requires a non-empty half-open rectangle fully
inside the surface pixel bounds (same enclosure rule as fill/copy). Outside,
negative, or empty regions → `ROBO_INVALID_ARGUMENT`. The source surface is
not mutated.

`robo_gfx_surface_encode` encodes the full surface bounds.

## Memory output

```c
robo_gfx_encoded_buffer_t buf;
robo_gfx_surface_encode(surface, &opts, &buf);
/* buf.data / buf.size */
robo_gfx_encoded_buffer_release(&buf);
```

No filesystem access is required.

## File / sink boundary

`robo_gfx_surface_encode_to_sink` encodes then writes bytes through a caller
sink. RoboGraphics does not open platform files. Callers may route the sink to
an existing filesystem abstraction.

## Errors

| Condition | Result |
|---|---|
| NULL args / invalid options | `ROBO_INVALID_ARGUMENT` |
| Empty / outside region | `ROBO_INVALID_ARGUMENT` |
| RGB8 layout without background on alpha-storing source | `ROBO_INVALID_ARGUMENT` |
| Unimplemented source format | `ROBO_NOT_SUPPORTED` |
| Allocation failure | `ROBO_OUT_OF_MEMORY` |
| Dimension / size overflow | `ROBO_INVALID_ARGUMENT` |

## Limitations (6.13)

* No image decoding.
* No ICC / color management / HDR.
* PNG is uncompressed (store zlib) — larger files, deterministic.
* No GPU export or window/framebuffer capture (later goals).
