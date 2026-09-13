/*
 * RoboFont metrics model (Goal 5.1).
 *
 * Distinguishes:
 *   font-space units (FUnits)     — design units; depend on units-per-em
 *   device-independent pixels     — CSS px / logical pixels
 *   device pixels                 — physical framebuffer samples (later DPI)
 *
 * Scaling (future): pixel = font_unit * (pixel_size / units_per_em).
 * Goal 5.1 provides the types and pure conversion helpers; faces do not yet
 * populate metrics from SFNT tables (Goal 5.2+).
 */

#ifndef ROBO_FONT_METRICS_H
#define ROBO_FONT_METRICS_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontTypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Typographic metrics in font units (design space, +Y up).
 * ascent: distance above baseline (typically positive).
 * descent: distance below baseline (stored as a non-negative magnitude;
 *          design-space y of the descender is -descent).
 * line_gap: extra spacing recommended between lines (font units).
 */
typedef struct robo_font_metrics {
    uint16_t units_per_em; /* typically 1000 or 2048; 0 = unset */
    robo_font_unit_t ascent;
    robo_font_unit_t descent; /* magnitude ≥ 0 */
    robo_font_unit_t line_gap;
    robo_font_bbox_t bbox; /* font bounding box in font units; zero if unset */
    uint8_t has_bbox;
    uint8_t valid; /* 1 if ascent/descent/upem were set intentionally */
} robo_font_metrics_t;

void robo_font_metrics_clear(robo_font_metrics_t *m);

/**
 * Initialize metrics. descent must be ≥ 0; units_per_em must be > 0.
 * bbox optional (pass NULL → has_bbox=0).
 */
robo_result_t robo_font_metrics_init(robo_font_metrics_t *m,
                                     uint16_t units_per_em,
                                     robo_font_unit_t ascent,
                                     robo_font_unit_t descent,
                                     robo_font_unit_t line_gap,
                                     const robo_font_bbox_t *bbox);

/** Line height in font units: ascent + descent + line_gap. */
robo_font_unit_t robo_font_metrics_line_height(const robo_font_metrics_t *m);

/**
 * Scale a font-unit length to CSS pixels given pixel size and upem.
 * Returns ROBO_INVALID_ARGUMENT if upem==0 or m/out NULL.
 */
robo_result_t robo_font_units_to_pixels(robo_font_unit_t units,
                                        uint16_t units_per_em,
                                        robo_font_pixel_size_t pixel_size,
                                        float *out_pixels);

/**
 * Scale CSS pixels to font units (rounded toward nearest, ties away from 0).
 */
robo_result_t robo_font_pixels_to_units(float pixels,
                                        uint16_t units_per_em,
                                        robo_font_pixel_size_t pixel_size,
                                        robo_font_unit_t *out_units);

/** Approximate: points → CSS px assuming 96 CSS px per inch (1pt = 96/72 px). */
robo_font_pixel_size_t robo_font_points_to_pixels(robo_font_point_size_t points);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_METRICS_H */
