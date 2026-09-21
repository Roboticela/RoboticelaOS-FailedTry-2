# Pixel Formats (Goal 6.2)

RoboGraphics pixel formats define **explicit byte layouts**. They do not use host
`uint32_t` memory order to describe channels.

## Supported (implemented)

| Format | Channels | Bytes/Pixel | Alpha | Layout (memory order) |
|---|---|---|---|---|
| `RGBA8` | RGBA | 4 | Yes | R G B A |
| `BGRA8` | BGRA | 4 | Yes | B G R A |
| `RGB8` | RGB | 3 | No | R G B |
| `GRAY8` | Gray | 1 | No | Y |
| `ALPHA8` | Alpha | 1 | Yes | A |

## Architectural (metadata only)

| Format | Notes |
|---|---|
| `RGBA16` | Known bpp/channels; pack/convert → `RoboNotSupported` |
| `RGBA_F32` | Known bpp/channels; pack/convert → `RoboNotSupported` |

## Alpha semantics

Straight (unassociated) alpha, consistent with Goal 6.1 colors:

* `0` = fully transparent
* `255` = fully opaque

### Conversion rules

| From → To | Rule |
|---|---|
| RGBA8 ↔ BGRA8 | Swap R and B; G and A unchanged |
| RGBA8 → RGB8 | Drop A |
| RGB8 → RGBA8 | A = 255 |
| RGBA8 → GRAY8 | Y = (30R + 59G + 11B) / 100 |
| GRAY8 → RGBA8 | R=G=B=Y, A=255 |
| RGBA8 → ALPHA8 | Keep A |
| ALPHA8 → RGBA8 | R=G=B=0, A=src |

## Addressing

```text
row_bytes  = width × bytes_per_pixel   (tight pack)
offset     = y × stride + x × bytes_per_pixel
buffer     = height × stride
```

All helpers detect integer overflow and return `RoboInvalidArgument`.
Stride must be ≥ minimum row bytes.

## Portability

Channel bytes are written/read individually. Do not cast a pixel to `uint32_t`
and assume R/G/B/A positions.

## APIs

See `RoboGraphics/RoboGraphicsPixelFormat.h`:

* `GfxPixelFormatInfo` / `bytes_per_pixel` / `is_known` / `is_implemented`
* `min_stride` / `row_bytes` / `validate_stride` / `buffer_bytes` / `pixel_offset`
* `pixel_pack` / `pixel_unpack` / `pixel_convert`
