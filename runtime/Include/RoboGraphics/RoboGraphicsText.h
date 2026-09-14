/*
 * RoboGraphics ↔ RoboFont text rendering bridge (Goal 6.12).
 *
 * Glyph-centric integration boundary: RoboGraphics does not shape, segment,
 * bidi, or fall back fonts. RoboText supplies glyph IDs + placement later.
 *
 * Font design space: origin on baseline, +X right, +Y up (OpenType).
 * RoboGraphics device space: origin top-left, +X right, +Y down.
 *
 *   device_x = origin_x + (font_x / upem) * size_px
 *   device_y = origin_y - (font_y / upem) * size_px
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
typedef enum robo_gfx_glyph_render_mode {
    ROBO_GFX_GLYPH_MODE_OUTLINE_STROKE = 1, /* 1px outline stroke */
    ROBO_GFX_GLYPH_MODE_MASK = 2,           /* future monochrome coverage */
    ROBO_GFX_GLYPH_MODE_ALPHA = 3,          /* future grayscale AA */
    ROBO_GFX_GLYPH_MODE_COLOR = 4           /* future COLR/bitmap/SVG */
} robo_gfx_glyph_render_mode_t;

/**
 * Glyph draw request (absolute baseline origin in device space).
 * @p size_px is CSS-like pixel size (must be finite and > 0).
 * Opacity comes from the raster context.
 */
typedef struct robo_gfx_glyph_draw {
    robo_font_face_t *face;
    robo_glyph_id_t glyph_id;
    float size_px;
    robo_gfx_point_f_t origin; /* baseline origin (device, +Y down) */
    robo_gfx_color_rgba8_t color;
    robo_gfx_glyph_render_mode_t mode; /* 0 → OUTLINE_STROKE */
} robo_gfx_glyph_draw_t;

/**
 * Scaled glyph metrics in device pixels relative to baseline origin (0,0).
 * Bounds use RoboGraphics Y-down half-open semantics once snapped to ints
 * via separate helpers; floats here are continuous.
 *
 * advance_width: horizontal advance (+X).
 * left_side_bearing: offset from origin to left of ink.
 * bounds: axis-aligned ink box relative to origin (Y increases downward).
 */
typedef struct robo_gfx_glyph_metrics_device {
    float advance_width;
    float left_side_bearing;
    float ascent;  /* extent above baseline as positive magnitude */
    float descent; /* extent below baseline as positive magnitude */
    robo_gfx_rect_f_t bounds;
    uint8_t has_bounds;
} robo_gfx_glyph_metrics_device_t;

void robo_gfx_glyph_draw_init(robo_gfx_glyph_draw_t *draw);

/**
 * Scale factor: size_px / units_per_em.
 * upem==0 or non-finite / non-positive size → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_gfx_font_scale(uint16_t units_per_em,
                                  float size_px,
                                  float *out_scale);

/**
 * Font-unit point → device float at baseline @p origin.
 * Font +Y up → device +Y down.
 */
robo_result_t robo_gfx_font_point_to_device(robo_font_unit_t font_x,
                                           robo_font_unit_t font_y,
                                           uint16_t units_per_em,
                                           float size_px,
                                           robo_gfx_point_f_t origin,
                                           robo_gfx_point_f_t *out_device);

/**
 * Round device float to integer pixel (half away from zero — matches
 * robo_gfx_point_f_round / font nearest policy).
 */
robo_gfx_point_t robo_gfx_glyph_snap_point(robo_gfx_point_f_t p);

/**
 * Scaled glyph metrics from RoboFont (authoritative advances / bearings / bbox).
 * Parses metrics on the face if needed.
 */
robo_result_t robo_gfx_glyph_metrics_device(robo_font_face_t *face,
                                           robo_glyph_id_t glyph_id,
                                           float size_px,
                                           robo_gfx_glyph_metrics_device_t *out);

/**
 * Convert a RoboFont outline into a RoboGraphics path in device space.
 *
 * MOVE/LINE/CLOSE map directly. QUAD/CUBIC are flattened to LineTo with a
 * fixed subdivision (geometry conversion only — not AA Bezier rasterization).
 *
 * @p path is reset then filled. Empty / empty-status outline → empty path,
 * ROBO_OK. Invalid glyph / malformed → error. Caller owns @p path.
 */
robo_result_t robo_gfx_glyph_outline_to_path(robo_font_face_t *face,
                                             robo_glyph_id_t glyph_id,
                                             float size_px,
                                             robo_gfx_point_f_t origin,
                                             robo_gfx_path_t *path);

/**
 * Convert an already-fetched outline (retained by caller) to a device path.
 */
robo_result_t robo_gfx_outline_to_path(const robo_font_outline_t *outline,
                                       uint16_t units_per_em,
                                       float size_px,
                                       robo_gfx_point_f_t origin,
                                       robo_gfx_path_t *path);

/**
 * Draw a glyph through the CPU raster backend.
 *
 * Goal 6.12: OUTLINE_STROKE — 1-pixel Bresenham strokes of the converted
 * outline (clip + opacity from @p raster). MASK/ALPHA/COLOR →
 * ROBO_NOT_SUPPORTED.
 *
 * Completely outside clip / empty outline → ROBO_OK no-op.
 * Honors raster clip; never writes outside the surface.
 */
robo_result_t robo_gfx_raster_draw_glyph(robo_gfx_raster_t *raster,
                                         const robo_gfx_glyph_draw_t *draw);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_TEXT_H */
