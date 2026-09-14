/*
 * RoboGraphics alpha compositing (Goal 6.10).
 *
 * Canonical storage: straight (unassociated) RGBA8 — same as Goal 6.1 colors.
 * Blend math converts to premultiplied intermediates, then back to straight.
 *
 * Operator: source-over only (Porter–Duff SrcOver).
 *   out_a   = sa + da * (1 - sa)
 *   out_rgb = (src_rgb * sa + dst_rgb * da * (1 - sa)) / out_a   (when out_a>0)
 *
 * Global opacity (0..255) multiplies source alpha before compositing;
 * source RGB channels are unchanged (straight-alpha CSS-style opacity).
 *
 * See graphics/docs/Compositing.md.
 */

#ifndef ROBO_GRAPHICS_COMPOSITE_H
#define ROBO_GRAPHICS_COMPOSITE_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsColor.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsSurface.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Rounded (a*b)/255 for 8-bit channels.
 * (a*b + 127) / 255 — deterministic, no overflow (max 255*255+127 fits uint16).
 */
uint8_t robo_gfx_mul_div255(uint8_t a, uint8_t b);

/**
 * Straight → premultiplied (RGB *= A / 255; A unchanged).
 * Transparent → RGB forced to 0.
 */
robo_gfx_color_rgba8_t robo_gfx_color_premultiply(robo_gfx_color_rgba8_t straight);

/**
 * Premultiplied → straight (RGB = RGB * 255 / A when A>0; else RGB=0).
 * Input RGB must be premultiplied scaled values (0..A nominally).
 */
robo_gfx_color_rgba8_t robo_gfx_color_unpremultiply(robo_gfx_color_rgba8_t premul);

/**
 * Source-over: @p src OVER @p dst (both straight RGBA8).
 * Transparent source → @p dst. Opaque source → @p src.
 */
robo_gfx_color_rgba8_t robo_gfx_composite_src_over(robo_gfx_color_rgba8_t src,
                                                   robo_gfx_color_rgba8_t dst);

/**
 * Source-over with global source opacity (0 = invisible, 255 = full).
 * Effective source alpha = mul_div255(src.a, opacity); RGB unchanged.
 */
robo_gfx_color_rgba8_t robo_gfx_composite_src_over_opacity(
    robo_gfx_color_rgba8_t src,
    robo_gfx_color_rgba8_t dst,
    uint8_t opacity);

/**
 * Composite @p src onto destination pixel (x,y) with source-over.
 * Requires write-capable destination. Out of bounds → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_gfx_surface_composite_pixel(robo_gfx_surface_t *dst,
                                               uint32_t x,
                                               uint32_t y,
                                               robo_gfx_color_rgba8_t src,
                                               uint8_t opacity);

/**
 * Composite @p src_rect from @p src onto @p dst at @p dst_origin.
 *
 * Clips to source and destination pixel bounds (negative origins OK).
 * Empty / fully outside → ROBO_OK no-op.
 * No scaling, filtering, or transforms.
 *
 * Same underlying storage: source region is snapshotted before blending so
 * overlapping self-composites do not read partially written pixels.
 *
 * Formats may differ (via straight RGBA8). Implemented formats only.
 * Destination must be write-capable; source readable.
 */
robo_result_t robo_gfx_surface_composite_rect(const robo_gfx_surface_t *src,
                                              robo_gfx_rect_t src_rect,
                                              robo_gfx_surface_t *dst,
                                              robo_gfx_point_t dst_origin,
                                              uint8_t opacity);

/**
 * Composite the full source surface onto @p dst at @p dst_origin.
 * Equivalent to composite_rect of the full source pixel bounds.
 */
robo_result_t robo_gfx_surface_composite(const robo_gfx_surface_t *src,
                                         robo_gfx_surface_t *dst,
                                         robo_gfx_point_t dst_origin,
                                         uint8_t opacity);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_COMPOSITE_H */
