/*
 * RoboGraphics basic bitmap operations (Goals 6.5 / 6.6).
 *
 * Operates on CPU-accessible surfaces from Goal 6.4 — no separate public
 * Bitmap type. Pixel formats / pack-unpack come from Goal 6.2.
 * Geometry uses Goal 6.6 half-open rects / points.
 *
 * Bounds (fill_rect / copy_rect): rectangles must be fully contained in the
 * relevant surface pixel bounds (robo_gfx_surface_pixel_bounds). Empty rects
 * (width==0 or height==0) are a successful no-op. No automatic clipping —
 * callers clip with robo_gfx_rect_clip when needed.
 *
 * Overlap: same-surface copy uses direction-aware row copies / memmove
 * (safe for overlapping regions). Different surfaces use memcpy.
 *
 * Threading: same as surfaces — caller synchronizes concurrent mutation.
 *
 * See graphics/docs/Bitmap.md and graphics/docs/Geometry.md.
 */

#ifndef ROBO_GRAPHICS_BITMAP_H
#define ROBO_GRAPHICS_BITMAP_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsColor.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsSurface.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Drawable pixel area as half-open RectI(0, 0, width, height).
 * Surface too large for int32 → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_gfx_surface_pixel_bounds(const robo_gfx_surface_t *surface,
                                            robo_gfx_rect_t *out_bounds);

/**
 * Read logical RGBA8 color at (x, y).
 * Coordinates are unsigned surface indices: 0 <= x < width, 0 <= y < height.
 */
robo_result_t robo_gfx_surface_get_pixel(const robo_gfx_surface_t *surface,
                                         uint32_t x,
                                         uint32_t y,
                                         robo_gfx_color_rgba8_t *out_color);

/**
 * Write logical RGBA8 color at (x, y) (replace; no blending).
 * Requires write-capable surface access.
 */
robo_result_t robo_gfx_surface_set_pixel(robo_gfx_surface_t *surface,
                                         uint32_t x,
                                         uint32_t y,
                                         robo_gfx_color_rgba8_t color);

/**
 * Point overloads: @p p must lie in surface pixel bounds (non-negative storage).
 * Negative geometry points → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_gfx_surface_get_pixel_at(const robo_gfx_surface_t *surface,
                                            robo_gfx_point_t p,
                                            robo_gfx_color_rgba8_t *out_color);
robo_result_t robo_gfx_surface_set_pixel_at(robo_gfx_surface_t *surface,
                                            robo_gfx_point_t p,
                                            robo_gfx_color_rgba8_t color);

/**
 * Fill the entire surface with @p color (same as fill_rect of pixel_bounds).
 * Requires write-capable access. Implemented formats only.
 */
robo_result_t robo_gfx_surface_clear(robo_gfx_surface_t *surface,
                                     robo_gfx_color_rgba8_t color);

/**
 * Fill @p rect with @p color.
 * @p rect must be valid and fully enclosed by surface pixel bounds.
 * Empty rect → ROBO_OK (no-op). Outside / overflow → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_gfx_surface_fill_rect(robo_gfx_surface_t *surface,
                                         robo_gfx_rect_t rect,
                                         robo_gfx_color_rgba8_t color);

/**
 * Copy the full @p src surface onto @p dst at (0,0).
 * Surfaces must have identical width, height.
 * Same format → bulk copy; otherwise per-pixel convert (Goal 6.2).
 * Overlapping storage (same pixels) is a no-op success.
 */
robo_result_t robo_gfx_surface_copy(const robo_gfx_surface_t *src,
                                    robo_gfx_surface_t *dst);

/**
 * Copy @p src_rect from @p src to @p dst at @p dst_origin.
 * Source rect must lie fully in src; destination placement must lie fully
 * in dst. Empty src_rect → ROBO_OK.
 * Same format → row memmove/memcpy (overlap-safe when same storage).
 * Different formats → per-pixel convert.
 */
robo_result_t robo_gfx_surface_copy_rect(const robo_gfx_surface_t *src,
                                         robo_gfx_rect_t src_rect,
                                         robo_gfx_surface_t *dst,
                                         robo_gfx_point_t dst_origin);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_BITMAP_H */
