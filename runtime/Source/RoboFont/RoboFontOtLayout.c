#include "RoboFont/RoboFontOt.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

void robo_font_gdef_clear(robo_font_gdef_t *g)
{
    if (!g) {
        return;
    }
    robo_font_ot_class_def_clear(&g->glyph_class_def);
    robo_font_ot_class_def_clear(&g->mark_attach_class_def);
    memset(g, 0, sizeof(*g));
}

robo_result_t robo_font_gdef_parse(robo_font_span_t span, robo_font_gdef_t *out)
{
    robo_font_reader_t r;
    uint16_t major;
    uint16_t minor;
    uint16_t glyph_class_off;
    uint16_t attach_off;
    uint16_t lig_off;
    uint16_t mark_attach_off;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_gdef_clear(out);
    if (span.size < 12u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u16be(&r, &major) != ROBO_OK ||
        robo_font_read_u16be(&r, &minor) != ROBO_OK ||
        robo_font_read_u16be(&r, &glyph_class_off) != ROBO_OK ||
        robo_font_read_u16be(&r, &attach_off) != ROBO_OK ||
        robo_font_read_u16be(&r, &lig_off) != ROBO_OK ||
        robo_font_read_u16be(&r, &mark_attach_off) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (major != 1u) {
        return ROBO_NOT_SUPPORTED;
    }
    out->major_version = major;
    out->minor_version = minor;
    out->table = span;

    if (glyph_class_off != 0u) {
        size_t abs;
        if (robo_font_ot_resolve_offset16(span, 0u, glyph_class_off, &abs) != ROBO_OK ||
            robo_font_ot_class_def_parse(span, abs, &out->glyph_class_def) != ROBO_OK) {
            robo_font_gdef_clear(out);
            return ROBO_PARSE_ERROR;
        }
        out->has_glyph_class_def = 1u;
    }
    if (attach_off != 0u) {
        size_t abs;
        if (robo_font_ot_resolve_offset16(span, 0u, attach_off, &abs) != ROBO_OK) {
            robo_font_gdef_clear(out);
            return ROBO_PARSE_ERROR;
        }
        /* AttachList: Coverage offset + count — validate header only. */
        if (abs + 4u > span.size) {
            robo_font_gdef_clear(out);
            return ROBO_PARSE_ERROR;
        }
        out->attach_list_offset = abs;
        out->has_attach_list = 1u;
    }
    if (lig_off != 0u) {
        size_t abs;
        if (robo_font_ot_resolve_offset16(span, 0u, lig_off, &abs) != ROBO_OK ||
            abs + 4u > span.size) {
            robo_font_gdef_clear(out);
            return ROBO_PARSE_ERROR;
        }
        out->lig_caret_list_offset = abs;
        out->has_lig_caret_list = 1u;
    }
    if (mark_attach_off != 0u) {
        size_t abs;
        if (robo_font_ot_resolve_offset16(span, 0u, mark_attach_off, &abs) != ROBO_OK ||
            robo_font_ot_class_def_parse(span, abs, &out->mark_attach_class_def) !=
                ROBO_OK) {
            robo_font_gdef_clear(out);
            return ROBO_PARSE_ERROR;
        }
        out->has_mark_attach_class_def = 1u;
    }
    if (minor >= 2u) {
        uint16_t mark_sets;
        if (span.size < 14u || robo_font_read_u16be(&r, &mark_sets) != ROBO_OK) {
            robo_font_gdef_clear(out);
            return ROBO_PARSE_ERROR;
        }
        if (mark_sets != 0u) {
            size_t abs;
            if (robo_font_ot_resolve_offset16(span, 0u, mark_sets, &abs) != ROBO_OK ||
                abs + 4u > span.size) {
                robo_font_gdef_clear(out);
                return ROBO_PARSE_ERROR;
            }
            out->mark_glyph_sets_offset = abs;
            out->has_mark_glyph_sets = 1u;
        }
    }
    if (minor >= 3u) {
        uint32_t item_var;
        if (span.size < 18u || robo_font_read_u32be(&r, &item_var) != ROBO_OK) {
            robo_font_gdef_clear(out);
            return ROBO_PARSE_ERROR;
        }
        if (item_var != 0u) {
            size_t abs;
            if (robo_font_ot_resolve_offset32(span, 0u, item_var, &abs) != ROBO_OK) {
                robo_font_gdef_clear(out);
                return ROBO_PARSE_ERROR;
            }
            out->item_var_store_offset = abs;
            out->has_item_var_store = 1u;
        }
    }
    return ROBO_OK;
}

void robo_font_ot_layout_clear(robo_font_ot_layout_t *L)
{
    if (!L) {
        return;
    }
    robo_font_ot_script_list_clear(&L->scripts);
    robo_font_ot_feature_list_clear(&L->features);
    robo_font_ot_lookup_list_clear(&L->lookups);
    memset(L, 0, sizeof(*L));
}

static robo_result_t layout_parse(robo_font_span_t span,
                                  robo_font_ot_layout_kind_t kind,
                                  robo_font_ot_layout_t *out)
{
    robo_font_reader_t r;
    uint16_t major;
    uint16_t minor;
    uint16_t script_off;
    uint16_t feature_off;
    uint16_t lookup_off;
    size_t sabs;
    size_t fabs;
    size_t labs;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_ot_layout_clear(out);
    if (span.size < 10u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u16be(&r, &major) != ROBO_OK ||
        robo_font_read_u16be(&r, &minor) != ROBO_OK ||
        robo_font_read_u16be(&r, &script_off) != ROBO_OK ||
        robo_font_read_u16be(&r, &feature_off) != ROBO_OK ||
        robo_font_read_u16be(&r, &lookup_off) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (major != 1u || minor > 1u) {
        return ROBO_NOT_SUPPORTED;
    }
    out->kind = kind;
    out->major_version = major;
    out->minor_version = minor;
    out->table = span;

    if (robo_font_ot_resolve_offset16(span, 0u, script_off, &sabs) != ROBO_OK ||
        robo_font_ot_script_list_parse(span, sabs, &out->scripts) != ROBO_OK) {
        robo_font_ot_layout_clear(out);
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_ot_resolve_offset16(span, 0u, feature_off, &fabs) != ROBO_OK ||
        robo_font_ot_feature_list_parse(span, fabs, &out->features) != ROBO_OK) {
        robo_font_ot_layout_clear(out);
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_ot_resolve_offset16(span, 0u, lookup_off, &labs) != ROBO_OK ||
        robo_font_ot_lookup_list_parse(span, labs, &out->lookups) != ROBO_OK) {
        robo_font_ot_layout_clear(out);
        return ROBO_PARSE_ERROR;
    }

    /* Validate lookup types are known for this table kind. */
    {
        uint16_t i;
        for (i = 0u; i < out->lookups.lookup_count; i++) {
            uint16_t t = out->lookups.lookups[i].lookup_type;
            if (kind == ROBO_FONT_OT_LAYOUT_GSUB) {
                if (!robo_font_gsub_lookup_type_is_known(t)) {
                    robo_font_ot_layout_clear(out);
                    return ROBO_PARSE_ERROR;
                }
            } else if (!robo_font_gpos_lookup_type_is_known(t)) {
                robo_font_ot_layout_clear(out);
                return ROBO_PARSE_ERROR;
            }
        }
        /* Feature lookup indices must be in range. */
        for (i = 0u; i < out->features.feature_count; i++) {
            uint16_t j;
            for (j = 0u; j < out->features.features[i].lookup_index_count; j++) {
                if (out->features.features[i].lookup_indices[j] >=
                    out->lookups.lookup_count) {
                    robo_font_ot_layout_clear(out);
                    return ROBO_PARSE_ERROR;
                }
            }
        }
    }

    if (minor >= 1u) {
        uint32_t feat_var;
        if (span.size < 14u || robo_font_read_u32be(&r, &feat_var) != ROBO_OK) {
            robo_font_ot_layout_clear(out);
            return ROBO_PARSE_ERROR;
        }
        if (feat_var != 0u) {
            size_t abs;
            if (robo_font_ot_resolve_offset32(span, 0u, feat_var, &abs) != ROBO_OK) {
                robo_font_ot_layout_clear(out);
                return ROBO_PARSE_ERROR;
            }
            out->feature_variations_offset = abs;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_font_gsub_parse(robo_font_span_t span, robo_font_ot_layout_t *out)
{
    return layout_parse(span, ROBO_FONT_OT_LAYOUT_GSUB, out);
}

robo_result_t robo_font_gpos_parse(robo_font_span_t span, robo_font_ot_layout_t *out)
{
    return layout_parse(span, ROBO_FONT_OT_LAYOUT_GPOS, out);
}

int robo_font_gsub_lookup_type_is_known(uint16_t type)
{
    return type >= 1u && type <= 8u;
}

int robo_font_gpos_lookup_type_is_known(uint16_t type)
{
    return type >= 1u && type <= 9u;
}
