/*
 * RoboGraphics framebuffer (Goal 6.3).
 *
 * A framebuffer is a rectangular, CPU-addressable 2D pixel store with an
 * explicit stride. It is the foundational storage abstraction for later
 * surface / display / QEMU presentation work.
 *
 * Goal 6.3 implements owned (RoboMem) storage and a simple borrowed view.
 * GPU / native / QEMU backends arrive later behind the same ownership model.
 *
 * Pixel-format metadata comes only from Goal 6.2 APIs — do not hardcode bpp.
 *
 * Threading: not concurrently mutable unless the caller synchronizes.
 * Retain/release is not atomic.
 *
 * See graphics/docs/Framebuffer.md.
 */

#ifndef ROBO_GRAPHICS_FRAMEBUFFER_H
#define ROBO_GRAPHICS_FRAMEBUFFER_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboGraphics/RoboGraphicsTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum robo_gfx_framebuffer_ownership {
    ROBO_GFX_FRAMEBUFFER_OWNED = 1,
    ROBO_GFX_FRAMEBUFFER_BORROWED = 2
} robo_gfx_framebuffer_ownership_t;

typedef struct robo_gfx_framebuffer_desc {
    uint32_t width;
    uint32_t height;
    robo_gfx_pixel_format_t format;
    /** 0 → tightly packed minimum row bytes from pixel-format helpers. */
    size_t stride_bytes;
} robo_gfx_framebuffer_desc_t;

typedef struct robo_gfx_framebuffer robo_gfx_framebuffer_t;

void robo_gfx_framebuffer_desc_init(robo_gfx_framebuffer_desc_t *desc);

/**
 * Allocate an owned framebuffer (RoboMem), zero-filled.
 * width/height must be non-zero and within ROBO_GFX_MAX_SURFACE_DIM.
 * format must be implemented (Goal 6.2).
 * Overflow / invalid → ROBO_INVALID_ARGUMENT; OOM → ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_gfx_framebuffer_create(const robo_gfx_framebuffer_desc_t *desc,
                                          robo_gfx_framebuffer_t **out);

/**
 * Borrow external storage. Caller keeps @p pixels alive until last release.
 * stride_bytes must be >= minimum row bytes (0 → tightly packed).
 */
robo_result_t robo_gfx_framebuffer_create_borrowed(
    const robo_gfx_framebuffer_desc_t *desc,
    void *pixels,
    robo_gfx_framebuffer_t **out);

void robo_gfx_framebuffer_retain(robo_gfx_framebuffer_t *fb);
void robo_gfx_framebuffer_release(robo_gfx_framebuffer_t *fb);

robo_gfx_framebuffer_id_t robo_gfx_framebuffer_id(const robo_gfx_framebuffer_t *fb);
robo_gfx_framebuffer_ownership_t robo_gfx_framebuffer_ownership(
    const robo_gfx_framebuffer_t *fb);

uint32_t robo_gfx_framebuffer_width(const robo_gfx_framebuffer_t *fb);
uint32_t robo_gfx_framebuffer_height(const robo_gfx_framebuffer_t *fb);
robo_gfx_pixel_format_t robo_gfx_framebuffer_format(const robo_gfx_framebuffer_t *fb);
uint8_t robo_gfx_framebuffer_bytes_per_pixel(const robo_gfx_framebuffer_t *fb);
size_t robo_gfx_framebuffer_stride(const robo_gfx_framebuffer_t *fb);
size_t robo_gfx_framebuffer_byte_size(const robo_gfx_framebuffer_t *fb);

uint8_t *robo_gfx_framebuffer_pixels(robo_gfx_framebuffer_t *fb);
const uint8_t *robo_gfx_framebuffer_pixels_const(const robo_gfx_framebuffer_t *fb);

/**
 * Pointer to the start of row @p y.
 * Out of range → ROBO_INVALID_ARGUMENT and *out_row = NULL.
 */
robo_result_t robo_gfx_framebuffer_row(robo_gfx_framebuffer_t *fb,
                                       uint32_t y,
                                       uint8_t **out_row);
robo_result_t robo_gfx_framebuffer_row_const(const robo_gfx_framebuffer_t *fb,
                                             uint32_t y,
                                             const uint8_t **out_row);

/**
 * Byte offset of pixel (x,y) from storage base.
 * Bounds-checked against width/height; overflow-safe via pixel-format helpers.
 */
robo_result_t robo_gfx_framebuffer_pixel_offset(const robo_gfx_framebuffer_t *fb,
                                                uint32_t x,
                                                uint32_t y,
                                                size_t *out_offset);

/**
 * Pointer to the first byte of pixel (x,y).
 */
robo_result_t robo_gfx_framebuffer_pixel(robo_gfx_framebuffer_t *fb,
                                         uint32_t x,
                                         uint32_t y,
                                         uint8_t **out_pixel);
robo_result_t robo_gfx_framebuffer_pixel_const(const robo_gfx_framebuffer_t *fb,
                                               uint32_t x,
                                               uint32_t y,
                                               const uint8_t **out_pixel);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_FRAMEBUFFER_H */
