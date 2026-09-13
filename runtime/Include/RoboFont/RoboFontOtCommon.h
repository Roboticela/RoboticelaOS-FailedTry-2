/*
 * RoboFont OpenType common structures (Goal 5.4).
 *
 * Shared layout formats: Coverage, ClassDef, Device, ScriptList, FeatureList,
 * LookupList. Structural only — no substitution/positioning execution.
 */

#ifndef ROBO_FONT_OT_COMMON_H
#define ROBO_FONT_OT_COMMON_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBO_FONT_OT_MAX_SCRIPTS 256u
#define ROBO_FONT_OT_MAX_LANGSYS 256u
#define ROBO_FONT_OT_MAX_FEATURES 1024u
#define ROBO_FONT_OT_MAX_LOOKUPS 2048u
#define ROBO_FONT_OT_MAX_SUBTABLES 256u
#define ROBO_FONT_OT_MAX_COVERAGE_GLYPHS 65535u
#define ROBO_FONT_OT_MAX_CLASS_RANGES 8192u
#define ROBO_FONT_OT_MAX_FEATURE_LOOKUPS 256u

/** Resolve Offset16 from @p base within @p table; result span starts at base+off. */
robo_result_t robo_font_ot_resolve_offset16(robo_font_span_t table,
                                            size_t base,
                                            uint16_t offset,
                                            size_t *out_abs);

robo_result_t robo_font_ot_resolve_offset32(robo_font_span_t table,
                                            size_t base,
                                            uint32_t offset,
                                            size_t *out_abs);

/* ---- Coverage ---- */

typedef enum robo_font_ot_coverage_format {
    ROBO_FONT_OT_COVERAGE_FORMAT_1 = 1,
    ROBO_FONT_OT_COVERAGE_FORMAT_2 = 2
} robo_font_ot_coverage_format_t;

typedef struct robo_font_ot_coverage_range {
    uint16_t start_glyph;
    uint16_t end_glyph;
    uint16_t start_coverage_index;
} robo_font_ot_coverage_range_t;

typedef struct robo_font_ot_coverage {
    uint16_t format;
    uint16_t glyph_count; /* format 1: array length; format 2: covered glyph count */
    uint16_t range_count; /* format 2 */
    uint16_t *glyphs;     /* format 1; owned */
    robo_font_ot_coverage_range_t *ranges; /* format 2; owned */
} robo_font_ot_coverage_t;

void robo_font_ot_coverage_clear(robo_font_ot_coverage_t *c);

/** Parse Coverage at absolute @p offset within @p table. */
robo_result_t robo_font_ot_coverage_parse(robo_font_span_t table,
                                          size_t offset,
                                          robo_font_ot_coverage_t *out);

/**
 * Coverage index for @p glyph_id, or ROBO_NOT_FOUND if not covered.
 * @p out_index may be NULL.
 */
robo_result_t robo_font_ot_coverage_index(const robo_font_ot_coverage_t *c,
                                          uint16_t glyph_id,
                                          uint16_t *out_index);

/* ---- ClassDef ---- */

typedef struct robo_font_ot_class_range {
    uint16_t start_glyph;
    uint16_t end_glyph;
    uint16_t class_value;
} robo_font_ot_class_range_t;

typedef struct robo_font_ot_class_def {
    uint16_t format;
    uint16_t start_glyph; /* format 1 */
    uint16_t glyph_count; /* format 1 */
    uint16_t *class_values; /* format 1; owned */
    uint16_t range_count; /* format 2 */
    robo_font_ot_class_range_t *ranges; /* format 2; owned */
} robo_font_ot_class_def_t;

void robo_font_ot_class_def_clear(robo_font_ot_class_def_t *c);

robo_result_t robo_font_ot_class_def_parse(robo_font_span_t table,
                                           size_t offset,
                                           robo_font_ot_class_def_t *out);

/** Class for glyph (0 if unlisted). */
robo_result_t robo_font_ot_class_def_get(const robo_font_ot_class_def_t *c,
                                         uint16_t glyph_id,
                                         uint16_t *out_class);

/* ---- Device / VariationIndex (structural) ---- */

typedef struct robo_font_ot_device {
    uint16_t start_size;
    uint16_t end_size;
    uint16_t delta_format; /* 1..3 delta, 0x8000 = variation index */
    uint16_t delta_value_count;
    uint16_t *delta_values; /* owned; packed units as uint16 words */
} robo_font_ot_device_t;

void robo_font_ot_device_clear(robo_font_ot_device_t *d);

robo_result_t robo_font_ot_device_parse(robo_font_span_t table,
                                        size_t offset,
                                        robo_font_ot_device_t *out);

/* ---- Script / Feature / Lookup lists ---- */

typedef struct robo_font_ot_lang_sys {
    uint16_t lookup_order; /* always 0 / reserved */
    uint16_t required_feature_index; /* 0xFFFF = none */
    uint16_t feature_index_count;
    uint16_t *feature_indices; /* owned */
} robo_font_ot_lang_sys_t;

typedef struct robo_font_ot_lang_sys_record {
    robo_font_tag_t lang_tag;
    size_t lang_sys_offset; /* absolute in table */
    robo_font_ot_lang_sys_t lang_sys;
} robo_font_ot_lang_sys_record_t;

typedef struct robo_font_ot_script {
    robo_font_tag_t script_tag;
    size_t script_offset;
    uint8_t has_default_lang_sys;
    robo_font_ot_lang_sys_t default_lang_sys;
    uint16_t lang_sys_count;
    robo_font_ot_lang_sys_record_t *lang_sys_records; /* owned */
} robo_font_ot_script_t;

typedef struct robo_font_ot_script_list {
    uint16_t script_count;
    robo_font_ot_script_t *scripts; /* owned */
} robo_font_ot_script_list_t;

void robo_font_ot_script_list_clear(robo_font_ot_script_list_t *sl);

robo_result_t robo_font_ot_script_list_parse(robo_font_span_t table,
                                             size_t offset,
                                             robo_font_ot_script_list_t *out);

typedef struct robo_font_ot_feature {
    robo_font_tag_t feature_tag;
    size_t feature_offset;
    uint16_t feature_params_offset; /* 0 = none; relative to Feature */
    uint16_t lookup_index_count;
    uint16_t *lookup_indices; /* owned */
} robo_font_ot_feature_t;

typedef struct robo_font_ot_feature_list {
    uint16_t feature_count;
    robo_font_ot_feature_t *features; /* owned */
} robo_font_ot_feature_list_t;

void robo_font_ot_feature_list_clear(robo_font_ot_feature_list_t *fl);

robo_result_t robo_font_ot_feature_list_parse(robo_font_span_t table,
                                              size_t offset,
                                              robo_font_ot_feature_list_t *out);

#define ROBO_FONT_OT_LOOKUP_FLAG_RTL 0x0001u
#define ROBO_FONT_OT_LOOKUP_FLAG_IGNORE_BASE 0x0002u
#define ROBO_FONT_OT_LOOKUP_FLAG_IGNORE_LIGATURES 0x0004u
#define ROBO_FONT_OT_LOOKUP_FLAG_IGNORE_MARKS 0x0008u
#define ROBO_FONT_OT_LOOKUP_FLAG_USE_MARK_FILTERING_SET 0x0010u
#define ROBO_FONT_OT_LOOKUP_FLAG_MARK_ATTACHMENT_TYPE_MASK 0xFF00u

typedef struct robo_font_ot_lookup_subtable {
    size_t offset; /* absolute in parent GSUB/GPOS table */
    uint32_t length_hint; /* 0 if unknown; remaining to table end if not set */
} robo_font_ot_lookup_subtable_t;

typedef struct robo_font_ot_lookup {
    uint16_t lookup_type;
    uint16_t lookup_flag;
    uint16_t mark_filtering_set; /* valid if USE_MARK_FILTERING_SET */
    uint16_t subtable_count;
    robo_font_ot_lookup_subtable_t *subtables; /* owned */
    size_t lookup_offset;
} robo_font_ot_lookup_t;

typedef struct robo_font_ot_lookup_list {
    uint16_t lookup_count;
    robo_font_ot_lookup_t *lookups; /* owned */
} robo_font_ot_lookup_list_t;

void robo_font_ot_lookup_list_clear(robo_font_ot_lookup_list_t *ll);

robo_result_t robo_font_ot_lookup_list_parse(robo_font_span_t table,
                                             size_t offset,
                                             robo_font_ot_lookup_list_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_OT_COMMON_H */
