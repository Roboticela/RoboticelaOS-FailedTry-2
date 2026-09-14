/*
 * RoboGraphics surface (Goal 6.1).
 *
 * A surface is a 2D pixel buffer with explicit ownership:
 *   OWNED    — RoboMem heap pixels; freed on last release
 *   BORROWED — external pixels; caller keeps buffer alive until last release
 *
 * Access is CPU-mapped for Goal 6.1. Future GPU-backed surfaces will use the
 * same public identity without forcing heap ownership.
 *
 * Threading: mutable pixel bytes are thread-confined to the owner context
 * unless an explicit transfer protocol is used (later). Retain/release is
 * not atomic in 6.1 — external sync required for shared refs.
 */

#ifndef ROBO_GRAPHICS_SURFACE_H
#define ROBO_GRAPHICS_SURFACE_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsColor.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboGraphics/RoboGraphicsTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum robo_gfx_surface_ownership {
    ROBO_GFX_SURFACE_OWNED = 1,
    ROBO_GFX_SURFACE_BORROWED = 2
} robo_gfx_surface_ownership_t;

typedef enum robo_gfx_surface_access {
    ROBO_GFX_SURFACE_ACCESS_READ = 1,
    ROBO_GFX_SURFACE_ACCESS_WRITE = 2,
    ROBO_GFX_SURFACE_ACCESS_READ_WRITE = 3
} robo_gfx_surface_access_t;

typedef struct robo_gfx_surface_desc {
    uint32_t width;
    uint32_t height;
    robo_gfx_pixel_format_t format;
    size_t stride_bytes; /* 0 → tightly packed min stride */
    robo_gfx_surface_access_t access;
} robo_gfx_surface_desc_t;

typedef struct robo_gfx_surface robo_gfx_surface_t;

void robo_gfx_surface_desc_init(robo_gfx_surface_desc_t *desc);

/**
 * Allocate an owned CPU surface (zero-filled).
 * Invalid dims/format → ROBO_INVALID_ARGUMENT.
 * Unsupported CPU format → ROBO_NOT_SUPPORTED.
 * OOM → ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_gfx_surface_create(const robo_gfx_surface_desc_t *desc,
                                      robo_gfx_surface_t **out);

/**
 * Borrow external pixels. @p pixels must remain valid until last release.
 * stride_bytes must be >= min stride for width/format.
 */
robo_result_t robo_gfx_surface_create_borrowed(const robo_gfx_surface_desc_t *desc,
                                               void *pixels,
                                               robo_gfx_surface_t **out);

void robo_gfx_surface_retain(robo_gfx_surface_t *surface);
void robo_gfx_surface_release(robo_gfx_surface_t *surface);

robo_gfx_surface_id_t robo_gfx_surface_id(const robo_gfx_surface_t *surface);
robo_gfx_surface_ownership_t robo_gfx_surface_ownership(
    const robo_gfx_surface_t *surface);
uint32_t robo_gfx_surface_width(const robo_gfx_surface_t *surface);
uint32_t robo_gfx_surface_height(const robo_gfx_surface_t *surface);
robo_gfx_size_t robo_gfx_surface_size(const robo_gfx_surface_t *surface);
robo_gfx_pixel_format_t robo_gfx_surface_format(const robo_gfx_surface_t *surface);
size_t robo_gfx_surface_stride(const robo_gfx_surface_t *surface);
robo_gfx_surface_access_t robo_gfx_surface_access(const robo_gfx_surface_t *surface);

/**
 * Mutable CPU pointer. NULL if surface is null or access is read-only borrow
 * that was created without write (Goal 6.1 always returns pixels when present).
 */
uint8_t *robo_gfx_surface_pixels(robo_gfx_surface_t *surface);
const uint8_t *robo_gfx_surface_pixels_const(const robo_gfx_surface_t *surface);

/** Byte size of the pixel storage (stride * height). */
size_t robo_gfx_surface_byte_size(const robo_gfx_surface_t *surface);

/**
 * Pointer to row @p y. Returns NULL if out of range.
 */
uint8_t *robo_gfx_surface_row(robo_gfx_surface_t *surface, uint32_t y);

robo_result_t robo_gfx_surface_clear_rgba8(robo_gfx_surface_t *surface,
                                           robo_gfx_color_rgba8_t color);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_SURFACE_H */
