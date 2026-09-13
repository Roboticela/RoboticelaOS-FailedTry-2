/*
 * CFF / CFF2 font bundle parse (Goal 5.5).
 */

#include "RoboFont/RoboFontCff.h"
#include "RoboFont/RoboFontSource.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

static void cff_font_clear_contents(robo_font_cff_font_t *f)
{
    uint32_t i;
    if (!f) {
        return;
    }
    robo_font_cff_index_clear(&f->name_index);
    robo_font_cff_index_clear(&f->top_dict_index);
    robo_font_cff_index_clear(&f->string_index);
    robo_font_cff_index_clear(&f->global_subrs);
    robo_font_cff_index_clear(&f->charstrings);
    robo_font_cff_index_clear(&f->fdarray_index);
    robo_font_cff_index_clear(&f->local_subrs);
    robo_font_cff_top_dict_clear(&f->top);
    robo_font_cff_private_dict_clear(&f->priv);
    robo_font_cff_charset_clear(&f->charset);
    robo_font_cff_encoding_clear(&f->encoding);
    robo_font_cff_fdselect_clear(&f->fdselect);
    if (f->font_dicts) {
        for (i = 0u; i < f->fd_count; i++) {
            robo_font_cff_font_dict_clear(&f->font_dicts[i]);
        }
        robo_free(f->font_dicts);
    }
    if (f->fd_privates) {
        for (i = 0u; i < f->fd_count; i++) {
            robo_font_cff_private_dict_clear(&f->fd_privates[i]);
        }
        robo_free(f->fd_privates);
    }
    if (f->fd_local_subrs) {
        for (i = 0u; i < f->fd_count; i++) {
            robo_font_cff_index_clear(&f->fd_local_subrs[i]);
        }
        robo_free(f->fd_local_subrs);
    }
    robo_font_cff_vstore_clear(&f->vstore);
    if (f->sfnt) {
        robo_font_sfnt_release(f->sfnt);
        f->sfnt = NULL;
    }
}

void robo_font_cff_retain(robo_font_cff_font_t *font)
{
    if (font) {
        font->refs++;
    }
}

void robo_font_cff_release(robo_font_cff_font_t *font)
{
    if (!font) {
        return;
    }
    if (font->refs > 1) {
        font->refs--;
        return;
    }
    cff_font_clear_contents(font);
    robo_free(font);
}

robo_font_outline_tech_t robo_font_cff_outline_tech(const robo_font_cff_font_t *font)
{
    return font ? font->tech : ROBO_FONT_OUTLINE_UNKNOWN;
}

uint32_t robo_font_cff_glyph_count(const robo_font_cff_font_t *font)
{
    return font ? font->glyph_count : 0u;
}

robo_result_t robo_font_cff_charstring_bytes(const robo_font_cff_font_t *font,
                                             uint32_t gid,
                                             robo_font_span_t *out)
{
    if (!font || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_font_cff_index_get(&font->charstrings, gid, out);
}

static robo_result_t parse_private_at(robo_font_span_t table,
                                      int32_t priv_size,
                                      int32_t priv_offset,
                                      int is_cff2,
                                      robo_font_cff_private_dict_t *out_priv,
                                      robo_font_cff_index_t *out_local)
{
    robo_font_span_t priv_span;
    robo_font_cff_dict_t raw;
    size_t abs;
    robo_result_t res;

    if (priv_size < 0 || priv_offset < 0) {
        return ROBO_OK;
    }
    /* Private offset is relative to the start of the CFF/CFF2 table. */
    abs = (size_t)priv_offset;
    if (robo_font_span_slice(table, abs, (size_t)priv_size, &priv_span) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    memset(&raw, 0, sizeof(raw));
    res = robo_font_cff_dict_parse(priv_span, &raw);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_font_cff_private_dict_interpret(&raw, out_priv);
    robo_font_cff_dict_clear(&raw);
    if (res != ROBO_OK) {
        return res;
    }
    if (out_priv->subrs >= 0 && out_local) {
        size_t subr_off = abs + (size_t)out_priv->subrs;
        size_t end = 0u;
        if (is_cff2) {
            res = robo_font_cff2_index_parse(table, subr_off, out_local, &end);
        } else {
            res = robo_font_cff_index_parse(table, subr_off, out_local, &end);
        }
        if (res != ROBO_OK) {
            return res;
        }
    }
    return ROBO_OK;
}

static robo_result_t parse_cff1(robo_font_span_t table,
                                uint32_t flags,
                                robo_font_cff_font_t *f)
{
    robo_font_reader_t r;
    size_t cursor;
    size_t end;
    robo_font_span_t top_bytes;
    robo_font_cff_dict_t top_raw;
    robo_result_t res;

    robo_font_reader_init_span(&r, table);
    if (robo_font_read_u8(&r, &f->major) != ROBO_OK ||
        robo_font_read_u8(&r, &f->minor) != ROBO_OK ||
        robo_font_read_u8(&r, &f->hdr_size) != ROBO_OK ||
        robo_font_read_u8(&r, &f->off_size) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (f->major != 1u) {
        return ROBO_NOT_SUPPORTED;
    }
    if (f->hdr_size < 4u || f->off_size < 1u || f->off_size > 4u) {
        return ROBO_PARSE_ERROR;
    }
    if ((size_t)f->hdr_size > table.size) {
        return ROBO_PARSE_ERROR;
    }
    cursor = f->hdr_size;

    res = robo_font_cff_index_parse(table, cursor, &f->name_index, &end);
    if (res != ROBO_OK) {
        return res;
    }
    cursor = end;

    res = robo_font_cff_index_parse(table, cursor, &f->top_dict_index, &end);
    if (res != ROBO_OK) {
        return res;
    }
    cursor = end;

    res = robo_font_cff_index_parse(table, cursor, &f->string_index, &end);
    if (res != ROBO_OK) {
        return res;
    }
    cursor = end;

    res = robo_font_cff_index_parse(table, cursor, &f->global_subrs, &end);
    if (res != ROBO_OK) {
        return res;
    }

    if (f->top_dict_index.count < 1u) {
        return ROBO_PARSE_ERROR;
    }
    res = robo_font_cff_index_get(&f->top_dict_index, 0u, &top_bytes);
    if (res != ROBO_OK) {
        return res;
    }
    memset(&top_raw, 0, sizeof(top_raw));
    res = robo_font_cff_dict_parse(top_bytes, &top_raw);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_font_cff_top_dict_interpret(&top_raw, &f->top);
    robo_font_cff_dict_clear(&top_raw);
    if (res != ROBO_OK) {
        return res;
    }

    if (f->top.charstring_type != 2) {
        return ROBO_NOT_SUPPORTED;
    }
    if (f->top.charstrings < 0) {
        if (flags & ROBO_FONT_CFF_FLAGS_REQUIRE_CHARSTRINGS) {
            return ROBO_PARSE_ERROR;
        }
    } else {
        res = robo_font_cff_index_parse(table, (size_t)f->top.charstrings,
                                        &f->charstrings, &end);
        if (res != ROBO_OK) {
            return res;
        }
        f->glyph_count = f->charstrings.count;
        if (f->glyph_count > ROBO_FONT_CFF_MAX_GLYPHS) {
            return ROBO_PARSE_ERROR;
        }
    }

    if ((flags & ROBO_FONT_CFF_FLAGS_PARSE_PRIVATE) && !f->top.is_cid &&
        f->top.private_offset >= 0 && f->top.private_size >= 0) {
        res = parse_private_at(table, f->top.private_size, f->top.private_offset, 0,
                               &f->priv, &f->local_subrs);
        if (res != ROBO_OK) {
            return res;
        }
    }

    if ((flags & ROBO_FONT_CFF_FLAGS_PARSE_CID) && f->top.is_cid) {
        uint32_t i;
        if (f->top.fdarray < 0 || f->top.fdselect < 0) {
            return ROBO_PARSE_ERROR;
        }
        res = robo_font_cff_index_parse(table, (size_t)f->top.fdarray,
                                        &f->fdarray_index, &end);
        if (res != ROBO_OK) {
            return res;
        }
        f->fd_count = f->fdarray_index.count;
        if (f->fd_count == 0u || f->fd_count > ROBO_FONT_CFF_MAX_FDARRAY) {
            return ROBO_PARSE_ERROR;
        }
        f->font_dicts = (robo_font_cff_font_dict_t *)robo_calloc(
            f->fd_count, sizeof(robo_font_cff_font_dict_t));
        f->fd_privates = (robo_font_cff_private_dict_t *)robo_calloc(
            f->fd_count, sizeof(robo_font_cff_private_dict_t));
        f->fd_local_subrs =
            (robo_font_cff_index_t *)robo_calloc(f->fd_count, sizeof(robo_font_cff_index_t));
        if (!f->font_dicts || !f->fd_privates || !f->fd_local_subrs) {
            return ROBO_OUT_OF_MEMORY;
        }
        for (i = 0u; i < f->fd_count; i++) {
            robo_font_span_t fd_bytes;
            robo_font_cff_dict_t raw;
            res = robo_font_cff_index_get(&f->fdarray_index, i, &fd_bytes);
            if (res != ROBO_OK) {
                return res;
            }
            memset(&raw, 0, sizeof(raw));
            res = robo_font_cff_dict_parse(fd_bytes, &raw);
            if (res != ROBO_OK) {
                return res;
            }
            res = robo_font_cff_font_dict_interpret(&raw, &f->font_dicts[i]);
            robo_font_cff_dict_clear(&raw);
            if (res != ROBO_OK) {
                return res;
            }
            if (flags & ROBO_FONT_CFF_FLAGS_PARSE_PRIVATE) {
                res = parse_private_at(table, f->font_dicts[i].private_size,
                                       f->font_dicts[i].private_offset, 0,
                                       &f->fd_privates[i], &f->fd_local_subrs[i]);
                if (res != ROBO_OK) {
                    return res;
                }
            }
        }
        res = robo_font_cff_fdselect_parse(table, (size_t)f->top.fdselect,
                                           f->glyph_count, f->fd_count, &f->fdselect);
        if (res != ROBO_OK) {
            return res;
        }
    }

    if ((flags & ROBO_FONT_CFF_FLAGS_PARSE_CHARSET) && f->glyph_count > 0u &&
        f->top.charset >= 0) {
        res = robo_font_cff_charset_parse(table, (size_t)f->top.charset, f->glyph_count,
                                          f->top.is_cid, &f->charset);
        if (res != ROBO_OK) {
            return res;
        }
    }

    if ((flags & ROBO_FONT_CFF_FLAGS_PARSE_ENCODING) && !f->top.is_cid &&
        f->top.encoding >= 0) {
        res = robo_font_cff_encoding_parse(table, (size_t)f->top.encoding, f->glyph_count,
                                           &f->encoding);
        if (res != ROBO_OK) {
            return res;
        }
    }

    return ROBO_OK;
}

static robo_result_t parse_cff2(robo_font_span_t table,
                                uint32_t flags,
                                robo_font_cff_font_t *f)
{
    robo_font_reader_t r;
    size_t cursor;
    size_t end;
    robo_font_span_t top_bytes;
    robo_font_cff_dict_t top_raw;
    robo_result_t res;
    uint32_t i;

    robo_font_reader_init_span(&r, table);
    if (robo_font_read_u8(&r, &f->major) != ROBO_OK ||
        robo_font_read_u8(&r, &f->minor) != ROBO_OK ||
        robo_font_read_u8(&r, &f->hdr_size) != ROBO_OK ||
        robo_font_read_u16be(&r, &f->top_dict_length) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (f->major != 2u) {
        return ROBO_NOT_SUPPORTED;
    }
    if (f->hdr_size < 5u) {
        return ROBO_PARSE_ERROR;
    }
    cursor = f->hdr_size;
    if (cursor + (size_t)f->top_dict_length > table.size) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_span_slice(table, cursor, f->top_dict_length, &top_bytes) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    cursor += f->top_dict_length;

    memset(&top_raw, 0, sizeof(top_raw));
    res = robo_font_cff_dict_parse(top_bytes, &top_raw);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_font_cff_top_dict_interpret(&top_raw, &f->top);
    robo_font_cff_dict_clear(&top_raw);
    if (res != ROBO_OK) {
        return res;
    }
    /* CFF2: single-byte ops 15/16 are vsindex/blend, not charset/encoding. */
    f->top.charset = -1;
    f->top.encoding = -1;

    /* Global Subr INDEX follows Top DICT */
    res = robo_font_cff2_index_parse(table, cursor, &f->global_subrs, &end);
    if (res != ROBO_OK) {
        return res;
    }

    if (f->top.charstrings < 0) {
        if (flags & ROBO_FONT_CFF_FLAGS_REQUIRE_CHARSTRINGS) {
            return ROBO_PARSE_ERROR;
        }
    } else {
        res = robo_font_cff2_index_parse(table, (size_t)f->top.charstrings,
                                         &f->charstrings, &end);
        if (res != ROBO_OK) {
            return res;
        }
        f->glyph_count = f->charstrings.count;
    }

    if ((flags & ROBO_FONT_CFF_FLAGS_PARSE_VSTORE) && f->top.vstore >= 0) {
        res = robo_font_cff_vstore_parse(table, (size_t)f->top.vstore, &f->vstore);
        if (res != ROBO_OK) {
            return res;
        }
        f->has_vstore = 1;
    }

    if (f->top.fdarray < 0) {
        return ROBO_PARSE_ERROR; /* CFF2 requires FDArray */
    }
    res = robo_font_cff2_index_parse(table, (size_t)f->top.fdarray, &f->fdarray_index,
                                     &end);
    if (res != ROBO_OK) {
        return res;
    }
    f->fd_count = f->fdarray_index.count;
    if (f->fd_count == 0u || f->fd_count > ROBO_FONT_CFF_MAX_FDARRAY) {
        return ROBO_PARSE_ERROR;
    }
    f->font_dicts = (robo_font_cff_font_dict_t *)robo_calloc(
        f->fd_count, sizeof(robo_font_cff_font_dict_t));
    f->fd_privates = (robo_font_cff_private_dict_t *)robo_calloc(
        f->fd_count, sizeof(robo_font_cff_private_dict_t));
    f->fd_local_subrs =
        (robo_font_cff_index_t *)robo_calloc(f->fd_count, sizeof(robo_font_cff_index_t));
    if (!f->font_dicts || !f->fd_privates || !f->fd_local_subrs) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0u; i < f->fd_count; i++) {
        robo_font_span_t fd_bytes;
        robo_font_cff_dict_t raw;
        res = robo_font_cff_index_get(&f->fdarray_index, i, &fd_bytes);
        if (res != ROBO_OK) {
            return res;
        }
        memset(&raw, 0, sizeof(raw));
        res = robo_font_cff_dict_parse(fd_bytes, &raw);
        if (res != ROBO_OK) {
            return res;
        }
        res = robo_font_cff_font_dict_interpret(&raw, &f->font_dicts[i]);
        robo_font_cff_dict_clear(&raw);
        if (res != ROBO_OK) {
            return res;
        }
        if (flags & ROBO_FONT_CFF_FLAGS_PARSE_PRIVATE) {
            res = parse_private_at(table, f->font_dicts[i].private_size,
                                   f->font_dicts[i].private_offset, 1,
                                   &f->fd_privates[i], &f->fd_local_subrs[i]);
            if (res != ROBO_OK) {
                return res;
            }
        }
    }

    if (f->top.fdselect >= 0 && f->glyph_count > 0u) {
        res = robo_font_cff_fdselect_parse(table, (size_t)f->top.fdselect, f->glyph_count,
                                           f->fd_count, &f->fdselect);
        if (res != ROBO_OK) {
            return res;
        }
    }

    return ROBO_OK;
}

robo_result_t robo_font_cff_parse(robo_font_sfnt_t *sfnt,
                                  uint32_t flags,
                                  robo_font_cff_font_t **out)
{
    robo_font_outline_tech_t tech;
    robo_font_span_t table;
    robo_font_cff_font_t *f;
    robo_result_t res;

    if (!sfnt || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    tech = robo_font_ot_detect_outline(sfnt);
    if (tech != ROBO_FONT_OUTLINE_CFF && tech != ROBO_FONT_OUTLINE_CFF2) {
        return ROBO_NOT_SUPPORTED;
    }

    f = (robo_font_cff_font_t *)robo_calloc(1u, sizeof(*f));
    if (!f) {
        return ROBO_OUT_OF_MEMORY;
    }
    f->refs = 1;
    f->tech = tech;
    robo_font_sfnt_retain(sfnt);
    f->sfnt = sfnt;

    if (tech == ROBO_FONT_OUTLINE_CFF2) {
        if (robo_font_sfnt_table_bytes(sfnt, ROBO_FONT_TAG_CFF2, &table) != ROBO_OK) {
            robo_font_cff_release(f);
            return ROBO_PARSE_ERROR;
        }
        res = parse_cff2(table, flags, f);
    } else {
        if (robo_font_sfnt_table_bytes(sfnt, ROBO_FONT_TAG_CFF, &table) != ROBO_OK) {
            robo_font_cff_release(f);
            return ROBO_PARSE_ERROR;
        }
        res = parse_cff1(table, flags, f);
    }

    if (res != ROBO_OK) {
        robo_font_cff_release(f);
        return res;
    }
    *out = f;
    return ROBO_OK;
}

robo_result_t robo_font_cff_parse_glyph(const robo_font_cff_font_t *font,
                                        uint32_t gid,
                                        robo_font_cff_charstring_t *out)
{
    robo_font_span_t cs;
    robo_font_cff_subr_ctx_t ctx;
    const robo_font_cff_private_dict_t *priv;
    const robo_font_cff_index_t *local;
    double def_w = 0.0;
    double nom_w = 0.0;
    int has_nom = 0;

    if (!font || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_font_cff_charstring_bytes(font, gid, &cs) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.global_subrs = &font->global_subrs;
    ctx.global_bias = robo_font_cff_subr_bias(font->global_subrs.count);

    priv = &font->priv;
    local = &font->local_subrs;
    if (font->fd_count > 0u && font->fdselect.fds) {
        uint8_t fd = 0u;
        if (robo_font_cff_fdselect_get(&font->fdselect, gid, &fd) == ROBO_OK &&
            fd < font->fd_count) {
            priv = &font->fd_privates[fd];
            local = &font->fd_local_subrs[fd];
        }
    }
    ctx.local_subrs = local;
    ctx.local_bias = robo_font_cff_subr_bias(local->count);
    if (priv->has_default_width) {
        def_w = priv->default_width_x;
    }
    if (priv->has_nominal_width) {
        nom_w = priv->nominal_width_x;
        has_nom = 1;
    }

    {
        uint32_t flags = ROBO_FONT_CFF_CS_FLAGS_NONE;
        if (font->major == 2u) {
            flags |= ROBO_FONT_CFF_CS_FLAGS_ALLOW_NO_ENDCHAR;
        }
        return robo_font_cff_charstring_parse_ex(cs, &ctx, def_w, nom_w, has_nom,
                                                 flags, out);
    }
}

robo_result_t robo_font_cff_fuzz_parse(const uint8_t *data, size_t size)
{
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_cff_font_t *cff = NULL;
    robo_result_t res;

    if (!data && size > 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_font_source_create_memory_copy(data, size, &src);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_font_sfnt_parse(src, 0u, 0u, &sfnt);
    if (res != ROBO_OK) {
        robo_font_source_release(src);
        return res;
    }
    res = robo_font_cff_parse(sfnt, ROBO_FONT_CFF_FLAGS_PARSE_ALL, &cff);
    if (cff) {
        robo_font_cff_release(cff);
    }
    robo_font_sfnt_release(sfnt);
    robo_font_source_release(src);
    return res;
}
