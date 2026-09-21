/*
 * RoboGraphics image output / encoding (Goal 6.13).
 *
 * Pipeline (renderer is not involved):
 *   Surface pixels → format conversion → encoder → destination
 *
 * Canonical pixel source: existing RoboSurface / RoboBitmap accessors.
 * Encoding does not render, scale, filter, or decode.
 *
 * Deterministic: PNG/BMP encoders emit no timestamps or random metadata.
 * PNG uses filter type 0 and zlib store (uncompressed) DEFLATE blocks.
 *
 * See graphics/docs/ImageOutput.md.
 */

#ifndef ROBO_GRAPHICS_ENCODE_H
#define ROBO_GRAPHICS_ENCODE_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsColor.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsImage.h"
#include "RoboGraphics/RoboGraphicsSurface.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Container / file format produced by an encoder. */
typedef enum GfxEncodeContainer {
    RoboGfxEncodePng = 1,
    RoboGfxEncodeBmp = 2
} GfxEncodeContainer;

/**
 * Encoded pixel layout (channel model of the bitstream).
 * RGBA8 preserves alpha. RGB8 is opaque and requires an explicit background
 * when the source format stores alpha (composited with source-over).
 */
typedef enum GfxEncodeLayout {
    RoboGfxEncodeLayoutRgba8 = 1,
    RoboGfxEncodeLayoutRgb8 = 2
} GfxEncodeLayout;

typedef struct GfxEncodeOptions {
    GfxEncodeContainer container;
    GfxEncodeLayout layout;
    /**
     * 1 if @p background is set. Required when layout is RGB8 and the
     * source pixel format stores alpha (RGBA8/BGRA8/ALPHA8).
     */
    int backgroundSet;
    /** Straight RGBA8 backdrop for opaque RGB encoding (src-over). */
    GfxColorRgba8 background;
} GfxEncodeOptions;

/** Heap-owned encoded bytes. Release with GfxEncodedBufferRelease. */
typedef struct GfxEncodedBuffer {
    uint8_t *data;
    size_t size;
} GfxEncodedBuffer;

/**
 * Optional file / IPC boundary: write encoded bytes through a caller sink.
 * RoboGraphics never opens files itself.
 */
typedef Result (*GfxEncodeSinkFn)(const void *bytes,
                                                 size_t length,
                                                 void *user);

void GfxEncodeOptionsInit(GfxEncodeOptions *opts);

/** Free @p buf->data and zero the struct. Safe on empty/null fields. */
void GfxEncodedBufferRelease(GfxEncodedBuffer *buf);

/**
 * Encode the full surface into an in-memory buffer.
 * Does not mutate @p surface.
 */
Result GfxSurfaceEncode(const GfxSurface *surface,
                                      const GfxEncodeOptions *opts,
                                      GfxEncodedBuffer *out);

/**
 * Encode a rectangular region (half-open, must be fully inside the surface).
 * Empty / invalid / outside → RoboInvalidArgument.
 * Does not mutate @p surface.
 */
Result GfxSurfaceEncodeRect(const GfxSurface *surface,
                                           GfxRect region,
                                           const GfxEncodeOptions *opts,
                                           GfxEncodedBuffer *out);

/**
 * Encode a raster image's retained surface (full extent).
 * Empty/external images without pixels → RoboInvalidArgument.
 */
Result GfxImageEncode(GfxImage *image,
                                    const GfxEncodeOptions *opts,
                                    GfxEncodedBuffer *out);

/**
 * Encode then deliver bytes via @p sink (one or more calls).
 * Equivalent to encode + sink(full buffer); does not retain encoded data.
 */
Result GfxSurfaceEncodeToSink(
    const GfxSurface *surface,
    GfxRect region,
    const GfxEncodeOptions *opts,
    GfxEncodeSinkFn sink,
    void *user);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_ENCODE_H */
