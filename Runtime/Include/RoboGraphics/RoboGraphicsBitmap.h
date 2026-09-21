/*
 * RoboGraphics basic bitmap operations (Goals 6.5 / 6.6).
 *
 * Operates on CPU-accessible surfaces from Goal 6.4 — no separate public
 * Bitmap type. Pixel formats / pack-unpack come from Goal 6.2.
 * Geometry uses Goal 6.6 half-open rects / points.
 *
 * Bounds (fill_rect / copy_rect): rectangles must be fully contained in the
 * relevant surface pixel bounds (GfxSurfacePixelBounds). Empty rects
 * (width==0 or height==0) are a successful no-op. No automatic clipping —
 * callers clip with GfxRectClip when needed.
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
 * Surface too large for int32 → RoboInvalidArgument.
 */
Result GfxSurfacePixelBounds(const GfxSurface *surface,
                                            GfxRect *outBounds);

/**
 * Read logical RGBA8 color at (x, y).
 * Coordinates are unsigned surface indices: 0 <= x < width, 0 <= y < height.
 */
Result GfxSurfaceGetPixel(const GfxSurface *surface,
                                         uint32_t x,
                                         uint32_t y,
                                         GfxColorRgba8 *outColor);

/**
 * Write logical RGBA8 color at (x, y) (replace; no blending).
 * Requires write-capable surface access.
 */
Result GfxSurfaceSetPixel(GfxSurface *surface,
                                         uint32_t x,
                                         uint32_t y,
                                         GfxColorRgba8 color);

/**
 * Point overloads: @p p must lie in surface pixel bounds (non-negative storage).
 * Negative geometry points → RoboInvalidArgument.
 */
Result GfxSurfaceGetPixelAt(const GfxSurface *surface,
                                            GfxPoint p,
                                            GfxColorRgba8 *outColor);
Result GfxSurfaceSetPixelAt(GfxSurface *surface,
                                            GfxPoint p,
                                            GfxColorRgba8 color);

/**
 * Fill the entire surface with @p color (same as fill_rect of pixel_bounds).
 * Requires write-capable access. Implemented formats only.
 */
Result GfxSurfaceClear(GfxSurface *surface,
                                     GfxColorRgba8 color);

/**
 * Fill @p rect with @p color.
 * @p rect must be valid and fully enclosed by surface pixel bounds.
 * Empty rect → RoboOk (no-op). Outside / overflow → RoboInvalidArgument.
 */
Result GfxSurfaceFillRect(GfxSurface *surface,
                                         GfxRect rect,
                                         GfxColorRgba8 color);

/**
 * Copy the full @p src surface onto @p dst at (0,0).
 * Surfaces must have identical width, height.
 * Same format → bulk copy; otherwise per-pixel convert (Goal 6.2).
 * Overlapping storage (same pixels) is a no-op success.
 */
Result GfxSurfaceCopy(const GfxSurface *src,
                                    GfxSurface *dst);

/**
 * Copy @p srcRect from @p src to @p dst at @p dstOrigin.
 * Source rect must lie fully in src; destination placement must lie fully
 * in dst. Empty srcRect → RoboOk.
 * Same format → row memmove/memcpy (overlap-safe when same storage).
 * Different formats → per-pixel convert.
 */
Result GfxSurfaceCopyRect(const GfxSurface *src,
                                         GfxRect srcRect,
                                         GfxSurface *dst,
                                         GfxPoint dstOrigin);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_BITMAP_H */
