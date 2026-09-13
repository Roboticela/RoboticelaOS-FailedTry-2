#include "RoboFont/RoboFontOt.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

robo_result_t robo_font_base_parse(robo_font_span_t span, robo_font_base_t *out)
{
    robo_font_reader_t r;
    uint16_t major;
    uint16_t minor;
    uint16_t horiz;
    uint16_t vert;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (span.size < 8u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u16be(&r, &major) != ROBO_OK ||
        robo_font_read_u16be(&r, &minor) != ROBO_OK ||
        robo_font_read_u16be(&r, &horiz) != ROBO_OK ||
        robo_font_read_u16be(&r, &vert) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (major != 1u || minor > 1u) {
        return ROBO_NOT_SUPPORTED;
    }
    out->major_version = major;
    out->minor_version = minor;
    out->table = span;
    if (horiz != 0u) {
        size_t abs;
        if (robo_font_ot_resolve_offset16(span, 0u, horiz, &abs) != ROBO_OK ||
            abs + 4u > span.size) {
            return ROBO_PARSE_ERROR;
        }
        out->horiz_axis_offset = abs;
        out->has_horiz = 1u;
    }
    if (vert != 0u) {
        size_t abs;
        if (robo_font_ot_resolve_offset16(span, 0u, vert, &abs) != ROBO_OK ||
            abs + 4u > span.size) {
            return ROBO_PARSE_ERROR;
        }
        out->vert_axis_offset = abs;
        out->has_vert = 1u;
    }
    if (minor >= 1u) {
        uint32_t item_var;
        if (span.size < 12u || robo_font_read_u32be(&r, &item_var) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (item_var != 0u) {
            size_t abs;
            if (robo_font_ot_resolve_offset32(span, 0u, item_var, &abs) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            out->item_var_store_offset = abs;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_font_jstf_parse(robo_font_span_t span, robo_font_jstf_t *out)
{
    robo_font_reader_t r;
    uint16_t major;
    uint16_t minor;
    uint16_t count;
    uint16_t i;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (span.size < 6u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u16be(&r, &major) != ROBO_OK ||
        robo_font_read_u16be(&r, &minor) != ROBO_OK ||
        robo_font_read_u16be(&r, &count) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (major != 1u || minor != 0u) {
        return ROBO_NOT_SUPPORTED;
    }
    if (count > ROBO_FONT_OT_MAX_SCRIPTS) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_checked_range(span.size, 6u, (size_t)count * 6u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    out->major_version = major;
    out->minor_version = minor;
    out->script_count = count;
    out->table = span;
    for (i = 0u; i < count; i++) {
        uint32_t tag;
        uint16_t soff;
        size_t abs;
        (void)tag;
        if (robo_font_read_u32be(&r, &tag) != ROBO_OK ||
            robo_font_read_u16be(&r, &soff) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_ot_resolve_offset16(span, 0u, soff, &abs) != ROBO_OK ||
            abs + 2u > span.size) {
            return ROBO_PARSE_ERROR;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_font_math_parse(robo_font_span_t span, robo_font_math_t *out)
{
    robo_font_reader_t r;
    uint32_t version;
    uint16_t c_off;
    uint16_t g_off;
    uint16_t v_off;
    size_t abs;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (span.size < 10u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u32be(&r, &version) != ROBO_OK ||
        robo_font_read_u16be(&r, &c_off) != ROBO_OK ||
        robo_font_read_u16be(&r, &g_off) != ROBO_OK ||
        robo_font_read_u16be(&r, &v_off) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (version != 0x00010000u) {
        return ROBO_NOT_SUPPORTED;
    }
    out->version = version;
    out->table = span;
    if (robo_font_ot_resolve_offset16(span, 0u, c_off, &abs) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    out->math_constants_offset = abs;
    if (robo_font_ot_resolve_offset16(span, 0u, g_off, &abs) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    out->math_glyph_info_offset = abs;
    if (robo_font_ot_resolve_offset16(span, 0u, v_off, &abs) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    out->math_variants_offset = abs;
    return ROBO_OK;
}

void robo_font_kern_clear(robo_font_kern_t *k)
{
    if (!k) {
        return;
    }
    robo_free(k->subtables);
    memset(k, 0, sizeof(*k));
}

robo_result_t robo_font_kern_parse(robo_font_span_t span, robo_font_kern_t *out)
{
    robo_font_reader_t r;
    uint16_t version;
    uint16_t n_tables;
    uint16_t i;
    size_t cursor;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_kern_clear(out);
    if (span.size < 4u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u16be(&r, &version) != ROBO_OK ||
        robo_font_read_u16be(&r, &n_tables) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    /* Apple format uses uint32 version; Windows uses uint16. Support Windows v0. */
    if (version != 0u) {
        return ROBO_NOT_SUPPORTED;
    }
    if (n_tables > 256u) {
        return ROBO_PARSE_ERROR;
    }
    out->version = version;
    out->n_tables = n_tables;
    out->table = span;
    if (n_tables == 0u) {
        return ROBO_OK;
    }
    out->subtables =
        (robo_font_kern_subtable_t *)robo_calloc(n_tables, sizeof(robo_font_kern_subtable_t));
    if (!out->subtables) {
        return ROBO_OUT_OF_MEMORY;
    }
    cursor = 4u;
    for (i = 0u; i < n_tables; i++) {
        uint16_t sv;
        uint16_t len;
        uint16_t cov;
        if (cursor + 6u > span.size) {
            robo_font_kern_clear(out);
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_reader_seek(&r, cursor) != ROBO_OK ||
            robo_font_read_u16be(&r, &sv) != ROBO_OK ||
            robo_font_read_u16be(&r, &len) != ROBO_OK ||
            robo_font_read_u16be(&r, &cov) != ROBO_OK) {
            robo_font_kern_clear(out);
            return ROBO_PARSE_ERROR;
        }
        if (len < 6u || cursor + len > span.size) {
            robo_font_kern_clear(out);
            return ROBO_PARSE_ERROR;
        }
        out->subtables[i].version = sv;
        out->subtables[i].length = len;
        out->subtables[i].coverage = cov;
        out->subtables[i].format = (uint16_t)(cov >> 8);
        out->subtables[i].offset = cursor;
        cursor += len;
    }
    return ROBO_OK;
}

robo_result_t robo_font_cpal_parse(robo_font_span_t span, robo_font_cpal_t *out)
{
    robo_font_reader_t r;
    uint16_t version;
    uint16_t num_entries;
    uint16_t num_palettes;
    uint16_t num_colors;
    uint32_t color_off;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (span.size < 10u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u16be(&r, &version) != ROBO_OK ||
        robo_font_read_u16be(&r, &num_entries) != ROBO_OK ||
        robo_font_read_u16be(&r, &num_palettes) != ROBO_OK ||
        robo_font_read_u16be(&r, &num_colors) != ROBO_OK ||
        robo_font_read_u32be(&r, &color_off) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (version > 1u) {
        return ROBO_NOT_SUPPORTED;
    }
    if (num_palettes > 1024u || num_colors > 65535u || num_entries > 65535u) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_checked_range(span.size, 12u, (size_t)num_palettes * 2u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    {
        size_t abs;
        if (robo_font_ot_resolve_offset32(span, 0u, color_off, &abs) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_checked_range(span.size, abs, (size_t)num_colors * 4u) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        out->color_records_array_offset = abs;
    }
    out->version = version;
    out->num_palettes_entries = num_entries;
    out->num_palette = num_palettes;
    out->num_color_records = num_colors;
    out->table = span;
    return ROBO_OK;
}

robo_result_t robo_font_colr_parse(robo_font_span_t span, robo_font_colr_t *out)
{
    robo_font_reader_t r;
    uint16_t version;
    uint16_t num_base;
    uint32_t base_off;
    uint32_t layer_off;
    uint16_t num_layer;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (span.size < 14u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u16be(&r, &version) != ROBO_OK ||
        robo_font_read_u16be(&r, &num_base) != ROBO_OK ||
        robo_font_read_u32be(&r, &base_off) != ROBO_OK ||
        robo_font_read_u32be(&r, &layer_off) != ROBO_OK ||
        robo_font_read_u16be(&r, &num_layer) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (version > 1u) {
        return ROBO_NOT_SUPPORTED;
    }
    if (num_base > 65535u || num_layer > 65535u) {
        return ROBO_PARSE_ERROR;
    }
    {
        size_t abs;
        if (robo_font_ot_resolve_offset32(span, 0u, base_off, &abs) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        /* BaseGlyphRecord: glyphID + firstLayerIndex + numLayers = 6 bytes */
        if (robo_font_checked_range(span.size, abs, (size_t)num_base * 6u) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        out->base_glyph_records_offset = abs;
        if (robo_font_ot_resolve_offset32(span, 0u, layer_off, &abs) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        /* LayerRecord: glyphID + paletteIndex = 4 bytes */
        if (robo_font_checked_range(span.size, abs, (size_t)num_layer * 4u) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        out->layer_records_offset = abs;
    }
    out->version = version;
    out->num_base_glyph_records = num_base;
    out->num_layer_records = num_layer;
    out->table = span;
    return ROBO_OK;
}

void robo_font_stat_clear(robo_font_stat_t *s)
{
    if (!s) {
        return;
    }
    robo_free(s->axes);
    memset(s, 0, sizeof(*s));
}

robo_result_t robo_font_stat_parse(robo_font_span_t span, robo_font_stat_t *out)
{
    robo_font_reader_t r;
    uint16_t major;
    uint16_t minor;
    uint16_t axis_size;
    uint16_t axis_count;
    uint32_t axes_off;
    uint16_t value_count;
    uint32_t values_off;
    uint16_t i;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_stat_clear(out);
    if (span.size < 20u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u16be(&r, &major) != ROBO_OK ||
        robo_font_read_u16be(&r, &minor) != ROBO_OK ||
        robo_font_read_u16be(&r, &axis_size) != ROBO_OK ||
        robo_font_read_u16be(&r, &axis_count) != ROBO_OK ||
        robo_font_read_u32be(&r, &axes_off) != ROBO_OK ||
        robo_font_read_u16be(&r, &value_count) != ROBO_OK ||
        robo_font_read_u32be(&r, &values_off) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->elided_fallback_name_id) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (major != 1u || minor > 2u) {
        return ROBO_NOT_SUPPORTED;
    }
    if (axis_size < 8u || axis_count > 64u || value_count > 4096u) {
        return ROBO_PARSE_ERROR;
    }
    {
        size_t abs;
        if (robo_font_ot_resolve_offset32(span, 0u, axes_off, &abs) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_checked_range(span.size, abs, (size_t)axis_count * axis_size) !=
            ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        out->design_axes_offset = abs;
        if (values_off != 0u) {
            if (robo_font_ot_resolve_offset32(span, 0u, values_off, &abs) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
            out->offset_to_axis_values = abs;
        }
    }
    out->major_version = major;
    out->minor_version = minor;
    out->design_axis_size = axis_size;
    out->design_axis_count = axis_count;
    out->axis_value_count = value_count;
    out->table = span;

    if (axis_count == 0u) {
        return ROBO_OK;
    }
    out->axes =
        (robo_font_stat_axis_t *)robo_calloc(axis_count, sizeof(robo_font_stat_axis_t));
    if (!out->axes) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0u; i < axis_count; i++) {
        size_t aoff = out->design_axes_offset + (size_t)i * axis_size;
        robo_font_reader_t ar;
        uint32_t tag;
        uint16_t name_id;
        uint16_t ordering;
        robo_font_reader_init_span(&ar, span);
        if (robo_font_reader_seek(&ar, aoff) != ROBO_OK ||
            robo_font_read_u32be(&ar, &tag) != ROBO_OK ||
            robo_font_read_u16be(&ar, &name_id) != ROBO_OK ||
            robo_font_read_u16be(&ar, &ordering) != ROBO_OK) {
            robo_font_stat_clear(out);
            return ROBO_PARSE_ERROR;
        }
        out->axes[i].tag = tag;
        out->axes[i].name_id = name_id;
        out->axes[i].ordering = ordering;
    }
    return ROBO_OK;
}
