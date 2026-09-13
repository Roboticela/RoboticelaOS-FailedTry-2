#include "RoboFont/RoboFontTt.h"

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicode.h"

#include <stddef.h>
#include <string.h>

/* ---- helpers ---- */

static robo_result_t read_i64be(robo_font_reader_t *r, int64_t *out)
{
    uint32_t hi;
    uint32_t lo;
    if (robo_font_read_u32be(r, &hi) != ROBO_OK || robo_font_read_u32be(r, &lo) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = ((int64_t)(uint64_t)hi << 32) | (int64_t)(uint64_t)lo;
    return ROBO_OK;
}

robo_result_t robo_font_head_parse(robo_font_span_t span, robo_font_head_t *out)
{
    robo_font_reader_t r;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (span.size < 54u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u16be(&r, &out->major_version) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->minor_version) != ROBO_OK ||
        robo_font_read_u32be(&r, &out->font_revision) != ROBO_OK ||
        robo_font_read_u32be(&r, &out->checksum_adjustment) != ROBO_OK ||
        robo_font_read_u32be(&r, &out->magic_number) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->flags) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->units_per_em) != ROBO_OK ||
        read_i64be(&r, &out->created) != ROBO_OK ||
        read_i64be(&r, &out->modified) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->x_min) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->y_min) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->x_max) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->y_max) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->mac_style) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->lowest_rec_ppem) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->font_direction_hint) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->index_to_loc_format) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->glyph_data_format) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (out->major_version != 1u || out->minor_version != 0u) {
        return ROBO_PARSE_ERROR;
    }
    if (out->magic_number != ROBO_FONT_HEAD_MAGIC) {
        return ROBO_PARSE_ERROR;
    }
    if (out->units_per_em < 16u || out->units_per_em > 16384u) {
        return ROBO_PARSE_ERROR;
    }
    if (out->x_min > out->x_max || out->y_min > out->y_max) {
        return ROBO_PARSE_ERROR;
    }
    if (out->index_to_loc_format != 0 && out->index_to_loc_format != 1) {
        return ROBO_PARSE_ERROR;
    }
    if (out->glyph_data_format != 0) {
        return ROBO_PARSE_ERROR;
    }
    return ROBO_OK;
}

robo_result_t robo_font_hhea_parse(robo_font_span_t span, robo_font_hhea_t *out)
{
    robo_font_reader_t r;
    int16_t reserved;
    int i;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (span.size < 36u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u16be(&r, &out->major_version) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->minor_version) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->ascender) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->descender) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->line_gap) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->advance_width_max) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->min_left_side_bearing) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->min_right_side_bearing) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->x_max_extent) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->caret_slope_rise) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->caret_slope_run) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->caret_offset) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    for (i = 0; i < 4; i++) {
        if (robo_font_read_i16be(&r, &reserved) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
    }
    if (robo_font_read_i16be(&r, &out->metric_data_format) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->number_of_h_metrics) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (out->major_version != 1u || out->minor_version != 0u) {
        return ROBO_PARSE_ERROR;
    }
    if (out->metric_data_format != 0) {
        return ROBO_PARSE_ERROR;
    }
    return ROBO_OK;
}

robo_result_t robo_font_maxp_parse(robo_font_span_t span, robo_font_maxp_t *out)
{
    robo_font_reader_t r;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (span.size < 6u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u32be(&r, &out->version) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->num_glyphs) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (out->version == 0x00005000u) {
        /* CFF-style maxp: 6 bytes only. */
        return ROBO_OK;
    }
    if (out->version != 0x00010000u) {
        return ROBO_NOT_SUPPORTED;
    }
    if (span.size < 32u) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_read_u16be(&r, &out->max_points) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->max_contours) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->max_composite_points) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->max_composite_contours) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->max_zones) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->max_twilight_points) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->max_storage) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->max_function_defs) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->max_instruction_defs) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->max_stack_elements) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->max_size_of_instructions) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->max_component_elements) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->max_component_depth) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    return ROBO_OK;
}

robo_result_t robo_font_post_parse(robo_font_span_t span, robo_font_post_t *out)
{
    robo_font_reader_t r;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (span.size < 32u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u32be(&r, &out->version) != ROBO_OK ||
        robo_font_read_i32be(&r, &out->italic_angle) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->underline_position) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->underline_thickness) != ROBO_OK ||
        robo_font_read_u32be(&r, &out->is_fixed_pitch) != ROBO_OK ||
        robo_font_read_u32be(&r, &out->min_mem_type42) != ROBO_OK ||
        robo_font_read_u32be(&r, &out->max_mem_type42) != ROBO_OK ||
        robo_font_read_u32be(&r, &out->min_mem_type1) != ROBO_OK ||
        robo_font_read_u32be(&r, &out->max_mem_type1) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    /* Supported header versions: 1.0, 2.0, 2.5, 3.0 (extra data ignored). */
    if (out->version != 0x00010000u && out->version != 0x00020000u &&
        out->version != 0x00025000u && out->version != 0x00030000u) {
        return ROBO_NOT_SUPPORTED;
    }
    return ROBO_OK;
}

robo_result_t robo_font_os2_parse(robo_font_span_t span, robo_font_os2_t *out)
{
    robo_font_reader_t r;
    uint32_t i;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    /* Version 0 minimum length is 78 bytes. */
    if (span.size < 78u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u16be(&r, &out->version) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->x_avg_char_width) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->us_weight_class) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->us_width_class) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->fs_type) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->y_subscript_x_size) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->y_subscript_y_size) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->y_subscript_x_offset) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->y_subscript_y_offset) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->y_superscript_x_size) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->y_superscript_y_size) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->y_superscript_x_offset) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->y_superscript_y_offset) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->y_strikeout_size) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->y_strikeout_position) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->s_family_class) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    for (i = 0u; i < 10u; i++) {
        if (robo_font_read_u8(&r, &out->panose[i]) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
    }
    if (robo_font_read_u32be(&r, &out->ul_unicode_range1) != ROBO_OK ||
        robo_font_read_u32be(&r, &out->ul_unicode_range2) != ROBO_OK ||
        robo_font_read_u32be(&r, &out->ul_unicode_range3) != ROBO_OK ||
        robo_font_read_u32be(&r, &out->ul_unicode_range4) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    for (i = 0u; i < 4u; i++) {
        if (robo_font_read_u8(&r, &out->ach_vend_id[i]) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
    }
    if (robo_font_read_u16be(&r, &out->fs_selection) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->us_first_char_index) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->us_last_char_index) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->s_typo_ascender) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->s_typo_descender) != ROBO_OK ||
        robo_font_read_i16be(&r, &out->s_typo_line_gap) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->us_win_ascent) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->us_win_descent) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (out->version >= 1u) {
        if (span.size < 86u) {
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_read_u32be(&r, &out->ul_code_page_range1) != ROBO_OK ||
            robo_font_read_u32be(&r, &out->ul_code_page_range2) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        out->has_code_page_ranges = 1u;
    }
    if (out->version >= 2u) {
        if (span.size < 96u) {
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_read_i16be(&r, &out->sx_height) != ROBO_OK ||
            robo_font_read_i16be(&r, &out->s_cap_height) != ROBO_OK ||
            robo_font_read_u16be(&r, &out->us_default_char) != ROBO_OK ||
            robo_font_read_u16be(&r, &out->us_break_char) != ROBO_OK ||
            robo_font_read_u16be(&r, &out->us_max_context) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        out->has_v2_metrics = 1u;
    }
    if (out->version >= 5u) {
        if (span.size < 100u) {
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_read_u16be(&r, &out->us_lower_optical_point_size) != ROBO_OK ||
            robo_font_read_u16be(&r, &out->us_upper_optical_point_size) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        out->has_v5_optical = 1u;
    }
    if (out->version > 5u) {
        /* Newer versions: accept if minimum fields for v5 present; trailing ignored. */
        if (span.size < 100u) {
            return ROBO_PARSE_ERROR;
        }
    }
    if (out->us_weight_class < 1u || out->us_weight_class > 1000u) {
        return ROBO_PARSE_ERROR;
    }
    if (out->us_width_class < 1u || out->us_width_class > 9u) {
        return ROBO_PARSE_ERROR;
    }
    return ROBO_OK;
}

void robo_font_name_table_clear(robo_font_name_table_t *t)
{
    if (!t) {
        return;
    }
    robo_free(t->records);
    t->records = NULL;
    t->count = 0u;
    t->storage.data = NULL;
    t->storage.size = 0u;
}

robo_result_t robo_font_name_parse(robo_font_span_t span, robo_font_name_table_t *out)
{
    robo_font_reader_t r;
    uint16_t i;
    size_t storage_off;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (span.size < 6u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u16be(&r, &out->format) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->count) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->string_offset) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (out->format > 1u) {
        return ROBO_NOT_SUPPORTED;
    }
    if (out->count > ROBO_FONT_TT_MAX_NAME_RECORDS) {
        return ROBO_PARSE_ERROR;
    }
    if ((size_t)out->count * 12u > SIZE_MAX - 6u) {
        return ROBO_PARSE_ERROR;
    }
    if (span.size < 6u + (size_t)out->count * 12u) {
        return ROBO_PARSE_ERROR;
    }
    if ((size_t)out->string_offset > span.size) {
        return ROBO_PARSE_ERROR;
    }
    storage_off = out->string_offset;
    out->storage.data = span.data + storage_off;
    out->storage.size = span.size - storage_off;

    if (out->count == 0u) {
        return ROBO_OK;
    }
    out->records =
        (robo_font_name_record_t *)robo_calloc(out->count, sizeof(robo_font_name_record_t));
    if (!out->records) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0u; i < out->count; i++) {
        robo_font_name_record_t *rec = &out->records[i];
        if (robo_font_read_u16be(&r, &rec->platform_id) != ROBO_OK ||
            robo_font_read_u16be(&r, &rec->encoding_id) != ROBO_OK ||
            robo_font_read_u16be(&r, &rec->language_id) != ROBO_OK ||
            robo_font_read_u16be(&r, &rec->name_id) != ROBO_OK ||
            robo_font_read_u16be(&r, &rec->length) != ROBO_OK ||
            robo_font_read_u16be(&r, &rec->offset) != ROBO_OK) {
            robo_font_name_table_clear(out);
            return ROBO_PARSE_ERROR;
        }
        if (rec->length > ROBO_FONT_TT_MAX_NAME_STRING) {
            robo_font_name_table_clear(out);
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_checked_range(out->storage.size, rec->offset, rec->length) != ROBO_OK) {
            robo_font_name_table_clear(out);
            return ROBO_PARSE_ERROR;
        }
    }
    return ROBO_OK;
}

static int name_prefer(const robo_font_name_record_t *a, const robo_font_name_record_t *b)
{
    /* Prefer Windows Unicode BMP / full, then Mac Roman. */
    int sa = 0;
    int sb = 0;
    if (a->platform_id == 3u && (a->encoding_id == 1u || a->encoding_id == 10u)) {
        sa = 3;
    } else if (a->platform_id == 1u && a->encoding_id == 0u) {
        sa = 1;
    }
    if (b->platform_id == 3u && (b->encoding_id == 1u || b->encoding_id == 10u)) {
        sb = 3;
    } else if (b->platform_id == 1u && b->encoding_id == 0u) {
        sb = 1;
    }
    if (sa != sb) {
        return sa - sb;
    }
    /* Prefer en-US language 0x0409 on Windows. */
    if (a->platform_id == 3u && a->language_id == 0x0409u) {
        return 1;
    }
    if (b->platform_id == 3u && b->language_id == 0x0409u) {
        return -1;
    }
    return 0;
}

static robo_result_t decode_mac_roman_ascii(const uint8_t *data,
                                            size_t len,
                                            char *buf,
                                            size_t buf_cap,
                                            size_t *out_len)
{
    size_t i;
    if (!buf || buf_cap == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (len + 1u > buf_cap) {
        return ROBO_PARSE_ERROR;
    }
    for (i = 0u; i < len; i++) {
        if (data[i] >= 0x80u) {
            /* Non-ASCII Mac Roman — leave as-is only if printable Latin-1-ish not
             * guaranteed; reject for safety (caller can try Unicode record). */
            return ROBO_NOT_SUPPORTED;
        }
        buf[i] = (char)data[i];
    }
    buf[len] = '\0';
    if (out_len) {
        *out_len = len;
    }
    return ROBO_OK;
}

robo_result_t robo_font_name_get_utf8(const robo_font_name_table_t *t,
                                      uint16_t name_id,
                                      char *buf,
                                      size_t buf_cap,
                                      size_t *out_len)
{
    const robo_font_name_record_t *best = NULL;
    uint16_t i;
    const uint8_t *bytes;
    size_t len;

    if (!t || !buf || buf_cap == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (out_len) {
        *out_len = 0u;
    }
    buf[0] = '\0';
    if (!t->records) {
        return ROBO_NOT_FOUND;
    }
    for (i = 0u; i < t->count; i++) {
        if (t->records[i].name_id != name_id) {
            continue;
        }
        if (!best || name_prefer(&t->records[i], best) > 0) {
            best = &t->records[i];
        }
    }
    if (!best) {
        return ROBO_NOT_FOUND;
    }
    bytes = t->storage.data + best->offset;
    len = best->length;

    if (best->platform_id == 3u &&
        (best->encoding_id == 1u || best->encoding_id == 10u)) {
        robo_uchar16_t *u16;
        size_t nunits;
        size_t ui;
        size_t utf8_len = 0u;
        robo_unicode_status_t st;

        if ((len & 1u) != 0u) {
            return ROBO_PARSE_ERROR;
        }
        nunits = len / 2u;
        u16 = (robo_uchar16_t *)robo_calloc(nunits == 0u ? 1u : nunits, sizeof(robo_uchar16_t));
        if (!u16) {
            return ROBO_OUT_OF_MEMORY;
        }
        for (ui = 0u; ui < nunits; ui++) {
            u16[ui] = (robo_uchar16_t)(((uint16_t)bytes[ui * 2u] << 8) |
                                       (uint16_t)bytes[ui * 2u + 1u]);
        }
        st = robo_unicode_utf16_to_utf8(u16, nunits, NULL, 0u, &utf8_len,
                                        ROBO_UNICODE_STRICT);
        if (st != ROBO_UNICODE_OK && st != ROBO_UNICODE_NO_SPACE) {
            robo_free(u16);
            return ROBO_PARSE_ERROR;
        }
        if (utf8_len + 1u > buf_cap) {
            robo_free(u16);
            return ROBO_PARSE_ERROR;
        }
        st = robo_unicode_utf16_to_utf8(u16, nunits, buf, buf_cap, &utf8_len,
                                        ROBO_UNICODE_STRICT);
        robo_free(u16);
        if (st != ROBO_UNICODE_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (utf8_len < buf_cap) {
            buf[utf8_len] = '\0';
        }
        if (out_len) {
            *out_len = utf8_len;
        }
        return ROBO_OK;
    }
    if (best->platform_id == 1u && best->encoding_id == 0u) {
        return decode_mac_roman_ascii(bytes, len, buf, buf_cap, out_len);
    }
    return ROBO_NOT_SUPPORTED;
}
