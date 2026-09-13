#include "RoboFont/RoboFontOtCommon.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

robo_result_t robo_font_ot_resolve_offset16(robo_font_span_t table,
                                            size_t base,
                                            uint16_t offset,
                                            size_t *out_abs)
{
    size_t abs;
    if (!out_abs) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (base > table.size) {
        return ROBO_PARSE_ERROR;
    }
    abs = base + (size_t)offset;
    if (abs < base || abs > table.size) {
        return ROBO_PARSE_ERROR;
    }
    *out_abs = abs;
    return ROBO_OK;
}

robo_result_t robo_font_ot_resolve_offset32(robo_font_span_t table,
                                            size_t base,
                                            uint32_t offset,
                                            size_t *out_abs)
{
    size_t abs;
    if (!out_abs) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (base > table.size) {
        return ROBO_PARSE_ERROR;
    }
    abs = base + (size_t)offset;
    if (abs < base || abs > table.size) {
        return ROBO_PARSE_ERROR;
    }
    *out_abs = abs;
    return ROBO_OK;
}

void robo_font_ot_coverage_clear(robo_font_ot_coverage_t *c)
{
    if (!c) {
        return;
    }
    robo_free(c->glyphs);
    robo_free(c->ranges);
    memset(c, 0, sizeof(*c));
}

robo_result_t robo_font_ot_coverage_parse(robo_font_span_t table,
                                          size_t offset,
                                          robo_font_ot_coverage_t *out)
{
    robo_font_reader_t r;
    uint16_t format;
    uint16_t i;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_ot_coverage_clear(out);
    if (offset + 4u > table.size) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, table);
    if (robo_font_reader_seek(&r, offset) != ROBO_OK ||
        robo_font_read_u16be(&r, &format) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    out->format = format;

    if (format == 1u) {
        uint16_t count;
        if (robo_font_read_u16be(&r, &count) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (count > ROBO_FONT_OT_MAX_COVERAGE_GLYPHS) {
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_checked_range(table.size, robo_font_reader_pos(&r),
                                    (size_t)count * 2u) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        out->glyph_count = count;
        if (count == 0u) {
            return ROBO_OK;
        }
        out->glyphs = (uint16_t *)robo_calloc(count, sizeof(uint16_t));
        if (!out->glyphs) {
            return ROBO_OUT_OF_MEMORY;
        }
        for (i = 0u; i < count; i++) {
            if (robo_font_read_u16be(&r, &out->glyphs[i]) != ROBO_OK) {
                robo_font_ot_coverage_clear(out);
                return ROBO_PARSE_ERROR;
            }
            if (i > 0u && out->glyphs[i] <= out->glyphs[i - 1u]) {
                robo_font_ot_coverage_clear(out);
                return ROBO_PARSE_ERROR;
            }
        }
        return ROBO_OK;
    }

    if (format == 2u) {
        uint16_t range_count;
        uint32_t covered = 0u;
        if (robo_font_read_u16be(&r, &range_count) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (range_count > ROBO_FONT_OT_MAX_CLASS_RANGES) {
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_checked_range(table.size, robo_font_reader_pos(&r),
                                    (size_t)range_count * 6u) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        out->range_count = range_count;
        if (range_count == 0u) {
            return ROBO_OK;
        }
        out->ranges = (robo_font_ot_coverage_range_t *)robo_calloc(
            range_count, sizeof(robo_font_ot_coverage_range_t));
        if (!out->ranges) {
            return ROBO_OUT_OF_MEMORY;
        }
        for (i = 0u; i < range_count; i++) {
            uint16_t start_g;
            uint16_t end_g;
            uint16_t start_ci;
            if (robo_font_read_u16be(&r, &start_g) != ROBO_OK ||
                robo_font_read_u16be(&r, &end_g) != ROBO_OK ||
                robo_font_read_u16be(&r, &start_ci) != ROBO_OK) {
                robo_font_ot_coverage_clear(out);
                return ROBO_PARSE_ERROR;
            }
            if (start_g > end_g) {
                robo_font_ot_coverage_clear(out);
                return ROBO_PARSE_ERROR;
            }
            if (i > 0u && start_g <= out->ranges[i - 1u].end_glyph) {
                robo_font_ot_coverage_clear(out);
                return ROBO_PARSE_ERROR;
            }
            out->ranges[i].start_glyph = start_g;
            out->ranges[i].end_glyph = end_g;
            out->ranges[i].start_coverage_index = start_ci;
            covered += (uint32_t)(end_g - start_g) + 1u;
            if (covered > ROBO_FONT_OT_MAX_COVERAGE_GLYPHS) {
                robo_font_ot_coverage_clear(out);
                return ROBO_PARSE_ERROR;
            }
        }
        out->glyph_count = (uint16_t)covered;
        return ROBO_OK;
    }

    /* Coverage formats are only 1 and 2; anything else is malformed. */
    return ROBO_PARSE_ERROR;
}

robo_result_t robo_font_ot_coverage_index(const robo_font_ot_coverage_t *c,
                                          uint16_t glyph_id,
                                          uint16_t *out_index)
{
    if (!c) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (c->format == 1u) {
        uint16_t lo = 0u;
        uint16_t hi = c->glyph_count;
        while (lo < hi) {
            uint16_t mid = (uint16_t)(lo + (hi - lo) / 2u);
            if (c->glyphs[mid] < glyph_id) {
                lo = (uint16_t)(mid + 1u);
            } else if (c->glyphs[mid] > glyph_id) {
                hi = mid;
            } else {
                if (out_index) {
                    *out_index = mid;
                }
                return ROBO_OK;
            }
        }
        return ROBO_NOT_FOUND;
    }
    if (c->format == 2u) {
        uint16_t i;
        for (i = 0u; i < c->range_count; i++) {
            const robo_font_ot_coverage_range_t *rg = &c->ranges[i];
            if (glyph_id < rg->start_glyph) {
                return ROBO_NOT_FOUND;
            }
            if (glyph_id <= rg->end_glyph) {
                if (out_index) {
                    *out_index =
                        (uint16_t)(rg->start_coverage_index + (glyph_id - rg->start_glyph));
                }
                return ROBO_OK;
            }
        }
        return ROBO_NOT_FOUND;
    }
    return ROBO_NOT_SUPPORTED;
}

void robo_font_ot_class_def_clear(robo_font_ot_class_def_t *c)
{
    if (!c) {
        return;
    }
    robo_free(c->class_values);
    robo_free(c->ranges);
    memset(c, 0, sizeof(*c));
}

robo_result_t robo_font_ot_class_def_parse(robo_font_span_t table,
                                           size_t offset,
                                           robo_font_ot_class_def_t *out)
{
    robo_font_reader_t r;
    uint16_t format;
    uint16_t i;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_ot_class_def_clear(out);
    if (offset + 2u > table.size) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, table);
    if (robo_font_reader_seek(&r, offset) != ROBO_OK ||
        robo_font_read_u16be(&r, &format) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    out->format = format;

    if (format == 1u) {
        uint16_t start;
        uint16_t count;
        if (robo_font_read_u16be(&r, &start) != ROBO_OK ||
            robo_font_read_u16be(&r, &count) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (count > ROBO_FONT_OT_MAX_COVERAGE_GLYPHS) {
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_checked_range(table.size, robo_font_reader_pos(&r),
                                    (size_t)count * 2u) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        out->start_glyph = start;
        out->glyph_count = count;
        if (count == 0u) {
            return ROBO_OK;
        }
        out->class_values = (uint16_t *)robo_calloc(count, sizeof(uint16_t));
        if (!out->class_values) {
            return ROBO_OUT_OF_MEMORY;
        }
        for (i = 0u; i < count; i++) {
            if (robo_font_read_u16be(&r, &out->class_values[i]) != ROBO_OK) {
                robo_font_ot_class_def_clear(out);
                return ROBO_PARSE_ERROR;
            }
        }
        return ROBO_OK;
    }

    if (format == 2u) {
        uint16_t range_count;
        if (robo_font_read_u16be(&r, &range_count) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (range_count > ROBO_FONT_OT_MAX_CLASS_RANGES) {
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_checked_range(table.size, robo_font_reader_pos(&r),
                                    (size_t)range_count * 6u) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        out->range_count = range_count;
        if (range_count == 0u) {
            return ROBO_OK;
        }
        out->ranges = (robo_font_ot_class_range_t *)robo_calloc(
            range_count, sizeof(robo_font_ot_class_range_t));
        if (!out->ranges) {
            return ROBO_OUT_OF_MEMORY;
        }
        for (i = 0u; i < range_count; i++) {
            if (robo_font_read_u16be(&r, &out->ranges[i].start_glyph) != ROBO_OK ||
                robo_font_read_u16be(&r, &out->ranges[i].end_glyph) != ROBO_OK ||
                robo_font_read_u16be(&r, &out->ranges[i].class_value) != ROBO_OK) {
                robo_font_ot_class_def_clear(out);
                return ROBO_PARSE_ERROR;
            }
            if (out->ranges[i].start_glyph > out->ranges[i].end_glyph) {
                robo_font_ot_class_def_clear(out);
                return ROBO_PARSE_ERROR;
            }
            if (i > 0u &&
                out->ranges[i].start_glyph <= out->ranges[i - 1u].end_glyph) {
                robo_font_ot_class_def_clear(out);
                return ROBO_PARSE_ERROR;
            }
        }
        return ROBO_OK;
    }

    /* ClassDef formats are only 1 and 2; anything else is malformed. */
    return ROBO_PARSE_ERROR;
}

robo_result_t robo_font_ot_class_def_get(const robo_font_ot_class_def_t *c,
                                         uint16_t glyph_id,
                                         uint16_t *out_class)
{
    if (!c || !out_class) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_class = 0u;
    if (c->format == 1u) {
        if (glyph_id < c->start_glyph) {
            return ROBO_OK;
        }
        {
            uint32_t idx = (uint32_t)glyph_id - (uint32_t)c->start_glyph;
            if (idx >= c->glyph_count) {
                return ROBO_OK;
            }
            *out_class = c->class_values[idx];
        }
        return ROBO_OK;
    }
    if (c->format == 2u) {
        uint16_t i;
        for (i = 0u; i < c->range_count; i++) {
            if (glyph_id < c->ranges[i].start_glyph) {
                break;
            }
            if (glyph_id <= c->ranges[i].end_glyph) {
                *out_class = c->ranges[i].class_value;
                break;
            }
        }
        return ROBO_OK;
    }
    return ROBO_NOT_SUPPORTED;
}

void robo_font_ot_device_clear(robo_font_ot_device_t *d)
{
    if (!d) {
        return;
    }
    robo_free(d->delta_values);
    memset(d, 0, sizeof(*d));
}

robo_result_t robo_font_ot_device_parse(robo_font_span_t table,
                                        size_t offset,
                                        robo_font_ot_device_t *out)
{
    robo_font_reader_t r;
    uint16_t nSizes;
    uint16_t words;
    uint16_t i;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_ot_device_clear(out);
    if (offset + 6u > table.size) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, table);
    if (robo_font_reader_seek(&r, offset) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->start_size) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->end_size) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->delta_format) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (out->delta_format == 0x8000u) {
        /* VariationIndex: two more uint16 (outer/inner) — already read format. */
        if (robo_font_reader_pos(&r) + 4u > table.size) {
            return ROBO_PARSE_ERROR;
        }
        out->delta_value_count = 2u;
        out->delta_values = (uint16_t *)robo_calloc(2u, sizeof(uint16_t));
        if (!out->delta_values) {
            return ROBO_OUT_OF_MEMORY;
        }
        if (robo_font_read_u16be(&r, &out->delta_values[0]) != ROBO_OK ||
            robo_font_read_u16be(&r, &out->delta_values[1]) != ROBO_OK) {
            robo_font_ot_device_clear(out);
            return ROBO_PARSE_ERROR;
        }
        return ROBO_OK;
    }
    if (out->delta_format < 1u || out->delta_format > 3u) {
        return ROBO_NOT_SUPPORTED;
    }
    if (out->start_size > out->end_size) {
        return ROBO_PARSE_ERROR;
    }
    nSizes = (uint16_t)(out->end_size - out->start_size + 1u);
    /* Packed: format 1 → 8/uint16, 2 → 4/uint16, 3 → 2/uint16 */
    {
        uint16_t per_word = (uint16_t)(1u << (4u - out->delta_format));
        words = (uint16_t)((nSizes + per_word - 1u) / per_word);
    }
    if (words > 4096u) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_checked_range(table.size, robo_font_reader_pos(&r),
                                (size_t)words * 2u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    out->delta_value_count = words;
    out->delta_values = (uint16_t *)robo_calloc(words == 0u ? 1u : words, sizeof(uint16_t));
    if (!out->delta_values) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0u; i < words; i++) {
        if (robo_font_read_u16be(&r, &out->delta_values[i]) != ROBO_OK) {
            robo_font_ot_device_clear(out);
            return ROBO_PARSE_ERROR;
        }
    }
    return ROBO_OK;
}

static void lang_sys_clear(robo_font_ot_lang_sys_t *ls)
{
    if (!ls) {
        return;
    }
    robo_free(ls->feature_indices);
    memset(ls, 0, sizeof(*ls));
}

static robo_result_t lang_sys_parse(robo_font_span_t table,
                                    size_t offset,
                                    robo_font_ot_lang_sys_t *out)
{
    robo_font_reader_t r;
    uint16_t i;
    uint16_t count;

    memset(out, 0, sizeof(*out));
    if (offset + 6u > table.size) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, table);
    if (robo_font_reader_seek(&r, offset) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->lookup_order) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->required_feature_index) != ROBO_OK ||
        robo_font_read_u16be(&r, &count) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (count > ROBO_FONT_OT_MAX_FEATURE_LOOKUPS) {
        return ROBO_PARSE_ERROR;
    }
    out->feature_index_count = count;
    if (robo_font_checked_range(table.size, robo_font_reader_pos(&r),
                                (size_t)count * 2u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (count == 0u) {
        return ROBO_OK;
    }
    out->feature_indices = (uint16_t *)robo_calloc(count, sizeof(uint16_t));
    if (!out->feature_indices) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0u; i < count; i++) {
        if (robo_font_read_u16be(&r, &out->feature_indices[i]) != ROBO_OK) {
            lang_sys_clear(out);
            return ROBO_PARSE_ERROR;
        }
    }
    return ROBO_OK;
}

void robo_font_ot_script_list_clear(robo_font_ot_script_list_t *sl)
{
    uint16_t i;
    uint16_t j;
    if (!sl) {
        return;
    }
    if (sl->scripts) {
        for (i = 0u; i < sl->script_count; i++) {
            lang_sys_clear(&sl->scripts[i].default_lang_sys);
            if (sl->scripts[i].lang_sys_records) {
                for (j = 0u; j < sl->scripts[i].lang_sys_count; j++) {
                    lang_sys_clear(&sl->scripts[i].lang_sys_records[j].lang_sys);
                }
                robo_free(sl->scripts[i].lang_sys_records);
            }
        }
        robo_free(sl->scripts);
    }
    memset(sl, 0, sizeof(*sl));
}

robo_result_t robo_font_ot_script_list_parse(robo_font_span_t table,
                                             size_t offset,
                                             robo_font_ot_script_list_t *out)
{
    robo_font_reader_t r;
    uint16_t count;
    uint16_t i;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_ot_script_list_clear(out);
    if (offset + 2u > table.size) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, table);
    if (robo_font_reader_seek(&r, offset) != ROBO_OK ||
        robo_font_read_u16be(&r, &count) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (count > ROBO_FONT_OT_MAX_SCRIPTS) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_checked_range(table.size, robo_font_reader_pos(&r),
                                (size_t)count * 6u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    out->script_count = count;
    if (count == 0u) {
        return ROBO_OK;
    }
    out->scripts = (robo_font_ot_script_t *)robo_calloc(count, sizeof(robo_font_ot_script_t));
    if (!out->scripts) {
        return ROBO_OUT_OF_MEMORY;
    }

    for (i = 0u; i < count; i++) {
        uint32_t tag;
        uint16_t script_off;
        size_t script_abs;
        uint16_t default_off;
        uint16_t lang_count;
        uint16_t j;
        robo_font_reader_t sr;

        if (robo_font_read_u32be(&r, &tag) != ROBO_OK ||
            robo_font_read_u16be(&r, &script_off) != ROBO_OK) {
            robo_font_ot_script_list_clear(out);
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_ot_resolve_offset16(table, offset, script_off, &script_abs) !=
            ROBO_OK) {
            robo_font_ot_script_list_clear(out);
            return ROBO_PARSE_ERROR;
        }
        out->scripts[i].script_tag = tag;
        out->scripts[i].script_offset = script_abs;

        if (script_abs + 4u > table.size) {
            robo_font_ot_script_list_clear(out);
            return ROBO_PARSE_ERROR;
        }
        robo_font_reader_init_span(&sr, table);
        if (robo_font_reader_seek(&sr, script_abs) != ROBO_OK ||
            robo_font_read_u16be(&sr, &default_off) != ROBO_OK ||
            robo_font_read_u16be(&sr, &lang_count) != ROBO_OK) {
            robo_font_ot_script_list_clear(out);
            return ROBO_PARSE_ERROR;
        }
        if (lang_count > ROBO_FONT_OT_MAX_LANGSYS) {
            robo_font_ot_script_list_clear(out);
            return ROBO_PARSE_ERROR;
        }
        if (default_off != 0u) {
            size_t dabs;
            if (robo_font_ot_resolve_offset16(table, script_abs, default_off, &dabs) !=
                    ROBO_OK ||
                lang_sys_parse(table, dabs, &out->scripts[i].default_lang_sys) != ROBO_OK) {
                robo_font_ot_script_list_clear(out);
                return ROBO_PARSE_ERROR;
            }
            out->scripts[i].has_default_lang_sys = 1u;
        }
        out->scripts[i].lang_sys_count = lang_count;
        if (robo_font_checked_range(table.size, robo_font_reader_pos(&sr),
                                    (size_t)lang_count * 6u) != ROBO_OK) {
            robo_font_ot_script_list_clear(out);
            return ROBO_PARSE_ERROR;
        }
        if (lang_count == 0u) {
            continue;
        }
        out->scripts[i].lang_sys_records = (robo_font_ot_lang_sys_record_t *)robo_calloc(
            lang_count, sizeof(robo_font_ot_lang_sys_record_t));
        if (!out->scripts[i].lang_sys_records) {
            robo_font_ot_script_list_clear(out);
            return ROBO_OUT_OF_MEMORY;
        }
        for (j = 0u; j < lang_count; j++) {
            uint32_t ltag;
            uint16_t loff;
            size_t labs;
            if (robo_font_read_u32be(&sr, &ltag) != ROBO_OK ||
                robo_font_read_u16be(&sr, &loff) != ROBO_OK) {
                robo_font_ot_script_list_clear(out);
                return ROBO_PARSE_ERROR;
            }
            if (robo_font_ot_resolve_offset16(table, script_abs, loff, &labs) != ROBO_OK ||
                lang_sys_parse(table, labs,
                               &out->scripts[i].lang_sys_records[j].lang_sys) != ROBO_OK) {
                robo_font_ot_script_list_clear(out);
                return ROBO_PARSE_ERROR;
            }
            out->scripts[i].lang_sys_records[j].lang_tag = ltag;
            out->scripts[i].lang_sys_records[j].lang_sys_offset = labs;
        }
    }
    return ROBO_OK;
}

void robo_font_ot_feature_list_clear(robo_font_ot_feature_list_t *fl)
{
    uint16_t i;
    if (!fl) {
        return;
    }
    if (fl->features) {
        for (i = 0u; i < fl->feature_count; i++) {
            robo_free(fl->features[i].lookup_indices);
        }
        robo_free(fl->features);
    }
    memset(fl, 0, sizeof(*fl));
}

robo_result_t robo_font_ot_feature_list_parse(robo_font_span_t table,
                                              size_t offset,
                                              robo_font_ot_feature_list_t *out)
{
    robo_font_reader_t r;
    uint16_t count;
    uint16_t i;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_ot_feature_list_clear(out);
    if (offset + 2u > table.size) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, table);
    if (robo_font_reader_seek(&r, offset) != ROBO_OK ||
        robo_font_read_u16be(&r, &count) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (count > ROBO_FONT_OT_MAX_FEATURES) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_checked_range(table.size, robo_font_reader_pos(&r),
                                (size_t)count * 6u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    out->feature_count = count;
    if (count == 0u) {
        return ROBO_OK;
    }
    out->features =
        (robo_font_ot_feature_t *)robo_calloc(count, sizeof(robo_font_ot_feature_t));
    if (!out->features) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0u; i < count; i++) {
        uint32_t tag;
        uint16_t foff;
        size_t fabs;
        robo_font_reader_t fr;
        uint16_t params;
        uint16_t lcount;
        uint16_t j;

        if (robo_font_read_u32be(&r, &tag) != ROBO_OK ||
            robo_font_read_u16be(&r, &foff) != ROBO_OK) {
            robo_font_ot_feature_list_clear(out);
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_ot_resolve_offset16(table, offset, foff, &fabs) != ROBO_OK) {
            robo_font_ot_feature_list_clear(out);
            return ROBO_PARSE_ERROR;
        }
        out->features[i].feature_tag = tag;
        out->features[i].feature_offset = fabs;
        if (fabs + 4u > table.size) {
            robo_font_ot_feature_list_clear(out);
            return ROBO_PARSE_ERROR;
        }
        robo_font_reader_init_span(&fr, table);
        if (robo_font_reader_seek(&fr, fabs) != ROBO_OK ||
            robo_font_read_u16be(&fr, &params) != ROBO_OK ||
            robo_font_read_u16be(&fr, &lcount) != ROBO_OK) {
            robo_font_ot_feature_list_clear(out);
            return ROBO_PARSE_ERROR;
        }
        if (lcount > ROBO_FONT_OT_MAX_FEATURE_LOOKUPS) {
            robo_font_ot_feature_list_clear(out);
            return ROBO_PARSE_ERROR;
        }
        out->features[i].feature_params_offset = params;
        out->features[i].lookup_index_count = lcount;
        if (robo_font_checked_range(table.size, robo_font_reader_pos(&fr),
                                    (size_t)lcount * 2u) != ROBO_OK) {
            robo_font_ot_feature_list_clear(out);
            return ROBO_PARSE_ERROR;
        }
        if (lcount == 0u) {
            continue;
        }
        out->features[i].lookup_indices =
            (uint16_t *)robo_calloc(lcount, sizeof(uint16_t));
        if (!out->features[i].lookup_indices) {
            robo_font_ot_feature_list_clear(out);
            return ROBO_OUT_OF_MEMORY;
        }
        for (j = 0u; j < lcount; j++) {
            if (robo_font_read_u16be(&fr, &out->features[i].lookup_indices[j]) != ROBO_OK) {
                robo_font_ot_feature_list_clear(out);
                return ROBO_PARSE_ERROR;
            }
        }
    }
    return ROBO_OK;
}

void robo_font_ot_lookup_list_clear(robo_font_ot_lookup_list_t *ll)
{
    uint16_t i;
    if (!ll) {
        return;
    }
    if (ll->lookups) {
        for (i = 0u; i < ll->lookup_count; i++) {
            robo_free(ll->lookups[i].subtables);
        }
        robo_free(ll->lookups);
    }
    memset(ll, 0, sizeof(*ll));
}

robo_result_t robo_font_ot_lookup_list_parse(robo_font_span_t table,
                                             size_t offset,
                                             robo_font_ot_lookup_list_t *out)
{
    robo_font_reader_t r;
    uint16_t count;
    uint16_t i;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_ot_lookup_list_clear(out);
    if (offset + 2u > table.size) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, table);
    if (robo_font_reader_seek(&r, offset) != ROBO_OK ||
        robo_font_read_u16be(&r, &count) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (count > ROBO_FONT_OT_MAX_LOOKUPS) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_checked_range(table.size, robo_font_reader_pos(&r),
                                (size_t)count * 2u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    out->lookup_count = count;
    if (count == 0u) {
        return ROBO_OK;
    }
    out->lookups =
        (robo_font_ot_lookup_t *)robo_calloc(count, sizeof(robo_font_ot_lookup_t));
    if (!out->lookups) {
        return ROBO_OUT_OF_MEMORY;
    }

    for (i = 0u; i < count; i++) {
        uint16_t loff;
        size_t labs;
        robo_font_reader_t lr;
        uint16_t type;
        uint16_t flags;
        uint16_t scount;
        uint16_t j;

        if (robo_font_read_u16be(&r, &loff) != ROBO_OK) {
            robo_font_ot_lookup_list_clear(out);
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_ot_resolve_offset16(table, offset, loff, &labs) != ROBO_OK) {
            robo_font_ot_lookup_list_clear(out);
            return ROBO_PARSE_ERROR;
        }
        out->lookups[i].lookup_offset = labs;
        if (labs + 6u > table.size) {
            robo_font_ot_lookup_list_clear(out);
            return ROBO_PARSE_ERROR;
        }
        robo_font_reader_init_span(&lr, table);
        if (robo_font_reader_seek(&lr, labs) != ROBO_OK ||
            robo_font_read_u16be(&lr, &type) != ROBO_OK ||
            robo_font_read_u16be(&lr, &flags) != ROBO_OK ||
            robo_font_read_u16be(&lr, &scount) != ROBO_OK) {
            robo_font_ot_lookup_list_clear(out);
            return ROBO_PARSE_ERROR;
        }
        if (scount == 0u || scount > ROBO_FONT_OT_MAX_SUBTABLES) {
            robo_font_ot_lookup_list_clear(out);
            return ROBO_PARSE_ERROR;
        }
        out->lookups[i].lookup_type = type;
        out->lookups[i].lookup_flag = flags;
        out->lookups[i].subtable_count = scount;
        if (robo_font_checked_range(table.size, robo_font_reader_pos(&lr),
                                    (size_t)scount * 2u) != ROBO_OK) {
            robo_font_ot_lookup_list_clear(out);
            return ROBO_PARSE_ERROR;
        }
        out->lookups[i].subtables = (robo_font_ot_lookup_subtable_t *)robo_calloc(
            scount, sizeof(robo_font_ot_lookup_subtable_t));
        if (!out->lookups[i].subtables) {
            robo_font_ot_lookup_list_clear(out);
            return ROBO_OUT_OF_MEMORY;
        }
        for (j = 0u; j < scount; j++) {
            uint16_t soff;
            size_t sabs;
            if (robo_font_read_u16be(&lr, &soff) != ROBO_OK) {
                robo_font_ot_lookup_list_clear(out);
                return ROBO_PARSE_ERROR;
            }
            if (robo_font_ot_resolve_offset16(table, labs, soff, &sabs) != ROBO_OK) {
                robo_font_ot_lookup_list_clear(out);
                return ROBO_PARSE_ERROR;
            }
            out->lookups[i].subtables[j].offset = sabs;
            out->lookups[i].subtables[j].length_hint =
                (uint32_t)(table.size - sabs);
        }
        if ((flags & ROBO_FONT_OT_LOOKUP_FLAG_USE_MARK_FILTERING_SET) != 0u) {
            if (robo_font_read_u16be(&lr, &out->lookups[i].mark_filtering_set) != ROBO_OK) {
                robo_font_ot_lookup_list_clear(out);
                return ROBO_PARSE_ERROR;
            }
        }
    }
    return ROBO_OK;
}
