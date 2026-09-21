/*
 * RoboFont cmap mapping suite (Goal 5.6).
 * Synthetic fixtures only — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboTest.h"

#include <string.h>

static void PutU8(uint8_t **p, uint8_t v) { *(*p)++ = v; }
static void PutU16(uint8_t **p, uint16_t v)
{
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static void PutI16(uint8_t **p, int16_t v) { PutU16(p, (uint16_t)v); }
static void PutU32(uint8_t **p, uint32_t v)
{
    *(*p)++ = (uint8_t)(v >> 24);
    *(*p)++ = (uint8_t)(v >> 16);
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}

typedef struct {
    uint32_t tag;
    const uint8_t *data;
    uint32_t length;
} SynthTable;

static void WriteSearchFields(uint8_t *hdr, uint16_t numTables)
{
    uint16_t pow2 = 1u;
    uint16_t entry = 0u;
    uint8_t *p;
    while ((uint16_t)(pow2 * 2u) <= numTables) {
        pow2 = (uint16_t)(pow2 * 2u);
        entry++;
    }
    p = hdr + 4;
    PutU16(&p, numTables);
    PutU16(&p, (uint16_t)(pow2 * 16u));
    PutU16(&p, entry);
    PutU16(&p, (uint16_t)(numTables * 16u - pow2 * 16u));
}

static size_t BuildSfnt(uint8_t *out,
                         size_t outCap,
                         uint32_t version,
                         const SynthTable *tables,
                         uint32_t n)
{
    size_t dirEnd = 12u + (size_t)n * 16u;
    size_t cursor;
    size_t need = dirEnd;
    uint32_t i;
    uint8_t *p;

    for (i = 0u; i < n; i++) {
        need += ((size_t)tables[i].length + 3u) & ~(size_t)3u;
    }
    if (need > outCap) {
        return 0u;
    }
    memset(out, 0, need);
    p = out;
    PutU32(&p, version);
    WriteSearchFields(out, (uint16_t)n);
    cursor = dirEnd;
    for (i = 0u; i < n; i++) {
        uint32_t pad = (uint32_t)(((size_t)tables[i].length + 3u) & ~(size_t)3u);
        uint8_t *rec = out + 12u + (size_t)i * 16u;
        p = rec;
        PutU32(&p, tables[i].tag);
        PutU32(&p, 0u);
        PutU32(&p, (uint32_t)cursor);
        PutU32(&p, tables[i].length);
        memcpy(out + cursor, tables[i].data, tables[i].length);
        cursor += pad;
    }
    return cursor;
}

static size_t BuildMaxpV05(uint8_t *out, uint16_t numGlyphs)
{
    uint8_t *p = out;
    PutU32(&p, 0x00005000u);
    PutU16(&p, numGlyphs);
    return (size_t)(p - out);
}

/* cmap header + one encoding record pointing at offset 12 */
static void CmapBegin(uint8_t **p, uint16_t platform, uint16_t encoding)
{
    PutU16(p, 0); /* version */
    PutU16(p, 1); /* numTables */
    PutU16(p, platform);
    PutU16(p, encoding);
    PutU32(p, 12u); /* subtable offset */
}

static void TestCmapScalar(void)
{
    RoboCheck(FontCmapIsUnicodeScalar(0x41u));
    RoboCheck(FontCmapIsUnicodeScalar(0x10FFFFu));
    RoboCheck(!FontCmapIsUnicodeScalar(0xD800u));
    RoboCheck(!FontCmapIsUnicodeScalar(0xDFFFu));
    RoboCheck(!FontCmapIsUnicodeScalar(0x110000u));
}

static void TestCmapFormat0(void)
{
    uint8_t buf[280];
    uint8_t *p = buf;
    FontCmapMap *map = NULL;
    FontCmapLookupResult r;
    int i;

    CmapBegin(&p, 1, 0); /* Mac Roman — still lookupable */
    PutU16(&p, 0);       /* format */
    PutU16(&p, 262);     /* length */
    PutU16(&p, 0);       /* language */
    for (i = 0; i < 256; i++) {
        PutU8(&p, (uint8_t)(i == 65 ? 1 : 0));
    }

    RoboCheckEqInt(
        FontCmapMapCreateFromSpan((FontSpan){buf, (size_t)(p - buf)},
                                            16u, &map),
        RoboOk);
    RoboCheckEqInt(FontCmapMapSelectedFormat(map), 0);
    RoboCheckEqInt(FontCmapLookup(map, 65u, &r), RoboOk);
    RoboCheckEqInt(r.status, RoboFontCmapMapped);
    RoboCheckEqInt((int)r.glyphId, 1);
    RoboCheckEqInt(FontCmapLookup(map, 66u, &r), RoboOk);
    RoboCheckEqInt(r.status, RoboFontCmapMapped);
    RoboCheckEqInt((int)r.glyphId, 0); /* explicit .notdef in format 0 */
    RoboCheckEqInt(FontCmapLookup(map, 0x100u, &r), RoboOk);
    RoboCheckEqInt(r.status, RoboFontCmapUnmapped);
    RoboCheckEqInt(FontCmapLookup(map, 0xD800u, &r), RoboOk);
    RoboCheckEqInt(r.status, RoboFontCmapInvalidCodePoint);
    FontCmapMapRelease(map);
}

static void TestCmapFormat2(void)
{
    uint8_t buf[600];
    uint8_t *p = buf;
    uint8_t *lenPtr;
    uint8_t *subStart;
    FontCmapMap *map = NULL;
    FontCmapLookupResult r;
    int i;

    CmapBegin(&p, 3, 1);
    subStart = p;
    PutU16(&p, 2);
    lenPtr = p;
    PutU16(&p, 0);
    PutU16(&p, 0);
    for (i = 0; i < 256; i++) {
        PutU16(&p, 0); /* all high bytes → subheader 0 */
    }
    /* subheader 0: map low byte 0x41 via idDelta */
    PutU16(&p, 0x41);
    PutU16(&p, 1);
    PutI16(&p, (int16_t)(4 - 0x41));
    PutU16(&p, 0);
    {
        uint16_t len = (uint16_t)(p - subStart);
        lenPtr[0] = (uint8_t)(len >> 8);
        lenPtr[1] = (uint8_t)len;
    }

    RoboCheckEqInt(
        FontCmapMapCreateFromSpan((FontSpan){buf, (size_t)(p - buf)},
                                            16u, &map),
        RoboOk);
    RoboCheckEqInt(FontCmapMapSelectedFormat(map), 2);
    RoboCheckEqInt(FontCmapLookup(map, 0x41u, &r), RoboOk);
    RoboCheckEqInt(r.status, RoboFontCmapMapped);
    RoboCheckEqInt((int)r.glyphId, 4);
    RoboCheckEqInt(FontCmapLookup(map, 0x42u, &r), RoboOk);
    RoboCheckEqInt(r.status, RoboFontCmapUnmapped);
    FontCmapMapRelease(map);

    /* Range-offset path: glyphIndexArray after one subheader */
    p = buf;
    CmapBegin(&p, 3, 1);
    subStart = p;
    PutU16(&p, 2);
    lenPtr = p;
    PutU16(&p, 0);
    PutU16(&p, 0);
    for (i = 0; i < 256; i++) {
        PutU16(&p, 0);
    }
    PutU16(&p, 0x41);
    PutU16(&p, 2); /* 'A','B' */
    PutI16(&p, 0);
    PutU16(&p, 2); /* idRangeOffset → next word is glyph array[0] */
    PutU16(&p, 8); /* 'A' → 8 */
    PutU16(&p, 0); /* 'B' unmapped via 0 */
    {
        uint16_t len = (uint16_t)(p - subStart);
        lenPtr[0] = (uint8_t)(len >> 8);
        lenPtr[1] = (uint8_t)len;
    }
    RoboCheckEqInt(
        FontCmapMapCreateFromSpan((FontSpan){buf, (size_t)(p - buf)},
                                            16u, &map),
        RoboOk);
    RoboCheckEqInt(FontCmapLookup(map, 0x41u, &r), RoboOk);
    RoboCheckEqInt((int)r.glyphId, 8);
    RoboCheckEqInt(FontCmapLookup(map, 0x42u, &r), RoboOk);
    RoboCheckEqInt(r.status, RoboFontCmapUnmapped);
    FontCmapMapRelease(map);
}

static void TestCmapFormat4(void)
{
    uint8_t buf[128];
    uint8_t *p = buf;
    uint8_t *lenPtr;
    FontCmapMap *map = NULL;
    FontCmapLookupResult r;

    CmapBegin(&p, 3, 1);
    PutU16(&p, 4);
    lenPtr = p;
    PutU16(&p, 0); /* length placeholder */
    PutU16(&p, 0); /* language */
    PutU16(&p, 4); /* segCountX2 = 2 segments */
    PutU16(&p, 4); /* searchRange */
    PutU16(&p, 1); /* entrySelector */
    PutU16(&p, 0); /* rangeShift */
    PutU16(&p, 0x0041);
    PutU16(&p, 0xFFFF);
    PutU16(&p, 0); /* reservedPad */
    PutU16(&p, 0x0041);
    PutU16(&p, 0xFFFF);
    PutI16(&p, (int16_t)(1 - 0x41)); /* idDelta for A → 1 */
    PutI16(&p, 1);
    PutU16(&p, 0);
    PutU16(&p, 0);
    {
        uint16_t len = (uint16_t)(p - (buf + 12));
        lenPtr[0] = (uint8_t)(len >> 8);
        lenPtr[1] = (uint8_t)len;
    }

    RoboCheckEqInt(
        FontCmapMapCreateFromSpan((FontSpan){buf, (size_t)(p - buf)},
                                            16u, &map),
        RoboOk);
    RoboCheckEqInt(FontCmapMapSelectedFormat(map), 4);
    RoboCheckEqInt(FontCmapLookup(map, 0x41u, &r), RoboOk);
    RoboCheckEqInt(r.status, RoboFontCmapMapped);
    RoboCheckEqInt((int)r.glyphId, 1);
    RoboCheckEqInt(FontCmapLookup(map, 0x42u, &r), RoboOk);
    RoboCheckEqInt(r.status, RoboFontCmapUnmapped);
    FontCmapMapRelease(map);
}

static void TestCmapFormat6(void)
{
    uint8_t buf[64];
    uint8_t *p = buf;
    uint8_t *lenPtr;
    FontCmapMap *map = NULL;
    FontCmapLookupResult r;

    CmapBegin(&p, 3, 1);
    PutU16(&p, 6);
    lenPtr = p;
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 0x40); /* firstCode */
    PutU16(&p, 2);    /* entryCount */
    PutU16(&p, 0);    /* '@' unmapped */
    PutU16(&p, 7);    /* 'A' → 7 */
    {
        uint16_t len = (uint16_t)(p - (buf + 12));
        lenPtr[0] = (uint8_t)(len >> 8);
        lenPtr[1] = (uint8_t)len;
    }

    RoboCheckEqInt(
        FontCmapMapCreateFromSpan((FontSpan){buf, (size_t)(p - buf)},
                                            16u, &map),
        RoboOk);
    RoboCheckEqInt(FontCmapLookup(map, 0x41u, &r), RoboOk);
    RoboCheckEqInt(r.status, RoboFontCmapMapped);
    RoboCheckEqInt((int)r.glyphId, 7);
    RoboCheckEqInt(FontCmapLookup(map, 0x40u, &r), RoboOk);
    RoboCheckEqInt(r.status, RoboFontCmapMapped);
    RoboCheckEqInt((int)r.glyphId, 0); /* .notdef still mapped */
    FontCmapMapRelease(map);
}

static void TestCmapFormat10(void)
{
    uint8_t buf[48];
    uint8_t *p = buf;
    uint8_t *lenPtr;
    FontCmapMap *map = NULL;
    FontCmapLookupResult r;

    CmapBegin(&p, 3, 10);
    PutU16(&p, 10);
    PutU16(&p, 0); /* reserved */
    lenPtr = p;
    PutU32(&p, 0);
    PutU32(&p, 0); /* language */
    PutU32(&p, 0x1F600u);
    PutU32(&p, 2u);
    PutU16(&p, 11);
    PutU16(&p, 12);
    {
        uint32_t len = (uint32_t)(p - (buf + 12));
        lenPtr[0] = (uint8_t)(len >> 24);
        lenPtr[1] = (uint8_t)(len >> 16);
        lenPtr[2] = (uint8_t)(len >> 8);
        lenPtr[3] = (uint8_t)len;
    }

    RoboCheckEqInt(
        FontCmapMapCreateFromSpan((FontSpan){buf, (size_t)(p - buf)},
                                            32u, &map),
        RoboOk);
    RoboCheckEqInt(FontCmapMapSelectedFormat(map), 10);
    RoboCheckEqInt(FontCmapLookup(map, 0x1F600u, &r), RoboOk);
    RoboCheckEqInt(r.status, RoboFontCmapMapped);
    RoboCheckEqInt((int)r.glyphId, 11);
    RoboCheckEqInt(FontCmapLookup(map, 0x1F601u, &r), RoboOk);
    RoboCheckEqInt((int)r.glyphId, 12);
    RoboCheckEqInt(FontCmapLookup(map, 0x41u, &r), RoboOk);
    RoboCheckEqInt(r.status, RoboFontCmapUnmapped);
    RoboCheckEqInt(FontCmapLookup(map, 0xD800u, &r), RoboOk);
    RoboCheckEqInt(r.status, RoboFontCmapInvalidCodePoint);
    FontCmapMapRelease(map);

    /* Overflowing numChars past U+10FFFF */
    {
        uint8_t bad[40];
        uint8_t *q = bad;
        uint8_t *lp;
        CmapBegin(&q, 3, 10);
        PutU16(&q, 10);
        PutU16(&q, 0);
        lp = q;
        PutU32(&q, 0);
        PutU32(&q, 0);
        PutU32(&q, 0x10FFFEu);
        PutU32(&q, 3u); /* end past plane */
        PutU16(&q, 1);
        PutU16(&q, 2);
        PutU16(&q, 3);
        {
            uint32_t len = (uint32_t)(q - (bad + 12));
            lp[0] = (uint8_t)(len >> 24);
            lp[1] = (uint8_t)(len >> 16);
            lp[2] = (uint8_t)(len >> 8);
            lp[3] = (uint8_t)len;
        }
        RoboCheck(FontCmapMapCreateFromSpan(
                       (FontSpan){bad, (size_t)(q - bad)}, 16u, &map) !=
                   RoboOk);
    }
}

static void TestCmapFormat12(void)
{
    uint8_t buf[80];
    uint8_t *p = buf;
    uint8_t *lenPtr;
    FontCmapMap *map = NULL;
    FontCmapLookupResult r;

    CmapBegin(&p, 3, 10);
    PutU16(&p, 12);
    PutU16(&p, 0); /* reserved */
    lenPtr = p;
    PutU32(&p, 0); /* length */
    PutU32(&p, 0); /* language */
    PutU32(&p, 2); /* nGroups */
    PutU32(&p, 0x41u);
    PutU32(&p, 0x42u);
    PutU32(&p, 1u); /* A→1, B→2 */
    PutU32(&p, 0x1F600u);
    PutU32(&p, 0x1F600u);
    PutU32(&p, 10u);
    {
        uint32_t len = (uint32_t)(p - (buf + 12));
        lenPtr[0] = (uint8_t)(len >> 24);
        lenPtr[1] = (uint8_t)(len >> 16);
        lenPtr[2] = (uint8_t)(len >> 8);
        lenPtr[3] = (uint8_t)len;
    }

    RoboCheckEqInt(
        FontCmapMapCreateFromSpan((FontSpan){buf, (size_t)(p - buf)},
                                            32u, &map),
        RoboOk);
    RoboCheckEqInt(FontCmapMapSelectedFormat(map), 12);
    RoboCheckEqInt(FontCmapLookup(map, 0x41u, &r), RoboOk);
    RoboCheckEqInt((int)r.glyphId, 1);
    RoboCheckEqInt(FontCmapLookup(map, 0x42u, &r), RoboOk);
    RoboCheckEqInt((int)r.glyphId, 2);
    RoboCheckEqInt(FontCmapLookup(map, 0x1F600u, &r), RoboOk);
    RoboCheckEqInt(r.status, RoboFontCmapMapped);
    RoboCheckEqInt((int)r.glyphId, 10);
    FontCmapMapRelease(map);
}

static void TestCmapFormat13(void)
{
    uint8_t buf[64];
    uint8_t *p = buf;
    uint8_t *lenPtr;
    FontCmapMap *map = NULL;
    FontCmapLookupResult r;

    CmapBegin(&p, 3, 10);
    PutU16(&p, 13);
    PutU16(&p, 0);
    lenPtr = p;
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 1);
    PutU32(&p, 0x1000u);
    PutU32(&p, 0x1002u);
    PutU32(&p, 5u); /* all → glyph 5 */
    {
        uint32_t len = (uint32_t)(p - (buf + 12));
        lenPtr[0] = (uint8_t)(len >> 24);
        lenPtr[1] = (uint8_t)(len >> 16);
        lenPtr[2] = (uint8_t)(len >> 8);
        lenPtr[3] = (uint8_t)len;
    }

    RoboCheckEqInt(
        FontCmapMapCreateFromSpan((FontSpan){buf, (size_t)(p - buf)},
                                            16u, &map),
        RoboOk);
    RoboCheckEqInt(FontCmapLookup(map, 0x1001u, &r), RoboOk);
    RoboCheckEqInt((int)r.glyphId, 5);
    FontCmapMapRelease(map);
}

static void TestCmapFormat14(void)
{
    uint8_t c[160];
    uint8_t *q = c;
    uint32_t f4Off;
    uint32_t f14Off;
    uint32_t f14Start;
    uint32_t defOffRel;
    uint8_t *f14LenPtr;
    uint8_t *f4LenPtr;
    FontCmapMap *map = NULL;
    FontCmapUvsResult u;

    PutU16(&q, 0);
    PutU16(&q, 2);
    PutU16(&q, 3);
    PutU16(&q, 1);
    PutU32(&q, 0);
    PutU16(&q, 0);
    PutU16(&q, 5);
    PutU32(&q, 0);

    f4Off = (uint32_t)(q - c);
    c[8] = (uint8_t)(f4Off >> 24);
    c[9] = (uint8_t)(f4Off >> 16);
    c[10] = (uint8_t)(f4Off >> 8);
    c[11] = (uint8_t)f4Off;

    PutU16(&q, 4);
    f4LenPtr = q;
    PutU16(&q, 0);
    PutU16(&q, 0);
    PutU16(&q, 4);
    PutU16(&q, 4);
    PutU16(&q, 1);
    PutU16(&q, 0);
    PutU16(&q, 0x41);
    PutU16(&q, 0xFFFF);
    PutU16(&q, 0);
    PutU16(&q, 0x41);
    PutU16(&q, 0xFFFF);
    PutI16(&q, (int16_t)(1 - 0x41));
    PutI16(&q, 1);
    PutU16(&q, 0);
    PutU16(&q, 0);
    {
        uint16_t len = (uint16_t)(q - (c + f4Off));
        f4LenPtr[0] = (uint8_t)(len >> 8);
        f4LenPtr[1] = (uint8_t)len;
    }

    f14Off = (uint32_t)(q - c);
    c[16] = (uint8_t)(f14Off >> 24);
    c[17] = (uint8_t)(f14Off >> 16);
    c[18] = (uint8_t)(f14Off >> 8);
    c[19] = (uint8_t)f14Off;
    f14Start = f14Off;
    PutU16(&q, 14);
    f14LenPtr = q;
    PutU32(&q, 0);
    PutU32(&q, 1);
    PutU8(&q, 0x00);
    PutU8(&q, 0xFE);
    PutU8(&q, 0x0F);
    {
        uint8_t *offp = q;
        PutU32(&q, 0);
        PutU32(&q, 0);
        defOffRel = (uint32_t)(q - (c + f14Start));
        offp[0] = (uint8_t)(defOffRel >> 24);
        offp[1] = (uint8_t)(defOffRel >> 16);
        offp[2] = (uint8_t)(defOffRel >> 8);
        offp[3] = (uint8_t)defOffRel;
        PutU32(&q, 1);
        PutU8(&q, 0x00);
        PutU8(&q, 0x00);
        PutU8(&q, 0x41);
        PutU8(&q, 0);
        {
            uint32_t len = (uint32_t)(q - (c + f14Start));
            f14LenPtr[0] = (uint8_t)(len >> 24);
            f14LenPtr[1] = (uint8_t)(len >> 16);
            f14LenPtr[2] = (uint8_t)(len >> 8);
            f14LenPtr[3] = (uint8_t)len;
        }
    }

    RoboCheckEqInt(
        FontCmapMapCreateFromSpan((FontSpan){c, (size_t)(q - c)}, 16u,
                                            &map),
        RoboOk);
    RoboCheck(FontCmapMapHasUvs(map));
    RoboCheckEqInt(FontCmapLookupUvs(map, 0x41u, 0xFE0Fu, &u), RoboOk);
    RoboCheckEqInt(u.status, RoboFontCmapUvsDefault);
    RoboCheckEqInt(FontCmapLookupUvs(map, 0x42u, 0xFE0Fu, &u), RoboOk);
    RoboCheckEqInt(u.status, RoboFontCmapUvsUnsupported);
    FontCmapMapRelease(map);
}

static void TestCmapSelectionAndGidValidate(void)
{
    /* Prefer format 12 (3,10) over format 4 (3,1) */
    uint8_t buf[160];
    uint8_t *p = buf;
    uint32_t f4Off;
    uint32_t f12Off;
    uint8_t *f4Len;
    uint8_t *f12Len;
    FontCmapMap *map = NULL;
    FontCmapLookupResult r;

    PutU16(&p, 0);
    PutU16(&p, 2);
    PutU16(&p, 3);
    PutU16(&p, 1);
    PutU32(&p, 0);
    PutU16(&p, 3);
    PutU16(&p, 10);
    PutU32(&p, 0);

    f4Off = (uint32_t)(p - buf);
    buf[8] = (uint8_t)(f4Off >> 24);
    buf[9] = (uint8_t)(f4Off >> 16);
    buf[10] = (uint8_t)(f4Off >> 8);
    buf[11] = (uint8_t)f4Off;
    PutU16(&p, 4);
    f4Len = p;
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 4);
    PutU16(&p, 4);
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutU16(&p, 0x41);
    PutU16(&p, 0xFFFF);
    PutU16(&p, 0);
    PutU16(&p, 0x41);
    PutU16(&p, 0xFFFF);
    PutI16(&p, (int16_t)(2 - 0x41)); /* would map to 2 if selected */
    PutI16(&p, 1);
    PutU16(&p, 0);
    PutU16(&p, 0);
    {
        uint16_t len = (uint16_t)(p - (buf + f4Off));
        f4Len[0] = (uint8_t)(len >> 8);
        f4Len[1] = (uint8_t)len;
    }

    f12Off = (uint32_t)(p - buf);
    buf[16] = (uint8_t)(f12Off >> 24);
    buf[17] = (uint8_t)(f12Off >> 16);
    buf[18] = (uint8_t)(f12Off >> 8);
    buf[19] = (uint8_t)f12Off;
    PutU16(&p, 12);
    PutU16(&p, 0);
    f12Len = p;
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 1);
    PutU32(&p, 0x41u);
    PutU32(&p, 0x41u);
    PutU32(&p, 9u); /* preferred mapping */
    {
        uint32_t len = (uint32_t)(p - (buf + f12Off));
        f12Len[0] = (uint8_t)(len >> 24);
        f12Len[1] = (uint8_t)(len >> 16);
        f12Len[2] = (uint8_t)(len >> 8);
        f12Len[3] = (uint8_t)len;
    }

    RoboCheckEqInt(
        FontCmapMapCreateFromSpan((FontSpan){buf, (size_t)(p - buf)},
                                            16u, &map),
        RoboOk);
    RoboCheckEqInt(FontCmapMapSelectedFormat(map), 12);
    RoboCheckEqInt(FontCmapLookup(map, 0x41u, &r), RoboOk);
    RoboCheckEqInt((int)r.glyphId, 9);

    /* Out-of-range GID */
    FontCmapMapRelease(map);
    RoboCheckEqInt(
        FontCmapMapCreateFromSpan((FontSpan){buf, (size_t)(p - buf)},
                                            5u, &map),
        RoboOk);
    RoboCheckEqInt(FontCmapLookup(map, 0x41u, &r), RoboOk);
    RoboCheckEqInt(r.status, RoboFontCmapInvalidFont);
    FontCmapMapRelease(map);
}

static void TestCmapFaceAndFuzz(void)
{
    uint8_t maxp[8];
    uint8_t cmap[96];
    uint8_t font[256];
    uint8_t *p = cmap;
    uint8_t *lenPtr;
    size_t n;
    SynthTable tabs[2];
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontCmapLookupResult r;
    size_t maxpLen = BuildMaxpV05(maxp, 16);

    CmapBegin(&p, 3, 1);
    PutU16(&p, 4);
    lenPtr = p;
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 4);
    PutU16(&p, 4);
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutU16(&p, 0x41);
    PutU16(&p, 0xFFFF);
    PutU16(&p, 0);
    PutU16(&p, 0x41);
    PutU16(&p, 0xFFFF);
    PutI16(&p, (int16_t)(3 - 0x41));
    PutI16(&p, 1);
    PutU16(&p, 0);
    PutU16(&p, 0);
    {
        uint16_t len = (uint16_t)(p - (cmap + 12));
        lenPtr[0] = (uint8_t)(len >> 8);
        lenPtr[1] = (uint8_t)len;
    }

    tabs[0].tag = RoboFontTagMaxp;
    tabs[0].data = maxp;
    tabs[0].length = (uint32_t)maxpLen;
    tabs[1].tag = RoboFontTagCmap;
    tabs[1].data = cmap;
    tabs[1].length = (uint32_t)(p - cmap);
    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, tabs, 2u);
    RoboCheck(n > 0u);

    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseCmap(face), RoboOk);
    RoboCheck(FontFaceCmap(face) != NULL);
    RoboCheckEqInt(FontFaceLookupGlyph(face, 0x41u, &r), RoboOk);
    RoboCheckEqInt((int)r.glyphId, 3);
    {
        uint32_t gc = 0;
        RoboCheckEqInt(FontFaceGlyphCount(face, &gc), RoboOk);
        RoboCheckEqInt((int)gc, 16);
    }

    RoboCheckEqInt(FontCmapFuzzParse(font, n), RoboOk);
    RoboCheckEqInt(FontCmapFuzzParse((const uint8_t *)"xxxx", 4), RoboOk);

    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestCmapMalformed(void)
{
    uint8_t bad[] = {0, 0, 0, 1, 0, 3, 0, 1, 0, 0, 0, 100}; /* offset OOB */
    FontCmapMap *map = NULL;
    RoboCheck(FontCmapMapCreateFromSpan(
                   (FontSpan){bad, sizeof(bad)}, 16u, &map) != RoboOk);

    /* overlapping format 12 groups */
    {
        uint8_t buf[64];
        uint8_t *p = buf;
        uint8_t *lenPtr;
        CmapBegin(&p, 3, 10);
        PutU16(&p, 12);
        PutU16(&p, 0);
        lenPtr = p;
        PutU32(&p, 0);
        PutU32(&p, 0);
        PutU32(&p, 2);
        PutU32(&p, 10);
        PutU32(&p, 20);
        PutU32(&p, 1);
        PutU32(&p, 15); /* overlaps */
        PutU32(&p, 25);
        PutU32(&p, 2);
        {
            uint32_t len = (uint32_t)(p - (buf + 12));
            lenPtr[0] = (uint8_t)(len >> 24);
            lenPtr[1] = (uint8_t)(len >> 16);
            lenPtr[2] = (uint8_t)(len >> 8);
            lenPtr[3] = (uint8_t)len;
        }
        RoboCheck(FontCmapMapCreateFromSpan(
                       (FontSpan){buf, (size_t)(p - buf)}, 16u, &map) !=
                   RoboOk);
    }
}

void TestRegisterFontCmap(void)
{
    TestRegister("font.cmap.scalar", TestCmapScalar);
    TestRegister("font.cmap.format0", TestCmapFormat0);
    TestRegister("font.cmap.format2", TestCmapFormat2);
    TestRegister("font.cmap.format4", TestCmapFormat4);
    TestRegister("font.cmap.format6", TestCmapFormat6);
    TestRegister("font.cmap.format10", TestCmapFormat10);
    TestRegister("font.cmap.format12", TestCmapFormat12);
    TestRegister("font.cmap.format13", TestCmapFormat13);
    TestRegister("font.cmap.format14", TestCmapFormat14);
    TestRegister("font.cmap.selection", TestCmapSelectionAndGidValidate);
    TestRegister("font.cmap.face", TestCmapFaceAndFuzz);
    TestRegister("font.cmap.malformed", TestCmapMalformed);
}
