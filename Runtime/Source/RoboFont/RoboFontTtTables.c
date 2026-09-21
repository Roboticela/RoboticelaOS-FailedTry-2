#include "RoboFont/RoboFontTt.h"

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicode.h"

#include <stddef.h>
#include <string.h>

/* ---- helpers ---- */

static Result ReadI64be(FontReader *r, int64_t *out)
{
    uint32_t hi;
    uint32_t lo;
    if (FontReadU32be(r, &hi) != RoboOk || FontReadU32be(r, &lo) != RoboOk) {
        return RoboParseError;
    }
    if (!out) {
        return RoboInvalidArgument;
    }
    *out = ((int64_t)(uint64_t)hi << 32) | (int64_t)(uint64_t)lo;
    return RoboOk;
}

Result FontHeadParse(FontSpan span, FontHead *out)
{
    FontReader r;
    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    if (span.size < 54u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReadU16be(&r, &out->majorVersion) != RoboOk ||
        FontReadU16be(&r, &out->minorVersion) != RoboOk ||
        FontReadU32be(&r, &out->fontRevision) != RoboOk ||
        FontReadU32be(&r, &out->checksumAdjustment) != RoboOk ||
        FontReadU32be(&r, &out->magicNumber) != RoboOk ||
        FontReadU16be(&r, &out->flags) != RoboOk ||
        FontReadU16be(&r, &out->unitsPerEm) != RoboOk ||
        ReadI64be(&r, &out->created) != RoboOk ||
        ReadI64be(&r, &out->modified) != RoboOk ||
        FontReadI16be(&r, &out->xMin) != RoboOk ||
        FontReadI16be(&r, &out->yMin) != RoboOk ||
        FontReadI16be(&r, &out->xMax) != RoboOk ||
        FontReadI16be(&r, &out->yMax) != RoboOk ||
        FontReadU16be(&r, &out->macStyle) != RoboOk ||
        FontReadU16be(&r, &out->lowestRecPpem) != RoboOk ||
        FontReadI16be(&r, &out->fontDirectionHint) != RoboOk ||
        FontReadI16be(&r, &out->indexToLocFormat) != RoboOk ||
        FontReadI16be(&r, &out->glyphDataFormat) != RoboOk) {
        return RoboParseError;
    }
    if (out->majorVersion != 1u || out->minorVersion != 0u) {
        return RoboParseError;
    }
    if (out->magicNumber != RoboFontHeadMagic) {
        return RoboParseError;
    }
    if (out->unitsPerEm < 16u || out->unitsPerEm > 16384u) {
        return RoboParseError;
    }
    if (out->xMin > out->xMax || out->yMin > out->yMax) {
        return RoboParseError;
    }
    if (out->indexToLocFormat != 0 && out->indexToLocFormat != 1) {
        return RoboParseError;
    }
    if (out->glyphDataFormat != 0) {
        return RoboParseError;
    }
    return RoboOk;
}

Result FontHheaParse(FontSpan span, FontHhea *out)
{
    FontReader r;
    int16_t reserved;
    int i;
    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    if (span.size < 36u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReadU16be(&r, &out->majorVersion) != RoboOk ||
        FontReadU16be(&r, &out->minorVersion) != RoboOk ||
        FontReadI16be(&r, &out->ascender) != RoboOk ||
        FontReadI16be(&r, &out->descender) != RoboOk ||
        FontReadI16be(&r, &out->lineGap) != RoboOk ||
        FontReadU16be(&r, &out->advanceWidthMax) != RoboOk ||
        FontReadI16be(&r, &out->minLeftSideBearing) != RoboOk ||
        FontReadI16be(&r, &out->minRightSideBearing) != RoboOk ||
        FontReadI16be(&r, &out->xMaxExtent) != RoboOk ||
        FontReadI16be(&r, &out->caretSlopeRise) != RoboOk ||
        FontReadI16be(&r, &out->caretSlopeRun) != RoboOk ||
        FontReadI16be(&r, &out->caretOffset) != RoboOk) {
        return RoboParseError;
    }
    for (i = 0; i < 4; i++) {
        if (FontReadI16be(&r, &reserved) != RoboOk) {
            return RoboParseError;
        }
    }
    if (FontReadI16be(&r, &out->metricDataFormat) != RoboOk ||
        FontReadU16be(&r, &out->numberOfHMetrics) != RoboOk) {
        return RoboParseError;
    }
    if (out->majorVersion != 1u || out->minorVersion != 0u) {
        return RoboParseError;
    }
    if (out->metricDataFormat != 0) {
        return RoboParseError;
    }
    return RoboOk;
}

Result FontMaxpParse(FontSpan span, FontMaxp *out)
{
    FontReader r;
    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    if (span.size < 6u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReadU32be(&r, &out->version) != RoboOk ||
        FontReadU16be(&r, &out->numGlyphs) != RoboOk) {
        return RoboParseError;
    }
    if (out->version == 0x00005000u) {
        /* CFF-style maxp: 6 bytes only. */
        return RoboOk;
    }
    if (out->version != 0x00010000u) {
        return RoboNotSupported;
    }
    if (span.size < 32u) {
        return RoboParseError;
    }
    if (FontReadU16be(&r, &out->maxPoints) != RoboOk ||
        FontReadU16be(&r, &out->maxContours) != RoboOk ||
        FontReadU16be(&r, &out->maxCompositePoints) != RoboOk ||
        FontReadU16be(&r, &out->maxCompositeContours) != RoboOk ||
        FontReadU16be(&r, &out->maxZones) != RoboOk ||
        FontReadU16be(&r, &out->maxTwilightPoints) != RoboOk ||
        FontReadU16be(&r, &out->maxStorage) != RoboOk ||
        FontReadU16be(&r, &out->maxFunctionDefs) != RoboOk ||
        FontReadU16be(&r, &out->maxInstructionDefs) != RoboOk ||
        FontReadU16be(&r, &out->maxStackElements) != RoboOk ||
        FontReadU16be(&r, &out->maxSizeOfInstructions) != RoboOk ||
        FontReadU16be(&r, &out->maxComponentElements) != RoboOk ||
        FontReadU16be(&r, &out->maxComponentDepth) != RoboOk) {
        return RoboParseError;
    }
    return RoboOk;
}

Result FontPostParse(FontSpan span, FontPost *out)
{
    FontReader r;
    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    if (span.size < 32u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReadU32be(&r, &out->version) != RoboOk ||
        FontReadI32be(&r, &out->italicAngle) != RoboOk ||
        FontReadI16be(&r, &out->underlinePosition) != RoboOk ||
        FontReadI16be(&r, &out->underlineThickness) != RoboOk ||
        FontReadU32be(&r, &out->isFixedPitch) != RoboOk ||
        FontReadU32be(&r, &out->minMemType42) != RoboOk ||
        FontReadU32be(&r, &out->maxMemType42) != RoboOk ||
        FontReadU32be(&r, &out->minMemType1) != RoboOk ||
        FontReadU32be(&r, &out->maxMemType1) != RoboOk) {
        return RoboParseError;
    }
    /* Supported header versions: 1.0, 2.0, 2.5, 3.0 (extra data ignored). */
    if (out->version != 0x00010000u && out->version != 0x00020000u &&
        out->version != 0x00025000u && out->version != 0x00030000u) {
        return RoboNotSupported;
    }
    return RoboOk;
}

Result FontOs2Parse(FontSpan span, FontOs2 *out)
{
    FontReader r;
    uint32_t i;
    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    /* Version 0 minimum length is 78 bytes. */
    if (span.size < 78u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReadU16be(&r, &out->version) != RoboOk ||
        FontReadI16be(&r, &out->xAvgCharWidth) != RoboOk ||
        FontReadU16be(&r, &out->usWeightClass) != RoboOk ||
        FontReadU16be(&r, &out->usWidthClass) != RoboOk ||
        FontReadU16be(&r, &out->fsType) != RoboOk ||
        FontReadI16be(&r, &out->ySubscriptXSize) != RoboOk ||
        FontReadI16be(&r, &out->ySubscriptYSize) != RoboOk ||
        FontReadI16be(&r, &out->ySubscriptXOffset) != RoboOk ||
        FontReadI16be(&r, &out->ySubscriptYOffset) != RoboOk ||
        FontReadI16be(&r, &out->ySuperscriptXSize) != RoboOk ||
        FontReadI16be(&r, &out->ySuperscriptYSize) != RoboOk ||
        FontReadI16be(&r, &out->ySuperscriptXOffset) != RoboOk ||
        FontReadI16be(&r, &out->ySuperscriptYOffset) != RoboOk ||
        FontReadI16be(&r, &out->yStrikeoutSize) != RoboOk ||
        FontReadI16be(&r, &out->yStrikeoutPosition) != RoboOk ||
        FontReadI16be(&r, &out->sFamilyClass) != RoboOk) {
        return RoboParseError;
    }
    for (i = 0u; i < 10u; i++) {
        if (FontReadU8(&r, &out->panose[i]) != RoboOk) {
            return RoboParseError;
        }
    }
    if (FontReadU32be(&r, &out->ulUnicodeRange1) != RoboOk ||
        FontReadU32be(&r, &out->ulUnicodeRange2) != RoboOk ||
        FontReadU32be(&r, &out->ulUnicodeRange3) != RoboOk ||
        FontReadU32be(&r, &out->ulUnicodeRange4) != RoboOk) {
        return RoboParseError;
    }
    for (i = 0u; i < 4u; i++) {
        if (FontReadU8(&r, &out->achVendId[i]) != RoboOk) {
            return RoboParseError;
        }
    }
    if (FontReadU16be(&r, &out->fsSelection) != RoboOk ||
        FontReadU16be(&r, &out->usFirstCharIndex) != RoboOk ||
        FontReadU16be(&r, &out->usLastCharIndex) != RoboOk ||
        FontReadI16be(&r, &out->sTypoAscender) != RoboOk ||
        FontReadI16be(&r, &out->sTypoDescender) != RoboOk ||
        FontReadI16be(&r, &out->sTypoLineGap) != RoboOk ||
        FontReadU16be(&r, &out->usWinAscent) != RoboOk ||
        FontReadU16be(&r, &out->usWinDescent) != RoboOk) {
        return RoboParseError;
    }
    if (out->version >= 1u) {
        if (span.size < 86u) {
            return RoboParseError;
        }
        if (FontReadU32be(&r, &out->ulCodePageRange1) != RoboOk ||
            FontReadU32be(&r, &out->ulCodePageRange2) != RoboOk) {
            return RoboParseError;
        }
        out->hasCodePageRanges = 1u;
    }
    if (out->version >= 2u) {
        if (span.size < 96u) {
            return RoboParseError;
        }
        if (FontReadI16be(&r, &out->sxHeight) != RoboOk ||
            FontReadI16be(&r, &out->sCapHeight) != RoboOk ||
            FontReadU16be(&r, &out->usDefaultChar) != RoboOk ||
            FontReadU16be(&r, &out->usBreakChar) != RoboOk ||
            FontReadU16be(&r, &out->usMaxContext) != RoboOk) {
            return RoboParseError;
        }
        out->hasV2Metrics = 1u;
    }
    if (out->version >= 5u) {
        if (span.size < 100u) {
            return RoboParseError;
        }
        if (FontReadU16be(&r, &out->usLowerOpticalPointSize) != RoboOk ||
            FontReadU16be(&r, &out->usUpperOpticalPointSize) != RoboOk) {
            return RoboParseError;
        }
        out->hasV5Optical = 1u;
    }
    if (out->version > 5u) {
        /* Newer versions: accept if minimum fields for v5 present; trailing ignored. */
        if (span.size < 100u) {
            return RoboParseError;
        }
    }
    if (out->usWeightClass < 1u || out->usWeightClass > 1000u) {
        return RoboParseError;
    }
    if (out->usWidthClass < 1u || out->usWidthClass > 9u) {
        return RoboParseError;
    }
    return RoboOk;
}

void FontNameTableClear(FontNameTable *t)
{
    if (!t) {
        return;
    }
    RoboFree(t->records);
    t->records = NULL;
    t->count = 0u;
    t->storage.data = NULL;
    t->storage.size = 0u;
}

Result FontNameParse(FontSpan span, FontNameTable *out)
{
    FontReader r;
    uint16_t i;
    size_t storageOff;
    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    if (span.size < 6u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReadU16be(&r, &out->format) != RoboOk ||
        FontReadU16be(&r, &out->count) != RoboOk ||
        FontReadU16be(&r, &out->stringOffset) != RoboOk) {
        return RoboParseError;
    }
    if (out->format > 1u) {
        return RoboNotSupported;
    }
    if (out->count > RoboFontTtMaxNameRecords) {
        return RoboParseError;
    }
    if ((size_t)out->count * 12u > SIZE_MAX - 6u) {
        return RoboParseError;
    }
    if (span.size < 6u + (size_t)out->count * 12u) {
        return RoboParseError;
    }
    if ((size_t)out->stringOffset > span.size) {
        return RoboParseError;
    }
    storageOff = out->stringOffset;
    out->storage.data = span.data + storageOff;
    out->storage.size = span.size - storageOff;

    if (out->count == 0u) {
        return RoboOk;
    }
    out->records =
        (FontNameRecord *)RoboCalloc(out->count, sizeof(FontNameRecord));
    if (!out->records) {
        return RoboOutOfMemory;
    }
    for (i = 0u; i < out->count; i++) {
        FontNameRecord *rec = &out->records[i];
        if (FontReadU16be(&r, &rec->platformId) != RoboOk ||
            FontReadU16be(&r, &rec->encodingId) != RoboOk ||
            FontReadU16be(&r, &rec->languageId) != RoboOk ||
            FontReadU16be(&r, &rec->nameId) != RoboOk ||
            FontReadU16be(&r, &rec->length) != RoboOk ||
            FontReadU16be(&r, &rec->offset) != RoboOk) {
            FontNameTableClear(out);
            return RoboParseError;
        }
        if (rec->length > RoboFontTtMaxNameString) {
            FontNameTableClear(out);
            return RoboParseError;
        }
        if (FontCheckedRange(out->storage.size, rec->offset, rec->length) != RoboOk) {
            FontNameTableClear(out);
            return RoboParseError;
        }
    }
    return RoboOk;
}

static int NamePrefer(const FontNameRecord *a, const FontNameRecord *b)
{
    /* Prefer Windows Unicode BMP / full, then Mac Roman. */
    int sa = 0;
    int sb = 0;
    if (a->platformId == 3u && (a->encodingId == 1u || a->encodingId == 10u)) {
        sa = 3;
    } else if (a->platformId == 1u && a->encodingId == 0u) {
        sa = 1;
    }
    if (b->platformId == 3u && (b->encodingId == 1u || b->encodingId == 10u)) {
        sb = 3;
    } else if (b->platformId == 1u && b->encodingId == 0u) {
        sb = 1;
    }
    if (sa != sb) {
        return sa - sb;
    }
    /* Prefer en-US language 0x0409 on Windows. */
    if (a->platformId == 3u && a->languageId == 0x0409u) {
        return 1;
    }
    if (b->platformId == 3u && b->languageId == 0x0409u) {
        return -1;
    }
    return 0;
}

static Result DecodeMacRomanAscii(const uint8_t *data,
                                            size_t len,
                                            char *buf,
                                            size_t bufCap,
                                            size_t *outLen)
{
    size_t i;
    if (!buf || bufCap == 0u) {
        return RoboInvalidArgument;
    }
    if (len + 1u > bufCap) {
        return RoboParseError;
    }
    for (i = 0u; i < len; i++) {
        if (data[i] >= 0x80u) {
            /* Non-ASCII Mac Roman — leave as-is only if printable Latin-1-ish not
             * guaranteed; reject for safety (caller can try Unicode record). */
            return RoboNotSupported;
        }
        buf[i] = (char)data[i];
    }
    buf[len] = '\0';
    if (outLen) {
        *outLen = len;
    }
    return RoboOk;
}

Result FontNameGetUtf8(const FontNameTable *t,
                                      uint16_t nameId,
                                      char *buf,
                                      size_t bufCap,
                                      size_t *outLen)
{
    const FontNameRecord *best = NULL;
    uint16_t i;
    const uint8_t *bytes;
    size_t len;

    if (!t || !buf || bufCap == 0u) {
        return RoboInvalidArgument;
    }
    if (outLen) {
        *outLen = 0u;
    }
    buf[0] = '\0';
    if (!t->records) {
        return RoboNotFound;
    }
    for (i = 0u; i < t->count; i++) {
        if (t->records[i].nameId != nameId) {
            continue;
        }
        if (!best || NamePrefer(&t->records[i], best) > 0) {
            best = &t->records[i];
        }
    }
    if (!best) {
        return RoboNotFound;
    }
    bytes = t->storage.data + best->offset;
    len = best->length;

    if (best->platformId == 3u &&
        (best->encodingId == 1u || best->encodingId == 10u)) {
        Uchar16 *u16;
        size_t nunits;
        size_t ui;
        size_t utf8Len = 0u;
        UnicodeStatus st;

        if ((len & 1u) != 0u) {
            return RoboParseError;
        }
        nunits = len / 2u;
        u16 = (Uchar16 *)RoboCalloc(nunits == 0u ? 1u : nunits, sizeof(Uchar16));
        if (!u16) {
            return RoboOutOfMemory;
        }
        for (ui = 0u; ui < nunits; ui++) {
            u16[ui] = (Uchar16)(((uint16_t)bytes[ui * 2u] << 8) |
                                       (uint16_t)bytes[ui * 2u + 1u]);
        }
        st = UnicodeUtf16ToUtf8(u16, nunits, NULL, 0u, &utf8Len,
                                        RoboUnicodeStrict);
        if (st != RoboUnicodeOk && st != RoboUnicodeNoSpace) {
            RoboFree(u16);
            return RoboParseError;
        }
        if (utf8Len + 1u > bufCap) {
            RoboFree(u16);
            return RoboParseError;
        }
        st = UnicodeUtf16ToUtf8(u16, nunits, buf, bufCap, &utf8Len,
                                        RoboUnicodeStrict);
        RoboFree(u16);
        if (st != RoboUnicodeOk) {
            return RoboParseError;
        }
        if (utf8Len < bufCap) {
            buf[utf8Len] = '\0';
        }
        if (outLen) {
            *outLen = utf8Len;
        }
        return RoboOk;
    }
    if (best->platformId == 1u && best->encodingId == 0u) {
        return DecodeMacRomanAscii(bytes, len, buf, bufCap, outLen);
    }
    return RoboNotSupported;
}
