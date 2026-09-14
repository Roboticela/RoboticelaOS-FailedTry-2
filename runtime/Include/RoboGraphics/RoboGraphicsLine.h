/*
 * RoboGraphics lines (Goal 6.7).
 *
 * One-pixel-wide deterministic integer lines on CPU surfaces.
 * Endpoints are inclusive. Direct pixel replace (no blending / AA / width).
 *
 * Outside geometry is clipped to surface pixel_bounds; fully outside →
 * successful no-op. No Chromium/Skia dependency.
 *
 * See graphics/docs/Lines.md.
 */

#ifndef ROBO_GRAPHICS_LINE_H
#define ROBO_GRAPHICS_LINE_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsColor.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsSurface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_gfx_line {
    robo_gfx_point_t start;
    robo_gfx_point_t end;
} robo_gfx_line_t;

robo_gfx_line_t robo_gfx_line(robo_gfx_point_t start, robo_gfx_point_t end);

/**
 * Draw a one-pixel-wide line with inclusive endpoints.
 * Color is written via Goal 6.2 pack (direct replace; alpha not blended).
 * Completely outside the surface → ROBO_OK (no-op).
 * Requires write-capable surface access and an implemented pixel format.
 */
robo_result_t robo_gfx_surface_draw_line(robo_gfx_surface_t *surface,
                                         robo_gfx_point_t start,
                                         robo_gfx_point_t end,
                                         robo_gfx_color_rgba8_t color);

robo_result_t robo_gfx_surface_draw_line_seg(robo_gfx_surface_t *surface,
                                             robo_gfx_line_t line,
                                             robo_gfx_color_rgba8_t color);

/**
 * Draw a line clipped to @p clip ∩ surface pixel bounds (half-open clip).
 * Same Bresenham as draw_line. Empty / fully outside → ROBO_OK no-op.
 *
 * When @p composite is 0: direct replace (Goal 6.7 semantics; @p opacity ignored).
 * When @p composite is non-zero: source-over with @p opacity (Goal 6.10).
 * Invalid clip → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_gfx_surface_draw_line_clipped(robo_gfx_surface_t *surface,
                                                 robo_gfx_point_t start,
                                                 robo_gfx_point_t end,
                                                 robo_gfx_color_rgba8_t color,
                                                 robo_gfx_rect_t clip,
                                                 uint8_t opacity,
                                                 int composite);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_LINE_H */
