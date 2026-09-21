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

typedef enum GfxPixelFormat {
    RoboGfxFormatUnknown = 0,
    RoboGfxFormatRgba8 = 1,    /* R,G,B,A */
    RoboGfxFormatBgra8 = 2,    /* B,G,R,A */
    RoboGfxFormatRgb8 = 3,     /* R,G,B */
    RoboGfxFormatGray8 = 4,    /* Y */
    RoboGfxFormatAlpha8 = 5,   /* A */
    RoboGfxFormatRgba16 = 6,   /* architectural — not implemented */
    RoboGfxFormatRgbaF32 = 7  /* architectural — not implemented */
} GfxPixelFormat;

typedef enum GfxChannelOrder {
    RoboGfxChannelsUnknown = 0,
    RoboGfxChannelsRgba = 1,
    RoboGfxChannelsBgra = 2,
    RoboGfxChannelsRgb = 3,
    RoboGfxChannelsGray = 4,
    RoboGfxChannelsAlpha = 5
} GfxChannelOrder;

/**
 * Static metadata for a pixel format.
 * Prefer querying this over hardcoding layout elsewhere.
 */
typedef struct GfxPixelFormatInfo {
    GfxPixelFormat format;
    uint8_t bytesPerPixel; /* 0 if unknown */
    uint8_t channelCount;
    uint8_t channelBits; /* bits per channel when uniform; 0 if N/A */
    uint8_t hasAlpha;    /* format stores or is an alpha channel */
    uint8_t alphaStored; /* alpha occupies storage in the pixel */
    uint8_t isGrayscale;
    uint8_t isFloat;
    uint8_t isNormalizedInt; /* unsigned integer channels, nominal full range */
    uint8_t isByteAddressable; /* 1 = channels are whole bytes; 0 = packed */
    uint8_t isImplemented; /* pack/unpack/convert supported */
    GfxChannelOrder order;
    const char *name; /* static string */
} GfxPixelFormatInfo;

/** Fill @p out with metadata. Unknown/invalid → RoboInvalidArgument. */
Result GetGfxPixelFormatInfo(GfxPixelFormat format,
                                         GfxPixelFormatInfo *out);

/** 1 if format has metadata (including architectural placeholders). */
int GfxPixelFormatIsKnown(GfxPixelFormat format);

/** 1 if pack/unpack/convert are implemented for this format. */
int GfxPixelFormatIsImplemented(GfxPixelFormat format);

/**
 * Bytes per pixel. Invalid/unknown → RoboInvalidArgument.
 * Architectural formats still return their nominal bpp when known.
 */
Result GfxPixelFormatBytesPerPixel(GfxPixelFormat format,
                                                    uint8_t *outBpp);

/**
 * Minimum tightly packed row bytes: width × bpp.
 * width==0 or overflow → RoboInvalidArgument.
 */
Result GfxPixelFormatMinStride(GfxPixelFormat format,
                                               uint32_t width,
                                               size_t *outStride);

/** Alias of minStride (rowBytes = width × bytesPerPixel). */
Result GfxPixelFormatRowBytes(GfxPixelFormat format,
                                              uint32_t width,
                                              size_t *outRowBytes);

/**
 * Validate stride for a row of @p width pixels.
 * stride must be >= minStride. Does not require alignment.
 */
Result GfxPixelFormatValidateStride(GfxPixelFormat format,
                                                    uint32_t width,
                                                    size_t strideBytes);

/**
 * Byte size of a rectangular buffer: height × stride.
 * If strideBytes==0, uses tightly packed min stride.
 * Detects overflow.
 */
Result GfxPixelFormatBufferBytes(GfxPixelFormat format,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 size_t strideBytes,
                                                 size_t *outBytes);

/**
 * Byte offset of pixel (x,y) within a buffer of the given stride.
 * Requires x < width is NOT checked (caller owns width); validates
 * x*bpp and y*stride overflow and that the pixel fits in one row
 * when stride is validated against a known width via validate_stride.
 *
 * Offset = y * stride + x * bpp.
 */
Result GfxPixelFormatPixelOffset(GfxPixelFormat format,
                                                 size_t strideBytes,
                                                 uint32_t x,
                                                 uint32_t y,
                                                 size_t *outOffset);

/**
 * Pack @p color into @p dst according to @p format (explicit channel bytes).
 * Requires implemented format. dstCap >= bytesPerPixel.
 *
 * Conversion rules into the packed form use @p color as straight RGBA8:
 *   RGB8     — writes R,G,B (alpha discarded)
 *   GRAY8    — luminance (30R+59G+11B)/100
 *   ALPHA8   — writes A
 *   RGBA8/BGRA8 — channel bytes as documented
 */
Result GfxPixelPack(GfxPixelFormat format,
                                  GfxColorRgba8 color,
                                  uint8_t *dst,
                                  size_t dstCap);

/**
 * Unpack explicit channel bytes into straight RGBA8.
 *   RGB8   → A = 255
 *   GRAY8  → R=G=B=Y, A=255
 *   ALPHA8 → R=G=B=0, A=src
 */
Result GfxPixelUnpack(GfxPixelFormat format,
                                    const uint8_t *src,
                                    size_t srcLen,
                                    GfxColorRgba8 *outColor);

/**
 * Convert one pixel between implemented formats (via RGBA8 intermediate).
 * Unsupported pair / architectural format → RoboNotSupported.
 */
Result GfxPixelConvert(GfxPixelFormat srcFormat,
                                     const uint8_t *src,
                                     size_t srcLen,
                                     GfxPixelFormat dstFormat,
                                     uint8_t *dst,
                                     size_t dstCap);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_PIXEL_FORMAT_H */
