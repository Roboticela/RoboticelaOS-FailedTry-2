/*
 * RoboFont metrics model (Goals 5.1 + 5.7).
 *
 * Distinguishes:
 *   font-space units (FUnits)     — design units; depend on units-per-em
 *   device-independent pixels     — CSS px / logical pixels
 *   device pixels                 — physical framebuffer (later DPI)
 *
 * Goal 5.7 builds an immutable metrics info object from SFNT tables
 * (head/hhea/hmtx/OS/2/post + optional vhea/vmtx). Metrics are measurements;
 * they are not shaping, layout, or kerning.
 *
 * Outline technology (glyf / CFF / CFF2) does not change the hmtx-based
 * horizontal metrics API. Glyph bounding boxes are available for TrueType
 * when glyf is present; CFF/CFF2 bbox awaits Goal 5.8 outlines.
 */

#ifndef ROBO_FONT_METRICS_H
#define ROBO_FONT_METRICS_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_font_tt robo_font_tt_t;
typedef struct robo_font_sfnt robo_font_sfnt_t;

#define ROBO_FONT_UPEM_MIN 1u
#define ROBO_FONT_UPEM_MAX 16384u
#define ROBO_FONT_OS2_USE_TYPO_METRICS 0x0080u

/* ---- Availability ---- */

typedef enum robo_font_metric_avail {
    ROBO_FONT_METRIC_PRESENT = 0,     /* font-authored */
    ROBO_FONT_METRIC_UNAVAILABLE = 1, /* table/field absent */
    ROBO_FONT_METRIC_FALLBACK = 2,    /* runtime default (not font-authored) */
    ROBO_FONT_METRIC_MALFORMED = 3    /* present but invalid */
} robo_font_metric_avail_t;

typedef struct robo_font_optional_unit {
    robo_font_unit_t value;
    robo_font_metric_avail_t avail;
} robo_font_optional_unit_t;

typedef struct robo_font_optional_u16 {
    uint16_t value;
    robo_font_metric_avail_t avail;
} robo_font_optional_u16_t;

/* ---- Canonical line metrics (layout) ---- */

/**
 * Typographic line metrics in font units (+Y up).
 * ascent: above baseline (typically positive).
 * descent: below baseline as non-negative magnitude (design y = -descent).
 * line_gap: extra recommended spacing between lines.
 *
 * This is the canonical layout line-metrics snapshot (see Metrics.md policy).
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

typedef enum robo_font_line_metric_source {
    ROBO_FONT_LINE_FROM_HHEA = 0,
    ROBO_FONT_LINE_FROM_OS2_TYPO = 1 /* fsSelection USE_TYPO_METRICS */
} robo_font_line_metric_source_t;

typedef enum robo_font_round_mode {
    ROBO_FONT_ROUND_NEAREST = 0, /* ties away from zero */
    ROBO_FONT_ROUND_FLOOR = 1,
    ROBO_FONT_ROUND_CEIL = 2,
    ROBO_FONT_ROUND_TOWARD_ZERO = 3
} robo_font_round_mode_t;

/* ---- Glyph metrics ---- */

typedef enum robo_font_glyph_metrics_status {
    ROBO_FONT_GLYPH_METRICS_OK = 0,
    ROBO_FONT_GLYPH_METRICS_INVALID_ID = 1,
    ROBO_FONT_GLYPH_METRICS_UNAVAILABLE = 2,
    ROBO_FONT_GLYPH_METRICS_MALFORMED = 3
} robo_font_glyph_metrics_status_t;

typedef struct robo_font_glyph_metrics {
    robo_font_glyph_metrics_status_t status;
    robo_glyph_id_t glyph_id;
    uint16_t advance_width; /* FUnits; unshaped */
    int16_t left_side_bearing;
    int16_t right_side_bearing; /* valid only if has_rsb */
    uint8_t has_rsb;
    robo_font_bbox_t bbox; /* glyph outline bbox when has_bbox */
    uint8_t has_bbox;
} robo_font_glyph_metrics_t;

/* ---- Vertical foundation ---- */

typedef struct robo_font_vhea {
    uint16_t major_version;
    uint16_t minor_version;
    int16_t ascent;
    int16_t descent;
    int16_t line_gap;
    int16_t advance_height_max;
    int16_t min_top_side_bearing;
    int16_t min_bottom_side_bearing;
    int16_t y_max_extent;
    int16_t caret_slope_rise;
    int16_t caret_slope_run;
    int16_t caret_offset;
    int16_t metric_data_format;
    uint16_t number_of_v_metrics;
} robo_font_vhea_t;

typedef struct robo_font_vertical_glyph_metrics {
    robo_font_glyph_metrics_status_t status;
    robo_glyph_id_t glyph_id;
    uint16_t advance_height;
    int16_t top_side_bearing;
} robo_font_vertical_glyph_metrics_t;

/* ---- Full immutable metrics info ---- */

/**
 * Global metrics snapshot (copied out of the opaque info object).
 * Optional fields use avail; do not invent values when UNAVAILABLE.
 */
typedef struct robo_font_metrics_global {
    uint16_t units_per_em;
    robo_font_metric_avail_t upem_avail;

    /* Canonical layout line metrics (policy-selected) */
    robo_font_metrics_t layout;
    robo_font_line_metric_source_t layout_source;

    /* Raw competing categories (never silently collapsed) */
    robo_font_unit_t hhea_ascender;
    robo_font_unit_t hhea_descender_mag; /* magnitude */
    robo_font_unit_t hhea_line_gap;
    uint8_t has_hhea_line;

    robo_font_unit_t typo_ascender;
    robo_font_unit_t typo_descender_mag;
    robo_font_unit_t typo_line_gap;
    uint8_t has_typo_line; /* OS/2 present (fields always in v0+) */

    uint16_t win_ascent;
    uint16_t win_descent;
    uint8_t has_win_clip; /* clipping metrics; not default line spacing */

    robo_font_optional_unit_t cap_height;
    robo_font_optional_unit_t x_height;
    robo_font_optional_unit_t average_char_width; /* OS/2 xAvgCharWidth */

    uint16_t max_advance_width;
    int16_t min_left_side_bearing;
    int16_t min_right_side_bearing;
    int16_t max_x_extent;
    uint8_t has_hhea_extrema;

    int16_t caret_slope_rise;
    int16_t caret_slope_run;
    int16_t caret_offset;
    robo_font_metric_avail_t caret_avail; /* PRESENT or FALLBACK */

    robo_font_optional_unit_t underline_position;
    robo_font_optional_unit_t underline_thickness;
    robo_font_optional_unit_t strikeout_position;
    robo_font_optional_unit_t strikeout_thickness;

    robo_font_optional_u16_t us_weight_class;
    robo_font_optional_u16_t us_width_class;

    robo_font_bbox_t font_bbox;
    robo_font_metric_avail_t font_bbox_avail;

    uint16_t num_glyphs;
    uint16_t number_of_h_metrics;
    uint8_t has_vertical; /* vhea+vmtx parsed */
} robo_font_metrics_global_t;

typedef struct robo_font_metrics_info robo_font_metrics_info_t;

void robo_font_metrics_clear(robo_font_metrics_t *m);

/**
 * Initialize layout metrics. descent must be ≥ 0; units_per_em must be > 0.
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
 * Returns ROBO_INVALID_ARGUMENT if upem==0 or out NULL.
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

/**
 * Safe scale with overflow checks (int64 intermediate) and rounding policy.
 * Device-pixel conversion is intentionally not performed here.
 */
robo_result_t robo_font_scale_font_units(robo_font_unit_t value,
                                         uint16_t units_per_em,
                                         robo_font_pixel_size_t font_size,
                                         robo_font_round_mode_t round_mode,
                                         float *out_scaled);

/** Validate units_per_em (non-zero and within OT-reasonable bounds). */
int robo_font_units_per_em_is_valid(uint16_t units_per_em);

/* ---- vhea / vmtx foundation ---- */

robo_result_t robo_font_vhea_parse(robo_font_span_t span, robo_font_vhea_t *out);

/**
 * Build immutable metrics info from a parsed TrueType bundle (retains @p tt).
 * Works for glyf, CFF, and CFF2 SFNT fonts that share head/hhea/hmtx/maxp.
 */
robo_result_t robo_font_metrics_info_create_from_tt(robo_font_tt_t *tt,
                                                    robo_font_metrics_info_t **out);

/** Parse SFNT → TT (no glyf required) → metrics info. */
robo_result_t robo_font_metrics_info_create(robo_font_sfnt_t *sfnt,
                                            robo_font_metrics_info_t **out);

void robo_font_metrics_info_retain(robo_font_metrics_info_t *info);
void robo_font_metrics_info_release(robo_font_metrics_info_t *info);

const robo_font_metrics_global_t *robo_font_metrics_info_global(
    const robo_font_metrics_info_t *info);

/** Canonical layout metrics (same as global->layout). */
robo_result_t robo_font_metrics_info_layout(const robo_font_metrics_info_t *info,
                                            robo_font_metrics_t *out);

robo_result_t robo_font_metrics_info_glyph(const robo_font_metrics_info_t *info,
                                           robo_glyph_id_t glyph_id,
                                           robo_font_glyph_metrics_t *out);

/**
 * Vertical glyph metrics when vhea/vmtx present.
 * Otherwise status = UNAVAILABLE.
 */
robo_result_t robo_font_metrics_info_glyph_vertical(
    const robo_font_metrics_info_t *info,
    robo_glyph_id_t glyph_id,
    robo_font_vertical_glyph_metrics_t *out);

const robo_font_vhea_t *robo_font_metrics_info_vhea(
    const robo_font_metrics_info_t *info); /* NULL if absent */

/** Fuzz-ready: parse metrics from arbitrary SFNT-ish bytes. */
robo_result_t robo_font_metrics_fuzz_parse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_METRICS_H */
