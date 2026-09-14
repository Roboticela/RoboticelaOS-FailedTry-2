/*
 * RoboGraphics surface (Goals 6.1 / 6.4).
 *
 * Surface = general graphics resource (paint / raster / present target).
 * Framebuffer = concrete CPU pixel store (Goal 6.3).
 *
 * CPU-owned and borrowed surfaces are backed by a framebuffer. Future GPU /
 * display / shared surfaces may use other storage without changing this API.
 *
 * Prefer map / unmap for CPU access (explicit lifetime). pixels() / row()
 * remain available as CPU-only conveniences when the surface is not mapped
 * through a foreign backend (always true for Goal 6.4 CPU surfaces).
 *
 * Threading: not concurrently mutable unless the caller synchronizes.
 * Retain/release is not atomic. Map/unmap is not thread-safe.
 *
 * See graphics/docs/Surface.md.
 */

#ifndef ROBO_GRAPHICS_SURFACE_H
#define ROBO_GRAPHICS_SURFACE_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsColor.h"
#include "RoboGraphics/RoboGraphicsFramebuffer.h"
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
    /* Future: SHARED, EXTERNAL, GPU — not implemented in Goal 6.4 */
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

/**
 * CPU mapping view. Valid only between successful map and matching unmap.
 * Do not retain @p pixels / @p pixels_const after unmap or surface release.
 */
typedef struct robo_gfx_surface_map {
    uint8_t *pixels;             /* NULL when access is read-only */
    const uint8_t *pixels_const; /* always set when map succeeds */
    uint32_t width;
    uint32_t height;
    size_t stride_bytes;
    size_t byte_size;
    robo_gfx_pixel_format_t format;
    robo_gfx_surface_access_t access;
} robo_gfx_surface_map_t;

typedef struct robo_gfx_surface robo_gfx_surface_t;

void robo_gfx_surface_desc_init(robo_gfx_surface_desc_t *desc);

/**
 * Allocate an owned CPU surface (framebuffer-backed, zero-filled).
 * Invalid dims/format → ROBO_INVALID_ARGUMENT.
 * Unsupported CPU format → ROBO_NOT_SUPPORTED.
 * OOM → ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_gfx_surface_create(const robo_gfx_surface_desc_t *desc,
                                      robo_gfx_surface_t **out);

/**
 * Borrow external pixels via a borrowed framebuffer.
 * @p pixels must remain valid until last surface release.
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
uint8_t robo_gfx_surface_bytes_per_pixel(const robo_gfx_surface_t *surface);
size_t robo_gfx_surface_stride(const robo_gfx_surface_t *surface);
size_t robo_gfx_surface_byte_size(const robo_gfx_surface_t *surface);
robo_gfx_surface_access_t robo_gfx_surface_access(const robo_gfx_surface_t *surface);

/** 1 if currently mapped for CPU access. */
int robo_gfx_surface_is_mapped(const robo_gfx_surface_t *surface);

/**
 * Begin a CPU mapping. One active map at a time.
 * @p access must be allowed by the surface's declared access mode.
 * Already mapped → ROBO_BUSY.
 */
robo_result_t robo_gfx_surface_map(robo_gfx_surface_t *surface,
                                   robo_gfx_surface_access_t access,
                                   robo_gfx_surface_map_t *out);

/**
 * End the active CPU mapping. Not mapped → ROBO_INVALID_ARGUMENT.
 * After unmap, pointers from the last map must not be used.
 */
robo_result_t robo_gfx_surface_unmap(robo_gfx_surface_t *surface);

/**
 * CPU convenience pointers (no map required for Goal 6.4 CPU surfaces).
 * Prefer map/unmap when preparing code for future non-CPU backends.
 * Mutable pixels() requires write-capable surface access; otherwise NULL.
 */
uint8_t *robo_gfx_surface_pixels(robo_gfx_surface_t *surface);
const uint8_t *robo_gfx_surface_pixels_const(const robo_gfx_surface_t *surface);

/**
 * Pointer to row @p y. NULL if out of range (legacy convenience).
 * Prefer robo_gfx_surface_row_checked for error-aware callers.
 */
uint8_t *robo_gfx_surface_row(robo_gfx_surface_t *surface, uint32_t y);

robo_result_t robo_gfx_surface_row_checked(robo_gfx_surface_t *surface,
                                           uint32_t y,
                                           uint8_t **out_row);
robo_result_t robo_gfx_surface_row_const(const robo_gfx_surface_t *surface,
                                         uint32_t y,
                                         const uint8_t **out_row);

robo_result_t robo_gfx_surface_pixel_offset(const robo_gfx_surface_t *surface,
                                            uint32_t x,
                                            uint32_t y,
                                            size_t *out_offset);
robo_result_t robo_gfx_surface_pixel(robo_gfx_surface_t *surface,
                                     uint32_t x,
                                     uint32_t y,
                                     uint8_t **out_pixel);
robo_result_t robo_gfx_surface_pixel_const(const robo_gfx_surface_t *surface,
                                           uint32_t x,
                                           uint32_t y,
                                           const uint8_t **out_pixel);

/**
 * Fill every pixel using Goal 6.2 pack conversion from @p color.
 * Requires write-capable access. Unsupported format → ROBO_NOT_SUPPORTED.
 */
robo_result_t robo_gfx_surface_clear_rgba8(robo_gfx_surface_t *surface,
                                           robo_gfx_color_rgba8_t color);

/**
 * Borrowed view of the underlying CPU framebuffer (retained by the surface).
 * NULL if the surface has no framebuffer backing (future GPU-only paths).
 * Do not release the returned pointer — owned by the surface.
 */
robo_gfx_framebuffer_t *robo_gfx_surface_framebuffer(robo_gfx_surface_t *surface);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_SURFACE_H */
