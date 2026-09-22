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
 * Goal 6.12's draw mode strokes outlines with 1-pixel lines along the flattened
 * contour. It is kept because it is cheap to audit and because a caller comparing
 * it against the coverage path is exactly the before/after this module's Phase 12.1
 * documents — but it is **not** what UI text should use.
 *
 * Phase 12.1 adds `RoboGfxGlyphModeAlpha`: the same outline, flattened without
 * snapping and rasterized into a grayscale **coverage mask** with 8×8 supersampling,
 * composited through the module's own source-over blend with a documented
 * coverage → alpha model (`RoboGraphicsGlyph.h`). This is the mode the paint stack's
 * text path selects, and the mode every later typography phase should reach
 * through. `MASK` (binary) and `COLOR` (COLR/bitmap/SVG) remain unimplemented, and
 * returning `RoboNotSupported` for them is the honest answer rather than a silent
 * outline stroke.
 *
 * See graphics/docs/TextBridge.md and
 * Documentation/Runtime/Typography/TextRasterQuality.md.
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
#include "RoboGraphics/RoboGraphicsGlyph.h"
#include "RoboGraphics/RoboGraphicsPath.h"
#include "RoboGraphics/RoboGraphicsRaster.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Glyph paint modes.
 *
 * OUTLINE_STROKE (and a zeroed @c mode, which means the same) and
 * ALPHA (Phase 12.1's grayscale coverage AA) are implemented.
 * MASK and COLOR are not, and are reported as unsupported rather than
 * approximated.
 */
typedef enum GfxGlyphRenderMode {
    RoboGfxGlyphModeOutlineStroke = 1, /* 1px outline stroke (Goal 6.12) */
    RoboGfxGlyphModeMask = 2,           /* not implemented: binary coverage */
    RoboGfxGlyphModeAlpha = 3,          /* grayscale coverage AA (Phase 12.1) */
    RoboGfxGlyphModeColor = 4           /* not implemented: COLR/bitmap/SVG */
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
 * OUTLINE_STROKE (or @c mode == 0) — 1-pixel Bresenham strokes of the converted
 * outline, snapped to whole pixels, with the clip and opacity from @p raster.
 *
 * ALPHA (Phase 12.1) — the outline rasterized into a grayscale coverage mask at
 * the draw's own (possibly fractional) origin and composited with
 * @ref RoboGfxCoverageGammaDefault. This is the UI-text mode.
 *
 * MASK / COLOR → RoboNotSupported.
 *
 * Completely outside the clip / empty outline → RoboOk no-op.
 * Honors the raster clip; never writes outside the surface.
 */
Result GfxRasterDrawGlyph(GfxRaster *raster,
                                         const GfxGlyphDraw *draw);

/**
 * Draw @p draw's glyph as a coverage mask (Phase 12.1), with explicit
 * rasterization parameters.
 *
 * The same door @ref GfxRasterDrawGlyph reaches for `ALPHA`, opened so a caller —
 * the typography quality fixture, a future glyph-run renderer — can choose the
 * gamma model and the supersample count instead of taking the defaults. @p params
 * may be NULL, which means `GfxGlyphRasterParamsInit`'s values.
 *
 * The glyph's placement is @p draw's `origin` in device pixels, honoured to a
 * 1/256 pixel; the mask is clipped to the raster's own clip, so a glyph whose ink
 * runs off the surface costs only the pixels that are on it and can never write
 * outside it.
 *
 * An outline this build cannot use answers with the same status
 * `GfxGlyphOutlineToPath` gives it (invalid or malformed → RoboInvalidArgument,
 * unavailable or unsupported → RoboNotSupported); ink that simply has no area is a
 * successful no-op.
 */
Result GfxRasterDrawGlyphCoverage(GfxRaster *raster,
                                  const GfxGlyphDraw *draw,
                                  const GfxGlyphRasterParams *params);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_TEXT_H */
