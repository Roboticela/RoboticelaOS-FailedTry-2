#include "RoboFont/RoboFontBinary.h"

void robo_font_reader_init(robo_font_reader_t *r, const uint8_t *data, size_t size)
{
    if (!r) {
        return;
    }
    r->span.data = data;
    r->span.size = (data || size == 0u) ? size : 0u;
    if (!data) {
        r->span.size = 0u;
    }
    r->pos = 0u;
}

void robo_font_reader_init_span(robo_font_reader_t *r, robo_font_span_t span)
{
    robo_font_reader_init(r, span.data, span.size);
}

size_t robo_font_reader_remaining(const robo_font_reader_t *r)
{
    if (!r || r->pos >= r->span.size) {
        return 0u;
    }
    return r->span.size - r->pos;
}

size_t robo_font_reader_pos(const robo_font_reader_t *r)
{
    return r ? r->pos : 0u;
}

robo_result_t robo_font_reader_seek(robo_font_reader_t *r, size_t pos)
{
    if (!r) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (pos > r->span.size) {
        return ROBO_PARSE_ERROR;
    }
    r->pos = pos;
    return ROBO_OK;
}

robo_result_t robo_font_checked_range(size_t size, size_t offset, size_t length)
{
    size_t end;
    if (offset > size) {
        return ROBO_PARSE_ERROR;
    }
    end = offset + length;
    if (end < offset) {
        return ROBO_PARSE_ERROR; /* overflow */
    }
    if (end > size) {
        return ROBO_PARSE_ERROR;
    }
    return ROBO_OK;
}

robo_result_t robo_font_span_slice(robo_font_span_t parent,
                                   size_t offset,
                                   size_t length,
                                   robo_font_span_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->data = NULL;
    out->size = 0u;
    if (!parent.data && length != 0u) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_checked_range(parent.size, offset, length) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    out->data = parent.data + offset;
    out->size = length;
    return ROBO_OK;
}

static robo_result_t need(robo_font_reader_t *r, size_t n)
{
    size_t end;
    if (!r || !r->span.data) {
        return ROBO_PARSE_ERROR;
    }
    end = r->pos + n;
    if (end < r->pos || end > r->span.size) {
        return ROBO_PARSE_ERROR;
    }
    return ROBO_OK;
}

robo_result_t robo_font_read_u8(robo_font_reader_t *r, uint8_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (need(r, 1u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    *out = r->span.data[r->pos++];
    return ROBO_OK;
}

robo_result_t robo_font_read_u16be(robo_font_reader_t *r, uint16_t *out)
{
    const uint8_t *p;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (need(r, 2u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    p = r->span.data + r->pos;
    r->pos += 2u;
    *out = (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
    return ROBO_OK;
}

robo_result_t robo_font_read_i16be(robo_font_reader_t *r, int16_t *out)
{
    uint16_t u;
    robo_result_t res = robo_font_read_u16be(r, &u);
    if (res != ROBO_OK) {
        return res;
    }
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = (int16_t)u;
    return ROBO_OK;
}

robo_result_t robo_font_read_u32be(robo_font_reader_t *r, uint32_t *out)
{
    const uint8_t *p;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (need(r, 4u) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    p = r->span.data + r->pos;
    r->pos += 4u;
    *out = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
    return ROBO_OK;
}

robo_result_t robo_font_read_i32be(robo_font_reader_t *r, int32_t *out)
{
    uint32_t u;
    robo_result_t res = robo_font_read_u32be(r, &u);
    if (res != ROBO_OK) {
        return res;
    }
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = (int32_t)u;
    return ROBO_OK;
}

robo_result_t robo_font_peek_u32be(const robo_font_reader_t *r, uint32_t *out)
{
    const uint8_t *p;
    size_t end;
    if (!r || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!r->span.data) {
        return ROBO_PARSE_ERROR;
    }
    end = r->pos + 4u;
    if (end < r->pos || end > r->span.size) {
        return ROBO_PARSE_ERROR;
    }
    p = r->span.data + r->pos;
    *out = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
    return ROBO_OK;
}
