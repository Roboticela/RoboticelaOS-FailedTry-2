#include "RoboFont/RoboFontBinary.h"

void FontReaderInit(FontReader *r, const uint8_t *data, size_t size)
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

void FontReaderInitSpan(FontReader *r, FontSpan span)
{
    FontReaderInit(r, span.data, span.size);
}

size_t FontReaderRemaining(const FontReader *r)
{
    if (!r || r->pos >= r->span.size) {
        return 0u;
    }
    return r->span.size - r->pos;
}

size_t FontReaderPos(const FontReader *r)
{
    return r ? r->pos : 0u;
}

Result FontReaderSeek(FontReader *r, size_t pos)
{
    if (!r) {
        return RoboInvalidArgument;
    }
    if (pos > r->span.size) {
        return RoboParseError;
    }
    r->pos = pos;
    return RoboOk;
}

Result FontCheckedRange(size_t size, size_t offset, size_t length)
{
    size_t end;
    if (offset > size) {
        return RoboParseError;
    }
    end = offset + length;
    if (end < offset) {
        return RoboParseError; /* overflow */
    }
    if (end > size) {
        return RoboParseError;
    }
    return RoboOk;
}

Result FontSpanSlice(FontSpan parent,
                                   size_t offset,
                                   size_t length,
                                   FontSpan *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    out->data = NULL;
    out->size = 0u;
    if (!parent.data && length != 0u) {
        return RoboParseError;
    }
    if (FontCheckedRange(parent.size, offset, length) != RoboOk) {
        return RoboParseError;
    }
    out->data = parent.data + offset;
    out->size = length;
    return RoboOk;
}

static Result need(FontReader *r, size_t n)
{
    size_t end;
    if (!r || !r->span.data) {
        return RoboParseError;
    }
    end = r->pos + n;
    if (end < r->pos || end > r->span.size) {
        return RoboParseError;
    }
    return RoboOk;
}

Result FontReadU8(FontReader *r, uint8_t *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    if (need(r, 1u) != RoboOk) {
        return RoboParseError;
    }
    *out = r->span.data[r->pos++];
    return RoboOk;
}

Result FontReadU16be(FontReader *r, uint16_t *out)
{
    const uint8_t *p;
    if (!out) {
        return RoboInvalidArgument;
    }
    if (need(r, 2u) != RoboOk) {
        return RoboParseError;
    }
    p = r->span.data + r->pos;
    r->pos += 2u;
    *out = (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
    return RoboOk;
}

Result FontReadI16be(FontReader *r, int16_t *out)
{
    uint16_t u;
    Result res = FontReadU16be(r, &u);
    if (res != RoboOk) {
        return res;
    }
    if (!out) {
        return RoboInvalidArgument;
    }
    *out = (int16_t)u;
    return RoboOk;
}

Result FontReadU32be(FontReader *r, uint32_t *out)
{
    const uint8_t *p;
    if (!out) {
        return RoboInvalidArgument;
    }
    if (need(r, 4u) != RoboOk) {
        return RoboParseError;
    }
    p = r->span.data + r->pos;
    r->pos += 4u;
    *out = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
    return RoboOk;
}

Result FontReadI32be(FontReader *r, int32_t *out)
{
    uint32_t u;
    Result res = FontReadU32be(r, &u);
    if (res != RoboOk) {
        return res;
    }
    if (!out) {
        return RoboInvalidArgument;
    }
    *out = (int32_t)u;
    return RoboOk;
}

Result FontPeekU32be(const FontReader *r, uint32_t *out)
{
    const uint8_t *p;
    size_t end;
    if (!r || !out) {
        return RoboInvalidArgument;
    }
    if (!r->span.data) {
        return RoboParseError;
    }
    end = r->pos + 4u;
    if (end < r->pos || end > r->span.size) {
        return RoboParseError;
    }
    p = r->span.data + r->pos;
    *out = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
    return RoboOk;
}
