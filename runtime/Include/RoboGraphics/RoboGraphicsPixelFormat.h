/*
 * RoboGraphics pixel formats (Goals 6.1–6.2).
 *
 * Byte layouts are explicit memory order (not host uint32_t layout).
 * Implemented (pack/unpack/convert): RGBA8, BGRA8, RGB8, GRAY8, ALPHA8.
 * Architectural placeholders: RGBA16, RGBA_F32 (metadata only).
 *
 * See graphics/docs/PixelFormats.md.
 */

#ifndef ROBO_GRAPHICS_PIXEL_FORMAT_H
#define ROBO_GRAPHICS_PIXEL_FORMAT_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsColor.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum robo_gfx_pixel_format {
    ROBO_GFX_FORMAT_UNKNOWN = 0,
    ROBO_GFX_FORMAT_RGBA8 = 1,    /* R,G,B,A */
    ROBO_GFX_FORMAT_BGRA8 = 2,    /* B,G,R,A */
    ROBO_GFX_FORMAT_RGB8 = 3,     /* R,G,B */
    ROBO_GFX_FORMAT_GRAY8 = 4,    /* Y */
    ROBO_GFX_FORMAT_ALPHA8 = 5,   /* A */
    ROBO_GFX_FORMAT_RGBA16 = 6,   /* architectural — not implemented */
    ROBO_GFX_FORMAT_RGBA_F32 = 7  /* architectural — not implemented */
} robo_gfx_pixel_format_t;

typedef enum robo_gfx_channel_order {
    ROBO_GFX_CHANNELS_UNKNOWN = 0,
    ROBO_GFX_CHANNELS_RGBA = 1,
    ROBO_GFX_CHANNELS_BGRA = 2,
    ROBO_GFX_CHANNELS_RGB = 3,
    ROBO_GFX_CHANNELS_GRAY = 4,
    ROBO_GFX_CHANNELS_ALPHA = 5
} robo_gfx_channel_order_t;

/**
 * Static metadata for a pixel format.
 * Prefer querying this over hardcoding layout elsewhere.
 */
typedef struct robo_gfx_pixel_format_info {
    robo_gfx_pixel_format_t format;
    uint8_t bytes_per_pixel; /* 0 if unknown */
    uint8_t channel_count;
    uint8_t channel_bits; /* bits per channel when uniform; 0 if N/A */
    uint8_t has_alpha;    /* format stores or is an alpha channel */
    uint8_t alpha_stored; /* alpha occupies storage in the pixel */
    uint8_t is_grayscale;
    uint8_t is_float;
    uint8_t is_normalized_int; /* unsigned integer channels, nominal full range */
    uint8_t is_byte_addressable; /* 1 = channels are whole bytes; 0 = packed */
    uint8_t is_implemented; /* pack/unpack/convert supported */
    robo_gfx_channel_order_t order;
    const char *name; /* static string */
} robo_gfx_pixel_format_info_t;

/** Fill @p out with metadata. Unknown/invalid → ROBO_INVALID_ARGUMENT. */
robo_result_t robo_gfx_pixel_format_info(robo_gfx_pixel_format_t format,
                                         robo_gfx_pixel_format_info_t *out);

/** 1 if format has metadata (including architectural placeholders). */
int robo_gfx_pixel_format_is_known(robo_gfx_pixel_format_t format);

/** 1 if pack/unpack/convert are implemented for this format. */
int robo_gfx_pixel_format_is_implemented(robo_gfx_pixel_format_t format);

/**
 * Bytes per pixel. Invalid/unknown → ROBO_INVALID_ARGUMENT.
 * Architectural formats still return their nominal bpp when known.
 */
robo_result_t robo_gfx_pixel_format_bytes_per_pixel(robo_gfx_pixel_format_t format,
                                                    uint8_t *out_bpp);

/**
 * Minimum tightly packed row bytes: width × bpp.
 * width==0 or overflow → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_gfx_pixel_format_min_stride(robo_gfx_pixel_format_t format,
                                               uint32_t width,
                                               size_t *out_stride);

/** Alias of min_stride (row_bytes = width × bytes_per_pixel). */
robo_result_t robo_gfx_pixel_format_row_bytes(robo_gfx_pixel_format_t format,
                                              uint32_t width,
                                              size_t *out_row_bytes);

/**
 * Validate stride for a row of @p width pixels.
 * stride must be >= min_stride. Does not require alignment.
 */
robo_result_t robo_gfx_pixel_format_validate_stride(robo_gfx_pixel_format_t format,
                                                    uint32_t width,
                                                    size_t stride_bytes);

/**
 * Byte size of a rectangular buffer: height × stride.
 * If stride_bytes==0, uses tightly packed min stride.
 * Detects overflow.
 */
robo_result_t robo_gfx_pixel_format_buffer_bytes(robo_gfx_pixel_format_t format,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 size_t stride_bytes,
                                                 size_t *out_bytes);

/**
 * Byte offset of pixel (x,y) within a buffer of the given stride.
 * Requires x < width is NOT checked (caller owns width); validates
 * x*bpp and y*stride overflow and that the pixel fits in one row
 * when stride is validated against a known width via validate_stride.
 *
 * Offset = y * stride + x * bpp.
 */
robo_result_t robo_gfx_pixel_format_pixel_offset(robo_gfx_pixel_format_t format,
                                                 size_t stride_bytes,
                                                 uint32_t x,
                                                 uint32_t y,
                                                 size_t *out_offset);

/**
 * Pack @p color into @p dst according to @p format (explicit channel bytes).
 * Requires implemented format. dst_cap >= bytes_per_pixel.
 *
 * Conversion rules into the packed form use @p color as straight RGBA8:
 *   RGB8     — writes R,G,B (alpha discarded)
 *   GRAY8    — luminance (30R+59G+11B)/100
 *   ALPHA8   — writes A
 *   RGBA8/BGRA8 — channel bytes as documented
 */
robo_result_t robo_gfx_pixel_pack(robo_gfx_pixel_format_t format,
                                  robo_gfx_color_rgba8_t color,
                                  uint8_t *dst,
                                  size_t dst_cap);

/**
 * Unpack explicit channel bytes into straight RGBA8.
 *   RGB8   → A = 255
 *   GRAY8  → R=G=B=Y, A=255
 *   ALPHA8 → R=G=B=0, A=src
 */
robo_result_t robo_gfx_pixel_unpack(robo_gfx_pixel_format_t format,
                                    const uint8_t *src,
                                    size_t src_len,
                                    robo_gfx_color_rgba8_t *out_color);

/**
 * Convert one pixel between implemented formats (via RGBA8 intermediate).
 * Unsupported pair / architectural format → ROBO_NOT_SUPPORTED.
 */
robo_result_t robo_gfx_pixel_convert(robo_gfx_pixel_format_t src_format,
                                     const uint8_t *src,
                                     size_t src_len,
                                     robo_gfx_pixel_format_t dst_format,
                                     uint8_t *dst,
                                     size_t dst_cap);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_PIXEL_FORMAT_H */
