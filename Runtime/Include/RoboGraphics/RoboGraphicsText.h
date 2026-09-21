/*
 * RoboGraphics ↔ RoboFont text rendering bridge (Goal 6.12).
 *
 * Glyph-centric integration boundary: RoboGraphics does not shape, segment,
 * bidi, or fall back fonts. RoboText supplies glyph IDs + placement later.
 *
 * Font design space: origin on baseline, +X right, +Y up (OpenType).
 * RoboGraphics device space: origin top-left, +X right, +Y down.
 *
 *   device_x = originX + (fontX / upem) * sizePx
 *   device_y = originY - (fontY / upem) * sizePx
 *
 * Goal 6.12 draw mode strokes outlines with 1-pixel lines (no fill / AA).
 * Mask / alpha / color glyph modes are reserved for later goals.
 *
 * See graphics/docs/TextBridge.md.
 */

#ifndef ROBO_GRAPHICS_TEXT_H
#define ROBO_GRAPHICS_TEXT_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontMetrics.h"
#include "RoboFont/RoboFontOutline.h"
#include "RoboFont/RoboFontTypes.h"
#include "RoboGraphics/RoboGraphicsColor.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsPath.h"
#include "RoboGraphics/RoboGraphicsRaster.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Future / current glyph paint modes.
 * OUTLINE_STROKE is the only implemented paint path in Goal 6.12.
 */
typedef enum GfxGlyphRenderMode {
    RoboGfxGlyphModeOutlineStroke = 1, /* 1px outline stroke */
    RoboGfxGlyphModeMask = 2,           /* future monochrome coverage */
    RoboGfxGlyphModeAlpha = 3,          /* future grayscale AA */
    RoboGfxGlyphModeColor = 4           /* future COLR/bitmap/SVG */
} GfxGlyphRenderMode;

/**
 * Glyph draw request (absolute baseline origin in device space).
 * @p sizePx is CSS-like pixel size (must be finite and > 0).
 * Opacity comes from the raster context.
 */
typedef struct GfxGlyphDraw {
    FontFace *face;
    GlyphId glyphId;
    float sizePx;
    GfxPointF origin; /* baseline origin (device, +Y down) */
    GfxColorRgba8 color;
    GfxGlyphRenderMode mode; /* 0 → OUTLINE_STROKE */
} GfxGlyphDraw;

/**
 * Scaled glyph metrics in device pixels relative to baseline origin (0,0).
 * Bounds use RoboGraphics Y-down half-open semantics once snapped to ints
 * via separate helpers; floats here are continuous.
 *
 * advanceWidth: horizontal advance (+X).
 * leftSideBearing: offset from origin to left of ink.
 * bounds: axis-aligned ink box relative to origin (Y increases downward).
 */
typedef struct GfxGlyphMetricsDevice {
    float advanceWidth;
    float leftSideBearing;
    float ascent;  /* extent above baseline as positive magnitude */
    float descent; /* extent below baseline as positive magnitude */
    GfxRectF bounds;
    uint8_t hasBounds;
} GfxGlyphMetricsDevice;

void GfxGlyphDrawInit(GfxGlyphDraw *draw);

/**
 * Scale factor: sizePx / unitsPerEm.
 * upem==0 or non-finite / non-positive size → RoboInvalidArgument.
 */
Result GfxFontScale(uint16_t unitsPerEm,
                                  float sizePx,
                                  float *outScale);

/**
 * Font-unit point → device float at baseline @p origin.
 * Font +Y up → device +Y down.
 */
Result GfxFontPointToDevice(FontUnit fontX,
                                           FontUnit fontY,
                                           uint16_t unitsPerEm,
                                           float sizePx,
                                           GfxPointF origin,
                                           GfxPointF *outDevice);

/**
 * Round device float to integer pixel (half away from zero — matches
 * GfxPointFRound / font nearest policy).
 */
GfxPoint GfxGlyphSnapPoint(GfxPointF p);

/**
 * Scaled glyph metrics from RoboFont (authoritative advances / bearings / bbox).
 * Parses metrics on the face if needed.
 */
Result GetGfxGlyphMetricsDevice(FontFace *face,
                                           GlyphId glyphId,
                                           float sizePx,
                                           GfxGlyphMetricsDevice *out);

/**
 * Convert a RoboFont outline into a RoboGraphics path in device space.
 *
 * MOVE/LINE/CLOSE map directly. QUAD/CUBIC are flattened to LineTo with a
 * fixed subdivision (geometry conversion only — not AA Bezier rasterization).
 *
 * @p path is reset then filled. Empty / empty-status outline → empty path,
 * RoboOk. Invalid glyph / malformed → error. Caller owns @p path.
 */
Result GfxGlyphOutlineToPath(FontFace *face,
                                             GlyphId glyphId,
                                             float sizePx,
                                             GfxPointF origin,
                                             GfxPath *path);

/**
 * Convert an already-fetched outline (retained by caller) to a device path.
 */
Result GfxOutlineToPath(const FontOutline *outline,
                                       uint16_t unitsPerEm,
                                       float sizePx,
                                       GfxPointF origin,
                                       GfxPath *path);

/**
 * Draw a glyph through the CPU raster backend.
 *
 * Goal 6.12: OUTLINE_STROKE — 1-pixel Bresenham strokes of the converted
 * outline (clip + opacity from @p raster). MASK/ALPHA/COLOR →
 * RoboNotSupported.
 *
 * Completely outside clip / empty outline → RoboOk no-op.
 * Honors raster clip; never writes outside the surface.
 */
Result GfxRasterDrawGlyph(GfxRaster *raster,
                                         const GfxGlyphDraw *draw);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_TEXT_H */
