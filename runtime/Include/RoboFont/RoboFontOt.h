/*
 * RoboFont OpenType semantic tables (Goal 5.4).
 *
 * Parses and represents OpenType structures for later shaping/color/selection.
 * Does NOT execute GSUB/GPOS, shape text, or fully map cmap (5.6).
 * CFF/CFF2 table bodies are Goal 5.5 (`RoboFontCff.h`).
 *
 * Ownership: robo_font_ot_t retains the SFNT (and thus the source).
 */

#ifndef ROBO_FONT_OT_H
#define ROBO_FONT_OT_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontOtCommon.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Outline technology ---- */

typedef enum robo_font_outline_tech {
    ROBO_FONT_OUTLINE_UNKNOWN = 0,
    ROBO_FONT_OUTLINE_TRUETYPE = 1, /* glyf + loca */
    ROBO_FONT_OUTLINE_CFF = 2,      /* 'CFF ' table present */
    ROBO_FONT_OUTLINE_CFF2 = 3,     /* 'CFF2' table present */
    ROBO_FONT_OUTLINE_UNSUPPORTED = 4
} robo_font_outline_tech_t;

robo_font_outline_tech_t robo_font_ot_detect_outline(const robo_font_sfnt_t *sfnt);

/* ---- Table categories / registry helpers ---- */

typedef enum robo_font_ot_table_family {
    ROBO_FONT_OT_FAMILY_UNKNOWN = 0,
    ROBO_FONT_OT_FAMILY_CORE = 1,
    ROBO_FONT_OT_FAMILY_METRIC = 2,
    ROBO_FONT_OT_FAMILY_CMAP = 3,
    ROBO_FONT_OT_FAMILY_LAYOUT = 4,
    ROBO_FONT_OT_FAMILY_VARIATION = 5,
    ROBO_FONT_OT_FAMILY_COLOR = 6,
    ROBO_FONT_OT_FAMILY_MATH = 7,
    ROBO_FONT_OT_FAMILY_LEGACY = 8,
    ROBO_FONT_OT_FAMILY_OUTLINE = 9
} robo_font_ot_table_family_t;

robo_font_ot_table_family_t robo_font_ot_table_family(robo_font_tag_t tag);

/* ---- cmap structural (Goal 5.4; full mapping → 5.6) ---- */

typedef struct robo_font_cmap_encoding {
    uint16_t platform_id;
    uint16_t encoding_id;
    uint32_t subtable_offset; /* from cmap table start */
    uint16_t format;          /* 0,2,4,6,8,10,12,13,14,… */
    uint32_t length;          /* subtable length field when known */
    uint32_t language;        /* format-dependent */
} robo_font_cmap_encoding_t;

typedef struct robo_font_cmap {
    uint16_t version;
    uint16_t num_tables;
    robo_font_cmap_encoding_t *encodings; /* owned; num_tables */
    robo_font_span_t table;               /* view */
} robo_font_cmap_t;

void robo_font_cmap_clear(robo_font_cmap_t *c);

/** Structural parse: encoding records + per-subtable format/length validation. */
robo_result_t robo_font_cmap_parse(robo_font_span_t span, robo_font_cmap_t *out);

int robo_font_cmap_format_is_recognized(uint16_t format);

/* ---- GDEF / GSUB / GPOS ---- */

typedef struct robo_font_gdef {
    uint16_t major_version;
    uint16_t minor_version;
    uint8_t has_glyph_class_def;
    uint8_t has_attach_list;
    uint8_t has_lig_caret_list;
    uint8_t has_mark_attach_class_def;
    uint8_t has_mark_glyph_sets;
    uint8_t has_item_var_store;
    robo_font_ot_class_def_t glyph_class_def;
    robo_font_ot_class_def_t mark_attach_class_def;
    size_t attach_list_offset;
    size_t lig_caret_list_offset;
    size_t mark_glyph_sets_offset;
    size_t item_var_store_offset;
    robo_font_span_t table;
} robo_font_gdef_t;

void robo_font_gdef_clear(robo_font_gdef_t *g);
robo_result_t robo_font_gdef_parse(robo_font_span_t span, robo_font_gdef_t *out);

typedef enum robo_font_ot_layout_kind {
    ROBO_FONT_OT_LAYOUT_GSUB = 1,
    ROBO_FONT_OT_LAYOUT_GPOS = 2
} robo_font_ot_layout_kind_t;

typedef struct robo_font_ot_layout {
    robo_font_ot_layout_kind_t kind;
    uint16_t major_version;
    uint16_t minor_version;
    robo_font_ot_script_list_t scripts;
    robo_font_ot_feature_list_t features;
    robo_font_ot_lookup_list_t lookups;
    size_t feature_variations_offset; /* 0 if absent */
    robo_font_span_t table;
} robo_font_ot_layout_t;

void robo_font_ot_layout_clear(robo_font_ot_layout_t *L);
robo_result_t robo_font_gsub_parse(robo_font_span_t span, robo_font_ot_layout_t *out);
robo_result_t robo_font_gpos_parse(robo_font_span_t span, robo_font_ot_layout_t *out);

/* GSUB lookup types 1–8; GPOS 1–9 (extension = 7 GSUB / 9 GPOS). */
int robo_font_gsub_lookup_type_is_known(uint16_t type);
int robo_font_gpos_lookup_type_is_known(uint16_t type);

/* ---- BASE / JSTF / MATH / kern / COLR / CPAL / STAT (structural) ---- */

typedef struct robo_font_base {
    uint16_t major_version;
    uint16_t minor_version;
    uint8_t has_horiz;
    uint8_t has_vert;
    size_t horiz_axis_offset;
    size_t vert_axis_offset;
    size_t item_var_store_offset;
    robo_font_span_t table;
} robo_font_base_t;

robo_result_t robo_font_base_parse(robo_font_span_t span, robo_font_base_t *out);

typedef struct robo_font_jstf {
    uint16_t major_version;
    uint16_t minor_version;
    uint16_t script_count;
    robo_font_span_t table;
} robo_font_jstf_t;

robo_result_t robo_font_jstf_parse(robo_font_span_t span, robo_font_jstf_t *out);

typedef struct robo_font_math {
    uint32_t version; /* Fixed */
    size_t math_constants_offset;
    size_t math_glyph_info_offset;
    size_t math_variants_offset;
    robo_font_span_t table;
} robo_font_math_t;

robo_result_t robo_font_math_parse(robo_font_span_t span, robo_font_math_t *out);

typedef struct robo_font_kern_subtable {
    uint16_t version;
    uint16_t length;
    uint16_t coverage;
    uint16_t format; /* derived from coverage bits */
    size_t offset;   /* absolute in kern table */
} robo_font_kern_subtable_t;

typedef struct robo_font_kern {
    uint16_t version;
    uint16_t n_tables;
    robo_font_kern_subtable_t *subtables; /* owned */
    robo_font_span_t table;
} robo_font_kern_t;

void robo_font_kern_clear(robo_font_kern_t *k);
robo_result_t robo_font_kern_parse(robo_font_span_t span, robo_font_kern_t *out);

typedef struct robo_font_cpal {
    uint16_t version;
    uint16_t num_palettes_entries;
    uint16_t num_palette;
    uint16_t num_color_records;
    size_t color_records_array_offset;
    robo_font_span_t table;
} robo_font_cpal_t;

robo_result_t robo_font_cpal_parse(robo_font_span_t span, robo_font_cpal_t *out);

typedef struct robo_font_colr {
    uint16_t version;
    uint16_t num_base_glyph_records;
    size_t base_glyph_records_offset;
    size_t layer_records_offset;
    uint16_t num_layer_records;
    robo_font_span_t table;
} robo_font_colr_t;

robo_result_t robo_font_colr_parse(robo_font_span_t span, robo_font_colr_t *out);

typedef struct robo_font_stat_axis {
    robo_font_tag_t tag;
    uint16_t name_id;
    uint16_t ordering;
} robo_font_stat_axis_t;

typedef struct robo_font_stat {
    uint16_t major_version;
    uint16_t minor_version;
    uint16_t design_axis_size;
    uint16_t design_axis_count;
    size_t design_axes_offset;
    uint16_t axis_value_count;
    size_t offset_to_axis_values;
    uint16_t elided_fallback_name_id;
    robo_font_stat_axis_t *axes; /* owned; design_axis_count */
    robo_font_span_t table;
} robo_font_stat_t;

void robo_font_stat_clear(robo_font_stat_t *s);
robo_result_t robo_font_stat_parse(robo_font_span_t span, robo_font_stat_t *out);

/* ---- Bundled OpenType parse ---- */

typedef enum robo_font_ot_flags {
    ROBO_FONT_OT_FLAGS_NONE = 0,
    /** Parse layout tables when present (GDEF/GSUB/GPOS). */
    ROBO_FONT_OT_FLAGS_PARSE_LAYOUT = 1,
    /** Parse BASE/JSTF/MATH/kern/COLR/CPAL/STAT when present. */
    ROBO_FONT_OT_FLAGS_PARSE_EXTRA = 2,
    /** Parse cmap structurally when present. */
    ROBO_FONT_OT_FLAGS_PARSE_CMAP = 4,
    ROBO_FONT_OT_FLAGS_PARSE_ALL =
        (ROBO_FONT_OT_FLAGS_PARSE_LAYOUT | ROBO_FONT_OT_FLAGS_PARSE_EXTRA |
         ROBO_FONT_OT_FLAGS_PARSE_CMAP)
} robo_font_ot_flags_t;

typedef struct robo_font_ot robo_font_ot_t;

robo_result_t robo_font_ot_parse(robo_font_sfnt_t *sfnt,
                                 uint32_t flags,
                                 robo_font_ot_t **out);

void robo_font_ot_retain(robo_font_ot_t *ot);
void robo_font_ot_release(robo_font_ot_t *ot);

const robo_font_sfnt_t *robo_font_ot_sfnt(const robo_font_ot_t *ot);
robo_font_outline_tech_t robo_font_ot_outline(const robo_font_ot_t *ot);

const robo_font_cmap_t *robo_font_ot_cmap(const robo_font_ot_t *ot);
const robo_font_gdef_t *robo_font_ot_gdef(const robo_font_ot_t *ot);
const robo_font_ot_layout_t *robo_font_ot_gsub(const robo_font_ot_t *ot);
const robo_font_ot_layout_t *robo_font_ot_gpos(const robo_font_ot_t *ot);
const robo_font_base_t *robo_font_ot_base(const robo_font_ot_t *ot);
const robo_font_jstf_t *robo_font_ot_jstf(const robo_font_ot_t *ot);
const robo_font_math_t *robo_font_ot_math(const robo_font_ot_t *ot);
const robo_font_kern_t *robo_font_ot_kern(const robo_font_ot_t *ot);
const robo_font_colr_t *robo_font_ot_colr(const robo_font_ot_t *ot);
const robo_font_cpal_t *robo_font_ot_cpal(const robo_font_ot_t *ot);
const robo_font_stat_t *robo_font_ot_stat(const robo_font_ot_t *ot);

/** Fuzz-ready entry. */
robo_result_t robo_font_ot_fuzz_parse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_OT_H */
