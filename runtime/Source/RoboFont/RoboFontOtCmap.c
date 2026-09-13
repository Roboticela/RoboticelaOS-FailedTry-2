#include "RoboFont/RoboFontOt.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

void robo_font_cmap_clear(robo_font_cmap_t *c)
{
    if (!c) {
        return;
    }
    robo_free(c->encodings);
    memset(c, 0, sizeof(*c));
}

int robo_font_cmap_format_is_recognized(uint16_t format)
{
    switch (format) {
    case 0:
    case 2:
    case 4:
    case 6:
    case 8:
    case 10:
    case 12:
    case 13:
    case 14:
        return 1;
    default:
        return 0;
    }
}

static robo_result_t read_subtable_header(robo_font_span_t span,
                                          size_t off,
                                          uint16_t *format,
                                          uint32_t *length,
                                          uint32_t *language)
{
    robo_font_reader_t r;
    uint16_t fmt;
    if (off + 2u > span.size) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_reader_seek(&r, off) != ROBO_OK ||
        robo_font_read_u16be(&r, &fmt) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    *format = fmt;
    *length = 0u;
    *language = 0u;

    if (fmt == 0u || fmt == 2u || fmt == 4u || fmt == 6u) {
        uint16_t len16;
        uint16_t lang16;
        if (robo_font_read_u16be(&r, &len16) != ROBO_OK ||
            robo_font_read_u16be(&r, &lang16) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        *length = len16;
        *language = lang16;
        if (len16 < 6u || off + len16 > span.size) {
            return ROBO_PARSE_ERROR;
        }
        return ROBO_OK;
    }
    if (fmt == 8u || fmt == 10u || fmt == 12u || fmt == 13u) {
        uint16_t reserved;
        uint32_t len32;
        uint32_t lang32;
        if (robo_font_read_u16be(&r, &reserved) != ROBO_OK ||
            robo_font_read_u32be(&r, &len32) != ROBO_OK ||
            robo_font_read_u32be(&r, &lang32) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        *length = len32;
        *language = lang32;
        if (len32 < 12u || off + len32 > span.size) {
            return ROBO_PARSE_ERROR;
        }
        return ROBO_OK;
    }
    if (fmt == 14u) {
        uint32_t len32;
        if (robo_font_read_u32be(&r, &len32) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        *length = len32;
        if (len32 < 10u || off + len32 > span.size) {
            return ROBO_PARSE_ERROR;
        }
        return ROBO_OK;
    }
    /* Unknown format: accept encoding record but mark format only. */
    return ROBO_OK;
}

robo_result_t robo_font_cmap_parse(robo_font_span_t span, robo_font_cmap_t *out)
{
    robo_font_reader_t r;
    uint16_t i;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_cmap_clear(out);
    if (span.size < 4u) {
        return ROBO_PARSE_ERROR;
    }
    robo_font_reader_init_span(&r, span);
    if (robo_font_read_u16be(&r, &out->version) != ROBO_OK ||
        robo_font_read_u16be(&r, &out->num_tables) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (out->version != 0u) {
        return ROBO_NOT_SUPPORTED;
    }
    if (out->num_tables > 1024u) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_checked_range(span.size, 4u, (size_t)out->num_tables * 8u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    out->table = span;
    if (out->num_tables == 0u) {
        return ROBO_OK;
    }
    out->encodings = (robo_font_cmap_encoding_t *)robo_calloc(
        out->num_tables, sizeof(robo_font_cmap_encoding_t));
    if (!out->encodings) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0u; i < out->num_tables; i++) {
        uint16_t platform;
        uint16_t encoding;
        uint32_t soff;
        uint16_t format = 0;
        uint32_t length = 0;
        uint32_t language = 0;
        if (robo_font_read_u16be(&r, &platform) != ROBO_OK ||
            robo_font_read_u16be(&r, &encoding) != ROBO_OK ||
            robo_font_read_u32be(&r, &soff) != ROBO_OK) {
            robo_font_cmap_clear(out);
            return ROBO_PARSE_ERROR;
        }
        if ((size_t)soff >= span.size) {
            robo_font_cmap_clear(out);
            return ROBO_PARSE_ERROR;
        }
        if (read_subtable_header(span, soff, &format, &length, &language) != ROBO_OK) {
            robo_font_cmap_clear(out);
            return ROBO_PARSE_ERROR;
        }
        out->encodings[i].platform_id = platform;
        out->encodings[i].encoding_id = encoding;
        out->encodings[i].subtable_offset = soff;
        out->encodings[i].format = format;
        out->encodings[i].length = length;
        out->encodings[i].language = language;
    }
    return ROBO_OK;
}
