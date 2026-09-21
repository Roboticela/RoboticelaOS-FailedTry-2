#include "RoboFont/RoboFontOt.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

void FontCmapClear(FontCmap *c)
{
    if (!c) {
        return;
    }
    RoboFree(c->encodings);
    memset(c, 0, sizeof(*c));
}

int FontCmapFormatIsRecognized(uint16_t format)
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

static Result ReadSubtableHeader(FontSpan span,
                                          size_t off,
                                          uint16_t *format,
                                          uint32_t *length,
                                          uint32_t *language)
{
    FontReader r;
    uint16_t fmt;
    if (off + 2u > span.size) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReaderSeek(&r, off) != RoboOk ||
        FontReadU16be(&r, &fmt) != RoboOk) {
        return RoboParseError;
    }
    *format = fmt;
    *length = 0u;
    *language = 0u;

    if (fmt == 0u || fmt == 2u || fmt == 4u || fmt == 6u) {
        uint16_t len16;
        uint16_t lang16;
        if (FontReadU16be(&r, &len16) != RoboOk ||
            FontReadU16be(&r, &lang16) != RoboOk) {
            return RoboParseError;
        }
        *length = len16;
        *language = lang16;
        if (len16 < 6u || off + len16 > span.size) {
            return RoboParseError;
        }
        return RoboOk;
    }
    if (fmt == 8u || fmt == 10u || fmt == 12u || fmt == 13u) {
        uint16_t reserved;
        uint32_t len32;
        uint32_t lang32;
        if (FontReadU16be(&r, &reserved) != RoboOk ||
            FontReadU32be(&r, &len32) != RoboOk ||
            FontReadU32be(&r, &lang32) != RoboOk) {
            return RoboParseError;
        }
        *length = len32;
        *language = lang32;
        if (len32 < 12u || off + len32 > span.size) {
            return RoboParseError;
        }
        return RoboOk;
    }
    if (fmt == 14u) {
        uint32_t len32;
        if (FontReadU32be(&r, &len32) != RoboOk) {
            return RoboParseError;
        }
        *length = len32;
        if (len32 < 10u || off + len32 > span.size) {
            return RoboParseError;
        }
        return RoboOk;
    }
    /* Unknown format: accept encoding record but mark format only. */
    return RoboOk;
}

Result FontCmapParse(FontSpan span, FontCmap *out)
{
    FontReader r;
    uint16_t i;

    if (!out) {
        return RoboInvalidArgument;
    }
    FontCmapClear(out);
    if (span.size < 4u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReadU16be(&r, &out->version) != RoboOk ||
        FontReadU16be(&r, &out->numTables) != RoboOk) {
        return RoboParseError;
    }
    if (out->version != 0u) {
        return RoboNotSupported;
    }
    if (out->numTables > 1024u) {
        return RoboParseError;
    }
    if (FontCheckedRange(span.size, 4u, (size_t)out->numTables * 8u) != RoboOk) {
        return RoboParseError;
    }
    out->table = span;
    if (out->numTables == 0u) {
        return RoboOk;
    }
    out->encodings = (FontCmapEncoding *)RoboCalloc(
        out->numTables, sizeof(FontCmapEncoding));
    if (!out->encodings) {
        return RoboOutOfMemory;
    }
    for (i = 0u; i < out->numTables; i++) {
        uint16_t platform;
        uint16_t encoding;
        uint32_t soff;
        uint16_t format = 0;
        uint32_t length = 0;
        uint32_t language = 0;
        if (FontReadU16be(&r, &platform) != RoboOk ||
            FontReadU16be(&r, &encoding) != RoboOk ||
            FontReadU32be(&r, &soff) != RoboOk) {
            FontCmapClear(out);
            return RoboParseError;
        }
        if ((size_t)soff >= span.size) {
            FontCmapClear(out);
            return RoboParseError;
        }
        if (ReadSubtableHeader(span, soff, &format, &length, &language) != RoboOk) {
            FontCmapClear(out);
            return RoboParseError;
        }
        out->encodings[i].platformId = platform;
        out->encodings[i].encodingId = encoding;
        out->encodings[i].subtableOffset = soff;
        out->encodings[i].format = format;
        out->encodings[i].length = length;
        out->encodings[i].language = language;
    }
    return RoboOk;
}
