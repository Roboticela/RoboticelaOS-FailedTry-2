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

typedef struct GfxLine {
    GfxPoint start;
    GfxPoint end;
} GfxLine;

GfxLine MakeGfxLine(GfxPoint start, GfxPoint end);

/**
 * Draw a one-pixel-wide line with inclusive endpoints.
 * Color is written via Goal 6.2 pack (direct replace; alpha not blended).
 * Completely outside the surface → RoboOk (no-op).
 * Requires write-capable surface access and an implemented pixel format.
 */
Result GfxSurfaceDrawLine(GfxSurface *surface,
                                         GfxPoint start,
                                         GfxPoint end,
                                         GfxColorRgba8 color);

Result GfxSurfaceDrawLineSeg(GfxSurface *surface,
                                             GfxLine line,
                                             GfxColorRgba8 color);

/**
 * Draw a line clipped to @p clip ∩ surface pixel bounds (half-open clip).
 * Same Bresenham as draw_line. Empty / fully outside → RoboOk no-op.
 *
 * When @p composite is 0: direct replace (Goal 6.7 semantics; @p opacity ignored).
 * When @p composite is non-zero: source-over with @p opacity (Goal 6.10).
 * Invalid clip → RoboInvalidArgument.
 */
Result GfxSurfaceDrawLineClipped(GfxSurface *surface,
                                                 GfxPoint start,
                                                 GfxPoint end,
                                                 GfxColorRgba8 color,
                                                 GfxRect clip,
                                                 uint8_t opacity,
                                                 int composite);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_LINE_H */
