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
typedef enum robo_gfx_encode_container {
    ROBO_GFX_ENCODE_PNG = 1,
    ROBO_GFX_ENCODE_BMP = 2
} robo_gfx_encode_container_t;

/**
 * Encoded pixel layout (channel model of the bitstream).
 * RGBA8 preserves alpha. RGB8 is opaque and requires an explicit background
 * when the source format stores alpha (composited with source-over).
 */
typedef enum robo_gfx_encode_layout {
    ROBO_GFX_ENCODE_LAYOUT_RGBA8 = 1,
    ROBO_GFX_ENCODE_LAYOUT_RGB8 = 2
} robo_gfx_encode_layout_t;

typedef struct robo_gfx_encode_options {
    robo_gfx_encode_container_t container;
    robo_gfx_encode_layout_t layout;
    /**
     * 1 if @p background is set. Required when layout is RGB8 and the
     * source pixel format stores alpha (RGBA8/BGRA8/ALPHA8).
     */
    int background_set;
    /** Straight RGBA8 backdrop for opaque RGB encoding (src-over). */
    robo_gfx_color_rgba8_t background;
} robo_gfx_encode_options_t;

/** Heap-owned encoded bytes. Release with robo_gfx_encoded_buffer_release. */
typedef struct robo_gfx_encoded_buffer {
    uint8_t *data;
    size_t size;
} robo_gfx_encoded_buffer_t;

/**
 * Optional file / IPC boundary: write encoded bytes through a caller sink.
 * RoboGraphics never opens files itself.
 */
typedef robo_result_t (*robo_gfx_encode_sink_fn)(const void *bytes,
                                                 size_t length,
                                                 void *user);

void robo_gfx_encode_options_init(robo_gfx_encode_options_t *opts);

/** Free @p buf->data and zero the struct. Safe on empty/null fields. */
void robo_gfx_encoded_buffer_release(robo_gfx_encoded_buffer_t *buf);

/**
 * Encode the full surface into an in-memory buffer.
 * Does not mutate @p surface.
 */
robo_result_t robo_gfx_surface_encode(const robo_gfx_surface_t *surface,
                                      const robo_gfx_encode_options_t *opts,
                                      robo_gfx_encoded_buffer_t *out);

/**
 * Encode a rectangular region (half-open, must be fully inside the surface).
 * Empty / invalid / outside → ROBO_INVALID_ARGUMENT.
 * Does not mutate @p surface.
 */
robo_result_t robo_gfx_surface_encode_rect(const robo_gfx_surface_t *surface,
                                           robo_gfx_rect_t region,
                                           const robo_gfx_encode_options_t *opts,
                                           robo_gfx_encoded_buffer_t *out);

/**
 * Encode a raster image's retained surface (full extent).
 * Empty/external images without pixels → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_gfx_image_encode(robo_gfx_image_t *image,
                                    const robo_gfx_encode_options_t *opts,
                                    robo_gfx_encoded_buffer_t *out);

/**
 * Encode then deliver bytes via @p sink (one or more calls).
 * Equivalent to encode + sink(full buffer); does not retain encoded data.
 */
robo_result_t robo_gfx_surface_encode_to_sink(
    const robo_gfx_surface_t *surface,
    robo_gfx_rect_t region,
    const robo_gfx_encode_options_t *opts,
    robo_gfx_encode_sink_fn sink,
    void *user);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_ENCODE_H */
