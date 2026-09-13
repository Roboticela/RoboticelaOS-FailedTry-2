/*
 * RoboFont TrueType semantic tables (Goal 5.3).
 *
 * Parses core TT tables on top of the SFNT container (Goal 5.2):
 *   head, hhea, maxp, hmtx, loca, glyf, name, OS/2, post
 *
 * Does NOT parse cmap (5.6), CFF (5.5), GSUB/GPOS (later), or rasterize.
 *
 * Ownership: robo_font_tt_t retains the SFNT (and thus the source).
 * Glyph structural parse results own temporary arrays cleared by the caller.
 */

#ifndef ROBO_FONT_TT_H
#define ROBO_FONT_TT_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontMetrics.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontStyle.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBO_FONT_HEAD_MAGIC ((uint32_t)0x5F0F3CF5u)
#define ROBO_FONT_TT_MAX_GLYPHS 65535u
#define ROBO_FONT_TT_MAX_COMPOSITE_DEPTH 64u
#define ROBO_FONT_TT_MAX_COMPONENTS_PER_GLYPH 256u
#define ROBO_FONT_TT_MAX_NAME_RECORDS 4096u
#define ROBO_FONT_TT_MAX_NAME_STRING 4096u

/* ---- head ---- */

typedef struct robo_font_head {
    uint16_t major_version;
    uint16_t minor_version;
    uint32_t font_revision; /* Fixed 16.16 as raw uint32 */
    uint32_t checksum_adjustment;
    uint32_t magic_number;
    uint16_t flags;
    uint16_t units_per_em;
    int64_t created;  /* LONGDATETIME */
    int64_t modified;
    int16_t x_min;
    int16_t y_min;
    int16_t x_max;
    int16_t y_max;
    uint16_t mac_style;
    uint16_t lowest_rec_ppem;
    int16_t font_direction_hint;
    int16_t index_to_loc_format; /* 0 short, 1 long */
    int16_t glyph_data_format;
} robo_font_head_t;

robo_result_t robo_font_head_parse(robo_font_span_t span, robo_font_head_t *out);

/* ---- hhea ---- */

typedef struct robo_font_hhea {
    uint16_t major_version;
    uint16_t minor_version;
    int16_t ascender;
    int16_t descender;
    int16_t line_gap;
    uint16_t advance_width_max;
    int16_t min_left_side_bearing;
    int16_t min_right_side_bearing;
    int16_t x_max_extent;
    int16_t caret_slope_rise;
    int16_t caret_slope_run;
    int16_t caret_offset;
    int16_t metric_data_format;
    uint16_t number_of_h_metrics;
} robo_font_hhea_t;

robo_result_t robo_font_hhea_parse(robo_font_span_t span, robo_font_hhea_t *out);

/* ---- maxp ---- */

typedef struct robo_font_maxp {
    uint32_t version; /* Fixed: 0x00005000 or 0x00010000 */
    uint16_t num_glyphs;
    /* v1.0 fields (zeroed for v0.5) */
    uint16_t max_points;
    uint16_t max_contours;
    uint16_t max_composite_points;
    uint16_t max_composite_contours;
    uint16_t max_zones;
    uint16_t max_twilight_points;
    uint16_t max_storage;
    uint16_t max_function_defs;
    uint16_t max_instruction_defs;
    uint16_t max_stack_elements;
    uint16_t max_size_of_instructions;
    uint16_t max_component_elements;
    uint16_t max_component_depth;
} robo_font_maxp_t;

robo_result_t robo_font_maxp_parse(robo_font_span_t span, robo_font_maxp_t *out);

/* ---- post ---- */

typedef struct robo_font_post {
    uint32_t version; /* Fixed */
    int32_t italic_angle; /* Fixed 16.16 */
    int16_t underline_position;
    int16_t underline_thickness;
    uint32_t is_fixed_pitch;
    uint32_t min_mem_type42;
    uint32_t max_mem_type42;
    uint32_t min_mem_type1;
    uint32_t max_mem_type1;
} robo_font_post_t;

robo_result_t robo_font_post_parse(robo_font_span_t span, robo_font_post_t *out);

/* ---- OS/2 (version-aware) ---- */

typedef struct robo_font_os2 {
    uint16_t version;
    int16_t x_avg_char_width;
    uint16_t us_weight_class;
    uint16_t us_width_class;
    uint16_t fs_type;
    int16_t y_subscript_x_size;
    int16_t y_subscript_y_size;
    int16_t y_subscript_x_offset;
    int16_t y_subscript_y_offset;
    int16_t y_superscript_x_size;
    int16_t y_superscript_y_size;
    int16_t y_superscript_x_offset;
    int16_t y_superscript_y_offset;
    int16_t y_strikeout_size;
    int16_t y_strikeout_position;
    int16_t s_family_class;
    uint8_t panose[10];
    uint32_t ul_unicode_range1;
    uint32_t ul_unicode_range2;
    uint32_t ul_unicode_range3;
    uint32_t ul_unicode_range4;
    uint8_t ach_vend_id[4];
    uint16_t fs_selection;
    uint16_t us_first_char_index;
    uint16_t us_last_char_index;
    int16_t s_typo_ascender;
    int16_t s_typo_descender;
    int16_t s_typo_line_gap;
    uint16_t us_win_ascent;
    uint16_t us_win_descent;
    /* version >= 1 */
    uint32_t ul_code_page_range1;
    uint32_t ul_code_page_range2;
    uint8_t has_code_page_ranges;
    /* version >= 2 */
    int16_t sx_height;
    int16_t s_cap_height;
    uint16_t us_default_char;
    uint16_t us_break_char;
    uint16_t us_max_context;
    uint8_t has_v2_metrics;
    /* version >= 5 */
    uint16_t us_lower_optical_point_size;
    uint16_t us_upper_optical_point_size;
    uint8_t has_v5_optical;
} robo_font_os2_t;

robo_result_t robo_font_os2_parse(robo_font_span_t span, robo_font_os2_t *out);

/* ---- name ---- */

#define ROBO_FONT_NAME_ID_FAMILY 1u
#define ROBO_FONT_NAME_ID_SUBFAMILY 2u
#define ROBO_FONT_NAME_ID_UNIQUE 3u
#define ROBO_FONT_NAME_ID_FULL 4u
#define ROBO_FONT_NAME_ID_VERSION 5u
#define ROBO_FONT_NAME_ID_POSTSCRIPT 6u
#define ROBO_FONT_NAME_ID_TYPO_FAMILY 16u
#define ROBO_FONT_NAME_ID_TYPO_SUBFAMILY 17u

typedef struct robo_font_name_record {
    uint16_t platform_id;
    uint16_t encoding_id;
    uint16_t language_id;
    uint16_t name_id;
    uint16_t length;
    uint16_t offset; /* from string storage start */
} robo_font_name_record_t;

typedef struct robo_font_name_table {
    uint16_t format;
    uint16_t count;
    uint16_t string_offset;
    robo_font_name_record_t *records; /* count entries; owned when from tt bundle */
    robo_font_span_t storage;         /* string bytes view */
} robo_font_name_table_t;

/**
 * Parse name table header + records into @p out.
 * If @p out->records is NULL, allocates; caller must robo_font_name_table_clear
 * unless owned by robo_font_tt_t.
 */
robo_result_t robo_font_name_parse(robo_font_span_t span, robo_font_name_table_t *out);
void robo_font_name_table_clear(robo_font_name_table_t *t);

/**
 * Decode a name record to UTF-8 into @p buf.
 * Prefers Windows Unicode (platform 3, encoding 1/10), then Mac Roman ASCII.
 * ROBO_NOT_FOUND if no decodable record for @p name_id.
 */
robo_result_t robo_font_name_get_utf8(const robo_font_name_table_t *t,
                                      uint16_t name_id,
                                      char *buf,
                                      size_t buf_cap,
                                      size_t *out_len);

/* ---- hmtx / loca (query via tt object) ---- */

/* ---- glyf ---- */

#define ROBO_FONT_GLYF_FLAG_ON_CURVE 0x01u
#define ROBO_FONT_GLYF_FLAG_X_SHORT 0x02u
#define ROBO_FONT_GLYF_FLAG_Y_SHORT 0x04u
#define ROBO_FONT_GLYF_FLAG_REPEAT 0x08u
#define ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE 0x10u
#define ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE 0x20u

#define ROBO_FONT_GLYF_COMP_ARGS_WORDS 0x0001u
#define ROBO_FONT_GLYF_COMP_ARGS_XY 0x0002u
#define ROBO_FONT_GLYF_COMP_ROUND_XY 0x0004u
#define ROBO_FONT_GLYF_COMP_HAVE_SCALE 0x0008u
#define ROBO_FONT_GLYF_COMP_MORE 0x0020u
#define ROBO_FONT_GLYF_COMP_HAVE_XY_SCALE 0x0040u
#define ROBO_FONT_GLYF_COMP_HAVE_2X2 0x0080u
#define ROBO_FONT_GLYF_COMP_HAVE_INSTRUCTIONS 0x0100u
#define ROBO_FONT_GLYF_COMP_USE_MY_METRICS 0x0200u
#define ROBO_FONT_GLYF_COMP_OVERLAP 0x0400u

typedef enum robo_font_glyf_kind {
    ROBO_FONT_GLYF_EMPTY = 0,
    ROBO_FONT_GLYF_SIMPLE = 1,
    ROBO_FONT_GLYF_COMPOSITE = 2
} robo_font_glyf_kind_t;

typedef struct robo_font_glyf_header {
    int16_t number_of_contours;
    int16_t x_min;
    int16_t y_min;
    int16_t x_max;
    int16_t y_max;
} robo_font_glyf_header_t;

typedef struct robo_font_glyf_component {
    uint16_t flags;
    uint16_t glyph_index;
    int32_t arg1; /* FWord or point index depending on flags */
    int32_t arg2;
    /* Transform: Fixed 16.16 stored as int16 × 2^-14 (F2DOT14) × 4 or scales */
    int16_t xscale; /* F2DOT14; 1.0 = 0x4000 when have scale */
    int16_t scale01;
    int16_t scale10;
    int16_t yscale;
    uint8_t has_transform;
} robo_font_glyf_component_t;

typedef struct robo_font_glyf_glyph {
    robo_font_glyf_kind_t kind;
    robo_font_glyf_header_t header;
    uint32_t offset; /* in glyf table */
    uint32_t length;
    /* simple */
    uint16_t contour_count;
    uint16_t point_count;
    uint16_t instruction_length;
    uint16_t *end_pts;     /* owned; contour_count */
    uint8_t *flags;        /* owned; point_count expanded */
    int16_t *x_coords;     /* owned; absolute coords */
    int16_t *y_coords;
    robo_font_span_t instructions; /* view into glyf */
    /* composite */
    uint16_t component_count;
    robo_font_glyf_component_t *components; /* owned */
    uint16_t composite_instruction_length;
    robo_font_span_t composite_instructions;
} robo_font_glyf_glyph_t;

void robo_font_glyf_glyph_clear(robo_font_glyf_glyph_t *g);

/* ---- bundled TrueType parse ---- */

typedef enum robo_font_tt_flags {
    ROBO_FONT_TT_FLAGS_NONE = 0,
    /** Require glyf+loca (TrueType outlines). CFF-only fonts → NOT_SUPPORTED. */
    ROBO_FONT_TT_FLAGS_REQUIRE_GLYF = 1,
    /** Verify head directory checksum with checksumAdjustment zeroed. */
    ROBO_FONT_TT_FLAGS_VERIFY_HEAD_CHECKSUM = 2
} robo_font_tt_flags_t;

typedef struct robo_font_tt robo_font_tt_t;

/**
 * Parse core TrueType tables from an already-parsed SFNT.
 * Retains @p sfnt. Cross-validates hmtx/loca/glyf relationships.
 */
robo_result_t robo_font_tt_parse(robo_font_sfnt_t *sfnt,
                                 uint32_t flags,
                                 robo_font_tt_t **out);

void robo_font_tt_retain(robo_font_tt_t *tt);
void robo_font_tt_release(robo_font_tt_t *tt);

const robo_font_sfnt_t *robo_font_tt_sfnt(const robo_font_tt_t *tt);
const robo_font_head_t *robo_font_tt_head(const robo_font_tt_t *tt);
const robo_font_hhea_t *robo_font_tt_hhea(const robo_font_tt_t *tt);
const robo_font_maxp_t *robo_font_tt_maxp(const robo_font_tt_t *tt);
const robo_font_post_t *robo_font_tt_post(const robo_font_tt_t *tt);
const robo_font_os2_t *robo_font_tt_os2(const robo_font_tt_t *tt); /* NULL if absent */
const robo_font_name_table_t *robo_font_tt_name(const robo_font_tt_t *tt); /* NULL if absent */

uint16_t robo_font_tt_num_glyphs(const robo_font_tt_t *tt);

robo_result_t robo_font_tt_advance_width(const robo_font_tt_t *tt,
                                         robo_glyph_id_t glyph_id,
                                         uint16_t *out_aw);
robo_result_t robo_font_tt_left_side_bearing(const robo_font_tt_t *tt,
                                             robo_glyph_id_t glyph_id,
                                             int16_t *out_lsb);

robo_result_t robo_font_tt_glyph_offset(const robo_font_tt_t *tt,
                                        robo_glyph_id_t glyph_id,
                                        uint32_t *out_offset);
robo_result_t robo_font_tt_glyph_length(const robo_font_tt_t *tt,
                                        robo_glyph_id_t glyph_id,
                                        uint32_t *out_length);

/** Parse glyph structure (simple or composite). Clears @p out first. */
robo_result_t robo_font_tt_parse_glyph(const robo_font_tt_t *tt,
                                       robo_glyph_id_t glyph_id,
                                       robo_font_glyf_glyph_t *out);

/** Parse one glyph from a glyf span (for unit tests / tooling). */
robo_result_t robo_font_tt_parse_glyph_at(robo_font_span_t glyf,
                                          uint32_t offset,
                                          uint32_t length,
                                          uint16_t num_glyphs,
                                          robo_font_glyf_glyph_t *out);

/**
 * Walk composite components with recursion depth limit (validates only).
 * ROBO_PARSE_ERROR on excessive depth / bad refs.
 */
robo_result_t robo_font_tt_validate_composite_closure(const robo_font_tt_t *tt,
                                                      robo_glyph_id_t glyph_id);

/** Fill RoboFont metrics from head + hhea (+ optional OS/2 typo metrics). */
robo_result_t robo_font_tt_metrics(const robo_font_tt_t *tt, robo_font_metrics_t *out);

/** Derive style_desc from OS/2 / head.macStyle when available. */
robo_result_t robo_font_tt_style_desc(const robo_font_tt_t *tt,
                                      robo_font_style_desc_t *out);

/** Fuzz-ready: SFNT + TT parse of borrowed bytes, then release. */
robo_result_t robo_font_tt_fuzz_parse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_TT_H */
