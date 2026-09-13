/*
 * CFF charset, encoding, FDSelect (Goal 5.5).
 */

#include "RoboFont/RoboFontCff.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

void robo_font_cff_charset_clear(robo_font_cff_charset_t *c)
{
    if (!c) {
        return;
    }
    robo_free(c->sids);
    memset(c, 0, sizeof(*c));
}

robo_result_t robo_font_cff_charset_parse(robo_font_span_t table,
                                          size_t offset,
                                          uint32_t n_glyphs,
                                          int is_cid,
                                          robo_font_cff_charset_t *out)
{
    robo_font_reader_t r;
    uint8_t format;
    uint32_t i;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_cff_charset_clear(out);
    if (n_glyphs == 0u || n_glyphs > ROBO_FONT_CFF_MAX_GLYPHS) {
        return ROBO_PARSE_ERROR;
    }

    /* Predefined charsets: offset 0/1/2 are not table offsets when top.charset <= 2 */
    if (offset <= 2u) {
        /* Structural stub: only .notdef + sequential SIDs for testing predefined */
        out->format = (uint8_t)offset;
        out->glyph_count = n_glyphs;
        out->sids = (uint16_t *)robo_calloc(n_glyphs, sizeof(uint16_t));
        if (!out->sids) {
            return ROBO_OUT_OF_MEMORY;
        }
        out->sids[0] = 0u;
        for (i = 1u; i < n_glyphs; i++) {
            out->sids[i] = is_cid ? (uint16_t)i : (uint16_t)i; /* simplified */
        }
        return ROBO_OK;
    }

    if (offset >= table.size) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, table);
    if (robo_font_reader_seek(&r, offset) != ROBO_OK ||
        robo_font_read_u8(&r, &format) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    out->format = format;
    out->glyph_count = n_glyphs;
    out->sids = (uint16_t *)robo_calloc(n_glyphs, sizeof(uint16_t));
    if (!out->sids) {
        return ROBO_OUT_OF_MEMORY;
    }
    out->sids[0] = 0u;

    if (format == 0u) {
        for (i = 1u; i < n_glyphs; i++) {
            uint16_t sid;
            if (is_cid) {
                uint16_t cid;
                if (robo_font_read_u16be(&r, &cid) != ROBO_OK) {
                    robo_font_cff_charset_clear(out);
                    return ROBO_PARSE_ERROR;
                }
                out->sids[i] = cid;
            } else {
                if (robo_font_read_u16be(&r, &sid) != ROBO_OK) {
                    robo_font_cff_charset_clear(out);
                    return ROBO_PARSE_ERROR;
                }
                out->sids[i] = sid;
            }
        }
        return ROBO_OK;
    }

    if (format == 1u || format == 2u) {
        uint32_t gid = 1u;
        while (gid < n_glyphs) {
            uint16_t first;
            uint32_t nLeft;
            uint32_t k;
            if (robo_font_read_u16be(&r, &first) != ROBO_OK) {
                robo_font_cff_charset_clear(out);
                return ROBO_PARSE_ERROR;
            }
            if (format == 1u) {
                uint8_t nl;
                if (robo_font_read_u8(&r, &nl) != ROBO_OK) {
                    robo_font_cff_charset_clear(out);
                    return ROBO_PARSE_ERROR;
                }
                nLeft = nl;
            } else {
                uint16_t nl;
                if (robo_font_read_u16be(&r, &nl) != ROBO_OK) {
                    robo_font_cff_charset_clear(out);
                    return ROBO_PARSE_ERROR;
                }
                nLeft = nl;
            }
            if (nLeft + 1u > n_glyphs - gid) {
                robo_font_cff_charset_clear(out);
                return ROBO_PARSE_ERROR;
            }
            for (k = 0u; k <= nLeft; k++) {
                out->sids[gid++] = (uint16_t)(first + k);
            }
        }
        return ROBO_OK;
    }

    robo_font_cff_charset_clear(out);
    return ROBO_PARSE_ERROR;
}

robo_result_t robo_font_cff_charset_sid(const robo_font_cff_charset_t *c,
                                        uint32_t gid,
                                        uint16_t *out_sid)
{
    if (!c || !out_sid) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (gid >= c->glyph_count || !c->sids) {
        return ROBO_PARSE_ERROR;
    }
    *out_sid = c->sids[gid];
    return ROBO_OK;
}

void robo_font_cff_encoding_clear(robo_font_cff_encoding_t *e)
{
    if (!e) {
        return;
    }
    robo_free(e->codes);
    robo_free(e->supp_codes);
    robo_free(e->supp_sids);
    memset(e, 0, sizeof(*e));
}

robo_result_t robo_font_cff_encoding_parse(robo_font_span_t table,
                                           size_t offset,
                                           uint32_t n_glyphs,
                                           robo_font_cff_encoding_t *out)
{
    robo_font_reader_t r;
    uint8_t format_byte;
    uint8_t format;
    int has_supp;
    uint32_t i;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_cff_encoding_clear(out);

    if (offset <= 1u) {
        out->is_predefined = 1;
        out->format = (uint8_t)offset;
        return ROBO_OK;
    }

    if (offset >= table.size || n_glyphs == 0u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, table);
    if (robo_font_reader_seek(&r, offset) != ROBO_OK ||
        robo_font_read_u8(&r, &format_byte) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    format = (uint8_t)(format_byte & 0x7Fu);
    has_supp = (format_byte & 0x80u) != 0;
    out->format = format_byte;

    if (format == 0u) {
        uint8_t nCodes;
        if (robo_font_read_u8(&r, &nCodes) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        out->n_codes = nCodes;
        if (nCodes > 0u) {
            out->codes = (uint8_t *)robo_calloc(nCodes, 1u);
            if (!out->codes) {
                return ROBO_OUT_OF_MEMORY;
            }
            for (i = 0u; i < nCodes; i++) {
                if (robo_font_read_u8(&r, &out->codes[i]) != ROBO_OK) {
                    robo_font_cff_encoding_clear(out);
                    return ROBO_PARSE_ERROR;
                }
            }
        }
    } else if (format == 1u) {
        uint8_t nRanges;
        uint32_t filled = 0u;
        uint8_t *expanded = NULL;
        uint32_t cap = n_glyphs > 0u ? n_glyphs : 1u;
        if (robo_font_read_u8(&r, &nRanges) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        expanded = (uint8_t *)robo_calloc(cap, 1u);
        if (!expanded) {
            return ROBO_OUT_OF_MEMORY;
        }
        for (i = 0u; i < nRanges; i++) {
            uint8_t first;
            uint8_t nLeft;
            uint32_t k;
            if (robo_font_read_u8(&r, &first) != ROBO_OK ||
                robo_font_read_u8(&r, &nLeft) != ROBO_OK) {
                robo_free(expanded);
                return ROBO_PARSE_ERROR;
            }
            for (k = 0u; k <= (uint32_t)nLeft; k++) {
                if (filled >= cap) {
                    robo_free(expanded);
                    return ROBO_PARSE_ERROR;
                }
                expanded[filled++] = (uint8_t)(first + k);
            }
        }
        out->codes = expanded;
        out->n_codes = filled;
    } else {
        return ROBO_PARSE_ERROR;
    }

    if (has_supp) {
        uint8_t nSups;
        if (robo_font_read_u8(&r, &nSups) != ROBO_OK) {
            robo_font_cff_encoding_clear(out);
            return ROBO_PARSE_ERROR;
        }
        out->n_supplements = nSups;
        if (nSups > 0u) {
            out->supp_codes = (uint8_t *)robo_calloc(nSups, 1u);
            out->supp_sids = (uint16_t *)robo_calloc(nSups, sizeof(uint16_t));
            if (!out->supp_codes || !out->supp_sids) {
                robo_font_cff_encoding_clear(out);
                return ROBO_OUT_OF_MEMORY;
            }
            for (i = 0u; i < nSups; i++) {
                if (robo_font_read_u8(&r, &out->supp_codes[i]) != ROBO_OK ||
                    robo_font_read_u16be(&r, &out->supp_sids[i]) != ROBO_OK) {
                    robo_font_cff_encoding_clear(out);
                    return ROBO_PARSE_ERROR;
                }
            }
        }
    }
    (void)n_glyphs;
    return ROBO_OK;
}

void robo_font_cff_fdselect_clear(robo_font_cff_fdselect_t *f)
{
    if (!f) {
        return;
    }
    robo_free(f->fds);
    memset(f, 0, sizeof(*f));
}

robo_result_t robo_font_cff_fdselect_parse(robo_font_span_t table,
                                           size_t offset,
                                           uint32_t n_glyphs,
                                           uint32_t fd_count,
                                           robo_font_cff_fdselect_t *out)
{
    robo_font_reader_t r;
    uint8_t format;
    uint32_t i;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_cff_fdselect_clear(out);
    if (n_glyphs == 0u || fd_count == 0u || fd_count > ROBO_FONT_CFF_MAX_FDARRAY) {
        return ROBO_PARSE_ERROR;
    }
    if (offset >= table.size) {
        return ROBO_PARSE_ERROR;
    }

    robo_font_reader_init_span(&r, table);
    if (robo_font_reader_seek(&r, offset) != ROBO_OK ||
        robo_font_read_u8(&r, &format) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    out->format = format;
    out->n_glyphs = n_glyphs;
    out->fds = (uint8_t *)robo_calloc(n_glyphs, 1u);
    if (!out->fds) {
        return ROBO_OUT_OF_MEMORY;
    }

    if (format == 0u) {
        for (i = 0u; i < n_glyphs; i++) {
            if (robo_font_read_u8(&r, &out->fds[i]) != ROBO_OK) {
                robo_font_cff_fdselect_clear(out);
                return ROBO_PARSE_ERROR;
            }
            if ((uint32_t)out->fds[i] >= fd_count) {
                robo_font_cff_fdselect_clear(out);
                return ROBO_PARSE_ERROR;
            }
        }
        return ROBO_OK;
    }

    if (format == 3u) {
        uint16_t nRanges;
        uint16_t first;
        if (robo_font_read_u16be(&r, &nRanges) != ROBO_OK) {
            robo_font_cff_fdselect_clear(out);
            return ROBO_PARSE_ERROR;
        }
        out->n_ranges = nRanges;
        if (nRanges == 0u) {
            robo_font_cff_fdselect_clear(out);
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_read_u16be(&r, &first) != ROBO_OK) {
            robo_font_cff_fdselect_clear(out);
            return ROBO_PARSE_ERROR;
        }
        for (i = 0u; i < nRanges; i++) {
            uint8_t fd;
            uint16_t next;
            uint32_t g;
            if (robo_font_read_u8(&r, &fd) != ROBO_OK ||
                robo_font_read_u16be(&r, &next) != ROBO_OK) {
                robo_font_cff_fdselect_clear(out);
                return ROBO_PARSE_ERROR;
            }
            if ((uint32_t)fd >= fd_count || next < first || next > n_glyphs) {
                robo_font_cff_fdselect_clear(out);
                return ROBO_PARSE_ERROR;
            }
            for (g = first; g < next; g++) {
                out->fds[g] = fd;
            }
            first = next;
        }
        if (first != n_glyphs) {
            robo_font_cff_fdselect_clear(out);
            return ROBO_PARSE_ERROR;
        }
        return ROBO_OK;
    }

    robo_font_cff_fdselect_clear(out);
    return ROBO_NOT_SUPPORTED;
}

robo_result_t robo_font_cff_fdselect_get(const robo_font_cff_fdselect_t *f,
                                         uint32_t gid,
                                         uint8_t *out_fd)
{
    if (!f || !out_fd) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (gid >= f->n_glyphs || !f->fds) {
        return ROBO_PARSE_ERROR;
    }
    *out_fd = f->fds[gid];
    return ROBO_OK;
}
