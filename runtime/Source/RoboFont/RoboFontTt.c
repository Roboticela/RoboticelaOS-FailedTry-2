#include "RoboFont/RoboFontTt.h"

#include "RoboFont/RoboFontClassification.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

struct robo_font_tt {
    uint32_t refs;
    robo_font_sfnt_t *sfnt;
    robo_font_head_t head;
    robo_font_hhea_t hhea;
    robo_font_maxp_t maxp;
    robo_font_post_t post;
    robo_font_os2_t os2;
    uint8_t has_os2;
    uint8_t has_post;
    robo_font_name_table_t name;
    uint8_t has_name;
    robo_font_span_t hmtx;
    robo_font_span_t glyf;
    uint32_t *loca; /* num_glyphs + 1 byte offsets */
    uint16_t num_glyphs;
    uint16_t number_of_h_metrics;
};

static robo_result_t table_span(const robo_font_sfnt_t *sfnt,
                                robo_font_tag_t tag,
                                robo_font_span_t *out)
{
    return robo_font_sfnt_table_bytes(sfnt, tag, out);
}

static robo_result_t parse_loca(robo_font_span_t span,
                                uint16_t num_glyphs,
                                int16_t index_format,
                                size_t glyf_size,
                                uint32_t **out_offs)
{
    uint32_t need = (uint32_t)num_glyphs + 1u;
    uint32_t *offs;
    uint32_t i;
    robo_font_reader_t r;

    if (!out_offs) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_offs = NULL;
    if (num_glyphs > ROBO_FONT_TT_MAX_GLYPHS) {
        return ROBO_PARSE_ERROR;
    }

    offs = (uint32_t *)robo_calloc(need, sizeof(uint32_t));
    if (!offs) {
        return ROBO_OUT_OF_MEMORY;
    }
    robo_font_reader_init_span(&r, span);

    if (index_format == 0) {
        if (span.size < (size_t)need * 2u) {
            robo_free(offs);
            return ROBO_PARSE_ERROR;
        }
        for (i = 0u; i < need; i++) {
            uint16_t v;
            if (robo_font_read_u16be(&r, &v) != ROBO_OK) {
                robo_free(offs);
                return ROBO_PARSE_ERROR;
            }
            offs[i] = (uint32_t)v * 2u;
        }
    } else if (index_format == 1) {
        if (span.size < (size_t)need * 4u) {
            robo_free(offs);
            return ROBO_PARSE_ERROR;
        }
        for (i = 0u; i < need; i++) {
            if (robo_font_read_u32be(&r, &offs[i]) != ROBO_OK) {
                robo_free(offs);
                return ROBO_PARSE_ERROR;
            }
        }
    } else {
        robo_free(offs);
        return ROBO_PARSE_ERROR;
    }

    for (i = 1u; i < need; i++) {
        if (offs[i] < offs[i - 1u]) {
            robo_free(offs);
            return ROBO_PARSE_ERROR;
        }
    }
    if (offs[num_glyphs] > glyf_size) {
        robo_free(offs);
        return ROBO_PARSE_ERROR;
    }
    *out_offs = offs;
    return ROBO_OK;
}

static size_t expected_hmtx_size(uint16_t num_glyphs, uint16_t n_h_metrics)
{
    if (n_h_metrics > num_glyphs) {
        return 0u;
    }
    return (size_t)n_h_metrics * 4u + (size_t)(num_glyphs - n_h_metrics) * 2u;
}

static robo_result_t verify_head_checksum(const robo_font_sfnt_t *sfnt,
                                          robo_font_span_t head_span)
{
    robo_font_sfnt_table_record_t rec;
    uint8_t *tmp;
    uint32_t calc;
    robo_result_t res;

    res = robo_font_sfnt_find_table(sfnt, ROBO_FONT_TAG_HEAD, &rec);
    if (res != ROBO_OK) {
        return res;
    }
    if (head_span.size < 12u) {
        return ROBO_PARSE_ERROR;
    }
    tmp = (uint8_t *)robo_malloc(head_span.size);
    if (!tmp) {
        return ROBO_OUT_OF_MEMORY;
    }
    memcpy(tmp, head_span.data, head_span.size);
    /* Zero checkSumAdjustment at bytes 8..11 for verification. */
    tmp[8] = tmp[9] = tmp[10] = tmp[11] = 0u;
    calc = robo_font_sfnt_checksum(tmp, head_span.size);
    robo_free(tmp);
    if (calc != rec.checksum) {
        return ROBO_PARSE_ERROR;
    }
    return ROBO_OK;
}

robo_result_t robo_font_tt_parse(robo_font_sfnt_t *sfnt,
                                 uint32_t flags,
                                 robo_font_tt_t **out)
{
    robo_font_tt_t *tt;
    robo_font_span_t head_s;
    robo_font_span_t hhea_s;
    robo_font_span_t maxp_s;
    robo_font_span_t hmtx_s;
    robo_font_span_t loca_s;
    robo_font_span_t glyf_s;
    robo_font_span_t post_s;
    robo_font_span_t os2_s;
    robo_font_span_t name_s;
    robo_result_t res;
    size_t hmtx_need;
    int has_glyf;

    if (!out || !sfnt) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;

    has_glyf = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_GLYF);
    if ((flags & ROBO_FONT_TT_FLAGS_REQUIRE_GLYF) != 0u && !has_glyf) {
        return ROBO_NOT_SUPPORTED;
    }

    if (table_span(sfnt, ROBO_FONT_TAG_HEAD, &head_s) != ROBO_OK ||
        table_span(sfnt, ROBO_FONT_TAG_HHEA, &hhea_s) != ROBO_OK ||
        table_span(sfnt, ROBO_FONT_TAG_MAXP, &maxp_s) != ROBO_OK ||
        table_span(sfnt, ROBO_FONT_TAG_HMTX, &hmtx_s) != ROBO_OK) {
        return ROBO_NOT_FOUND;
    }

    tt = (robo_font_tt_t *)robo_calloc(1, sizeof(*tt));
    if (!tt) {
        return ROBO_OUT_OF_MEMORY;
    }

    res = robo_font_head_parse(head_s, &tt->head);
    if (res != ROBO_OK) {
        goto fail;
    }
    if ((flags & ROBO_FONT_TT_FLAGS_VERIFY_HEAD_CHECKSUM) != 0u) {
        res = verify_head_checksum(sfnt, head_s);
        if (res != ROBO_OK) {
            goto fail;
        }
    }
    res = robo_font_hhea_parse(hhea_s, &tt->hhea);
    if (res != ROBO_OK) {
        goto fail;
    }
    res = robo_font_maxp_parse(maxp_s, &tt->maxp);
    if (res != ROBO_OK) {
        goto fail;
    }

    tt->num_glyphs = tt->maxp.num_glyphs;
    tt->number_of_h_metrics = tt->hhea.number_of_h_metrics;

    if (tt->num_glyphs > ROBO_FONT_TT_MAX_GLYPHS) {
        res = ROBO_PARSE_ERROR;
        goto fail;
    }
    if (tt->num_glyphs == 0u) {
        if (tt->number_of_h_metrics != 0u) {
            res = ROBO_PARSE_ERROR;
            goto fail;
        }
    } else if (tt->number_of_h_metrics == 0u ||
               tt->number_of_h_metrics > tt->num_glyphs) {
        res = ROBO_PARSE_ERROR;
        goto fail;
    }

    hmtx_need = expected_hmtx_size(tt->num_glyphs, tt->number_of_h_metrics);
    if (hmtx_s.size < hmtx_need) {
        res = ROBO_PARSE_ERROR;
        goto fail;
    }
    tt->hmtx = hmtx_s;

    if (has_glyf) {
        if (table_span(sfnt, ROBO_FONT_TAG_LOCA, &loca_s) != ROBO_OK ||
            table_span(sfnt, ROBO_FONT_TAG_GLYF, &glyf_s) != ROBO_OK) {
            res = ROBO_NOT_FOUND;
            goto fail;
        }
        /* TrueType outlines require maxp v1.0 */
        if (tt->maxp.version != 0x00010000u) {
            res = ROBO_PARSE_ERROR;
            goto fail;
        }
        res = parse_loca(loca_s, tt->num_glyphs, tt->head.index_to_loc_format,
                         glyf_s.size, &tt->loca);
        if (res != ROBO_OK) {
            goto fail;
        }
        tt->glyf = glyf_s;
    } else if ((flags & ROBO_FONT_TT_FLAGS_REQUIRE_GLYF) != 0u) {
        res = ROBO_NOT_SUPPORTED;
        goto fail;
    }

    if (table_span(sfnt, ROBO_FONT_TAG_POST, &post_s) == ROBO_OK) {
        res = robo_font_post_parse(post_s, &tt->post);
        if (res != ROBO_OK) {
            goto fail;
        }
        tt->has_post = 1u;
    }
    if (table_span(sfnt, ROBO_FONT_TAG_OS2, &os2_s) == ROBO_OK) {
        res = robo_font_os2_parse(os2_s, &tt->os2);
        if (res != ROBO_OK) {
            goto fail;
        }
        tt->has_os2 = 1u;
    }
    if (table_span(sfnt, ROBO_FONT_TAG_NAME, &name_s) == ROBO_OK) {
        res = robo_font_name_parse(name_s, &tt->name);
        if (res != ROBO_OK) {
            goto fail;
        }
        tt->has_name = 1u;
    }

    tt->refs = 1u;
    robo_font_sfnt_retain(sfnt);
    tt->sfnt = sfnt;
    *out = tt;
    return ROBO_OK;

fail:
    if (tt) {
        robo_free(tt->loca);
        robo_font_name_table_clear(&tt->name);
        robo_free(tt);
    }
    return res;
}

void robo_font_tt_retain(robo_font_tt_t *tt)
{
    if (!tt) {
        return;
    }
    tt->refs++;
}

void robo_font_tt_release(robo_font_tt_t *tt)
{
    if (!tt) {
        return;
    }
    if (tt->refs == 0u) {
        return;
    }
    tt->refs--;
    if (tt->refs != 0u) {
        return;
    }
    robo_font_sfnt_release(tt->sfnt);
    tt->sfnt = NULL;
    robo_free(tt->loca);
    tt->loca = NULL;
    robo_font_name_table_clear(&tt->name);
    robo_free(tt);
}

const robo_font_sfnt_t *robo_font_tt_sfnt(const robo_font_tt_t *tt)
{
    return tt ? tt->sfnt : NULL;
}

const robo_font_head_t *robo_font_tt_head(const robo_font_tt_t *tt)
{
    return tt ? &tt->head : NULL;
}

const robo_font_hhea_t *robo_font_tt_hhea(const robo_font_tt_t *tt)
{
    return tt ? &tt->hhea : NULL;
}

const robo_font_maxp_t *robo_font_tt_maxp(const robo_font_tt_t *tt)
{
    return tt ? &tt->maxp : NULL;
}

const robo_font_post_t *robo_font_tt_post(const robo_font_tt_t *tt)
{
    return (tt && tt->has_post) ? &tt->post : NULL;
}

const robo_font_os2_t *robo_font_tt_os2(const robo_font_tt_t *tt)
{
    return (tt && tt->has_os2) ? &tt->os2 : NULL;
}

const robo_font_name_table_t *robo_font_tt_name(const robo_font_tt_t *tt)
{
    return (tt && tt->has_name) ? &tt->name : NULL;
}

uint16_t robo_font_tt_num_glyphs(const robo_font_tt_t *tt)
{
    return tt ? tt->num_glyphs : 0u;
}

static robo_result_t check_gid(const robo_font_tt_t *tt, robo_glyph_id_t gid)
{
    if (!tt) {
        return ROBO_INVALID_ARGUMENT;
    }
    /* Glyph index 0 (.notdef) is valid; ROBO_GLYPH_ID_NONE coincides with 0 for
     * "unmapped" at the cmap layer — TT APIs treat 0 as a real glyph index. */
    if (gid >= tt->num_glyphs) {
        return ROBO_NOT_FOUND;
    }
    return ROBO_OK;
}

robo_result_t robo_font_tt_advance_width(const robo_font_tt_t *tt,
                                         robo_glyph_id_t glyph_id,
                                         uint16_t *out_aw)
{
    robo_font_reader_t r;
    uint16_t idx;
    uint16_t aw;
    int16_t lsb;
    robo_result_t res = check_gid(tt, glyph_id);
    if (res != ROBO_OK) {
        return res;
    }
    if (!out_aw) {
        return ROBO_INVALID_ARGUMENT;
    }
    idx = (glyph_id < tt->number_of_h_metrics) ? (uint16_t)glyph_id
                                               : (uint16_t)(tt->number_of_h_metrics - 1u);
    robo_font_reader_init_span(&r, tt->hmtx);
    if (robo_font_reader_seek(&r, (size_t)idx * 4u) != ROBO_OK ||
        robo_font_read_u16be(&r, &aw) != ROBO_OK ||
        robo_font_read_i16be(&r, &lsb) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    (void)lsb;
    *out_aw = aw;
    return ROBO_OK;
}

robo_result_t robo_font_tt_left_side_bearing(const robo_font_tt_t *tt,
                                             robo_glyph_id_t glyph_id,
                                             int16_t *out_lsb)
{
    robo_font_reader_t r;
    int16_t lsb;
    robo_result_t res = check_gid(tt, glyph_id);
    if (res != ROBO_OK) {
        return res;
    }
    if (!out_lsb) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_reader_init_span(&r, tt->hmtx);
    if (glyph_id < tt->number_of_h_metrics) {
        if (robo_font_reader_seek(&r, (size_t)glyph_id * 4u + 2u) != ROBO_OK ||
            robo_font_read_i16be(&r, &lsb) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
    } else {
        size_t off =
            (size_t)tt->number_of_h_metrics * 4u +
            (size_t)(glyph_id - tt->number_of_h_metrics) * 2u;
        if (robo_font_reader_seek(&r, off) != ROBO_OK ||
            robo_font_read_i16be(&r, &lsb) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
    }
    *out_lsb = lsb;
    return ROBO_OK;
}

robo_result_t robo_font_tt_glyph_offset(const robo_font_tt_t *tt,
                                        robo_glyph_id_t glyph_id,
                                        uint32_t *out_offset)
{
    robo_result_t res = check_gid(tt, glyph_id);
    if (res != ROBO_OK) {
        return res;
    }
    if (!out_offset || !tt->loca) {
        return tt->loca ? ROBO_INVALID_ARGUMENT : ROBO_NOT_SUPPORTED;
    }
    *out_offset = tt->loca[glyph_id];
    return ROBO_OK;
}

robo_result_t robo_font_tt_glyph_length(const robo_font_tt_t *tt,
                                        robo_glyph_id_t glyph_id,
                                        uint32_t *out_length)
{
    robo_result_t res = check_gid(tt, glyph_id);
    if (res != ROBO_OK) {
        return res;
    }
    if (!out_length || !tt->loca) {
        return tt->loca ? ROBO_INVALID_ARGUMENT : ROBO_NOT_SUPPORTED;
    }
    *out_length = tt->loca[glyph_id + 1u] - tt->loca[glyph_id];
    return ROBO_OK;
}

robo_result_t robo_font_tt_parse_glyph(const robo_font_tt_t *tt,
                                       robo_glyph_id_t glyph_id,
                                       robo_font_glyf_glyph_t *out)
{
    uint32_t off;
    uint32_t len;
    robo_result_t res;

    if (!tt || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!tt->loca) {
        return ROBO_NOT_SUPPORTED;
    }
    res = robo_font_tt_glyph_offset(tt, glyph_id, &off);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_font_tt_glyph_length(tt, glyph_id, &len);
    if (res != ROBO_OK) {
        return res;
    }
    return robo_font_tt_parse_glyph_at(tt->glyf, off, len, tt->num_glyphs, out);
}

static robo_result_t validate_composite_rec(const robo_font_tt_t *tt,
                                            robo_glyph_id_t gid,
                                            uint32_t depth,
                                            uint8_t *visiting)
{
    robo_font_glyf_glyph_t g;
    robo_result_t res;
    uint16_t i;

    if (depth > ROBO_FONT_TT_MAX_COMPOSITE_DEPTH) {
        return ROBO_PARSE_ERROR;
    }
    if (gid >= tt->num_glyphs) {
        return ROBO_PARSE_ERROR;
    }
    if (visiting[gid]) {
        return ROBO_PARSE_ERROR; /* cycle */
    }

    memset(&g, 0, sizeof(g));
    res = robo_font_tt_parse_glyph(tt, gid, &g);
    if (res != ROBO_OK) {
        return res;
    }
    if (g.kind != ROBO_FONT_GLYF_COMPOSITE) {
        robo_font_glyf_glyph_clear(&g);
        return ROBO_OK;
    }

    visiting[gid] = 1u;
    for (i = 0u; i < g.component_count; i++) {
        res = validate_composite_rec(tt, g.components[i].glyph_index, depth + 1u, visiting);
        if (res != ROBO_OK) {
            visiting[gid] = 0u;
            robo_font_glyf_glyph_clear(&g);
            return res;
        }
    }
    visiting[gid] = 0u;
    robo_font_glyf_glyph_clear(&g);
    return ROBO_OK;
}

robo_result_t robo_font_tt_validate_composite_closure(const robo_font_tt_t *tt,
                                                      robo_glyph_id_t glyph_id)
{
    uint8_t *visiting;
    robo_result_t res;
    if (!tt || !tt->loca) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (check_gid(tt, glyph_id) != ROBO_OK) {
        return ROBO_NOT_FOUND;
    }
    visiting = (uint8_t *)robo_calloc(tt->num_glyphs, 1u);
    if (!visiting) {
        return ROBO_OUT_OF_MEMORY;
    }
    res = validate_composite_rec(tt, glyph_id, 0u, visiting);
    robo_free(visiting);
    return res;
}

robo_result_t robo_font_tt_metrics(const robo_font_tt_t *tt, robo_font_metrics_t *out)
{
    robo_font_bbox_t bb;
    robo_font_unit_t ascent;
    robo_font_unit_t descent;
    robo_font_unit_t line_gap;
    if (!tt || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    ascent = tt->hhea.ascender;
    /* hhea descender is typically negative; store magnitude. */
    descent = tt->hhea.descender < 0 ? (robo_font_unit_t)(-tt->hhea.descender)
                                     : (robo_font_unit_t)tt->hhea.descender;
    line_gap = tt->hhea.line_gap < 0 ? 0 : (robo_font_unit_t)tt->hhea.line_gap;
    if (tt->has_os2 && (tt->os2.fs_selection & 0x80u) != 0u) {
        /* USE_TYPO_METRICS */
        ascent = tt->os2.s_typo_ascender;
        descent = tt->os2.s_typo_descender < 0
                      ? (robo_font_unit_t)(-tt->os2.s_typo_descender)
                      : (robo_font_unit_t)tt->os2.s_typo_descender;
        line_gap = tt->os2.s_typo_line_gap < 0
                       ? 0
                       : (robo_font_unit_t)tt->os2.s_typo_line_gap;
    }
    bb.x_min = tt->head.x_min;
    bb.y_min = tt->head.y_min;
    bb.x_max = tt->head.x_max;
    bb.y_max = tt->head.y_max;
    return robo_font_metrics_init(out, tt->head.units_per_em, ascent, descent, line_gap,
                                  &bb);
}

robo_result_t robo_font_tt_style_desc(const robo_font_tt_t *tt,
                                      robo_font_style_desc_t *out)
{
    robo_font_classification_info_t *info = NULL;
    const robo_font_classification_t *cls;
    robo_result_t res;

    if (!tt || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_font_classification_info_create(tt, NULL, NULL, &info);
    if (res != ROBO_OK) {
        return res;
    }
    cls = robo_font_classification_info_get(info);
    robo_font_classification_to_style_desc(cls, out);
    robo_font_classification_info_release(info);
    return ROBO_OK;
}

robo_result_t robo_font_tt_fuzz_parse(const uint8_t *data, size_t size)
{
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_tt_t *tt = NULL;
    robo_result_t res;

    if (!data || size == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_font_source_create_memory_borrow(data, size, &src);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_font_sfnt_parse(src, 0u, ROBO_FONT_SFNT_FLAGS_NONE, &sfnt);
    if (res == ROBO_OK) {
        res = robo_font_tt_parse(sfnt, ROBO_FONT_TT_FLAGS_NONE, &tt);
    }
    if (tt) {
        robo_font_tt_release(tt);
    }
    if (sfnt) {
        robo_font_sfnt_release(sfnt);
    }
    robo_font_source_release(src);
    return res;
}
