/*
 * RoboFont color-font suite (Goal 5.19).
 * Synthetic SFNT fixtures — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboFont/RoboFontColor.h"
#include "RoboTest.h"

#include <stdio.h>
#include <string.h>

#ifndef RoboFontNameIdFamily
#define RoboFontNameIdFamily 1u
#endif
#ifndef RoboFontNameIdSubfamily
#define RoboFontNameIdSubfamily 2u
#endif
#ifndef RoboFontNameIdFull
#define RoboFontNameIdFull 4u
#endif
#ifndef RoboFontNameIdPostscript
#define RoboFontNameIdPostscript 6u
#endif

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
static void PutI32(uint8_t **p, int32_t v) { PutU32(p, (uint32_t)v); }
static void PutI64(uint8_t **p, int64_t v)
{
    PutU32(p, (uint32_t)((uint64_t)v >> 32));
    PutU32(p, (uint32_t)((uint64_t)v));
}
static void PutOffset24(uint8_t **p, uint32_t off)
{
    PutU8(p, (uint8_t)(off >> 16));
    PutU8(p, (uint8_t)(off >> 8));
    PutU8(p, (uint8_t)off);
}
static void PutTag(uint8_t **p, uint32_t tag)
{
    PutU8(p, (uint8_t)(tag >> 24));
    PutU8(p, (uint8_t)(tag >> 16));
    PutU8(p, (uint8_t)(tag >> 8));
    PutU8(p, (uint8_t)tag);
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

static size_t BuildSfnt(uint8_t *out, size_t outCap, const SynthTable *tables,
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
    PutU32(&p, 0x00010000u);
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
        if (tables[i].length) {
            memcpy(out + cursor, tables[i].data, tables[i].length);
        }
        cursor += pad;
    }
    return cursor;
}

static size_t BuildHead(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutU32(&p, 0x00010000u);
    PutU32(&p, 0);
    PutU32(&p, RoboFontHeadMagic);
    PutU16(&p, 0);
    PutU16(&p, 1000);
    PutI64(&p, 0);
    PutI64(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, -200);
    PutI16(&p, 1000);
    PutI16(&p, 800);
    PutU16(&p, 0);
    PutU16(&p, 9);
    PutI16(&p, 2);
    PutI16(&p, 0);
    PutI16(&p, 0);
    return (size_t)(p - buf);
}

static size_t BuildHhea(uint8_t *buf, uint16_t numMetrics)
{
    uint8_t *p = buf;
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutI16(&p, 800);
    PutI16(&p, -200);
    PutI16(&p, 90);
    PutU16(&p, 1000);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutU16(&p, numMetrics);
    return (size_t)(p - buf);
}

static size_t BuildMaxp(uint8_t *buf, uint16_t glyphs)
{
    uint8_t *p = buf;
    int i;
    PutU32(&p, 0x00010000u);
    PutU16(&p, glyphs);
    for (i = 0; i < 13; i++) {
        PutU16(&p, 0);
    }
    return (size_t)(p - buf);
}

static size_t BuildHmtx(uint8_t *buf, uint16_t count)
{
    uint8_t *p = buf;
    uint16_t i;
    for (i = 0u; i < count; i++) {
        PutU16(&p, 500);
        PutI16(&p, 0);
    }
    return (size_t)(p - buf);
}

static size_t BuildLoca(uint8_t *buf, uint16_t glyphs)
{
    uint8_t *p = buf;
    uint16_t i;
    for (i = 0u; i <= glyphs; i++) {
        PutU16(&p, 0);
    }
    return (size_t)(p - buf);
}

static size_t BuildPost(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU32(&p, 0x00030000u);
    PutU32(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    return (size_t)(p - buf);
}

static size_t BuildOs2(uint8_t *buf)
{
    uint8_t *p = buf;
    int i;
    PutU16(&p, 4);
    PutI16(&p, 500);
    PutU16(&p, 400);
    PutU16(&p, 5);
    PutU16(&p, 0);
    for (i = 0; i < 10; i++) {
        PutI16(&p, 0);
    }
    PutI16(&p, 0);
    for (i = 0; i < 10; i++) {
        PutU8(&p, 0);
    }
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU8(&p, 'T');
    PutU8(&p, 'E');
    PutU8(&p, 'S');
    PutU8(&p, 'T');
    PutU16(&p, RoboFontFsRegular);
    PutU16(&p, 32);
    PutU16(&p, 126);
    PutI16(&p, 800);
    PutI16(&p, -200);
    PutI16(&p, 90);
    PutU16(&p, 800);
    PutU16(&p, 200);
    PutU32(&p, 1);
    PutU32(&p, 0);
    PutI16(&p, 500);
    PutI16(&p, 700);
    PutU16(&p, 0);
    PutU16(&p, 32);
    PutU16(&p, 0);
    return (size_t)(p - buf);
}

static void PutUtf16beAscii(uint8_t **p, const char *s)
{
    while (*s) {
        PutU16(p, (uint16_t)(uint8_t)*s);
        s++;
    }
}

static size_t BuildNameTable(uint8_t *buf, size_t cap, const char *family,
                               const char *ps)
{
    const char *strs[4];
    uint16_t ids[4];
    uint32_t count = 4u;
    uint32_t i;
    size_t header = 6u + (size_t)count * 12u;
    size_t storage = header;
    uint8_t *p = buf;

    ids[0] = RoboFontNameIdFamily;
    strs[0] = family;
    ids[1] = RoboFontNameIdSubfamily;
    strs[1] = "Regular";
    ids[2] = RoboFontNameIdFull;
    strs[2] = family;
    ids[3] = RoboFontNameIdPostscript;
    strs[3] = ps;

    if (cap < header) {
        return 0u;
    }
    PutU16(&p, 0);
    PutU16(&p, (uint16_t)count);
    PutU16(&p, (uint16_t)header);
    for (i = 0u; i < count; i++) {
        size_t slen = strlen(strs[i]) * 2u;
        uint8_t *rec = buf + 6u + (size_t)i * 12u;
        uint8_t *rp = rec;
        if (storage + slen > cap) {
            return 0u;
        }
        PutU16(&rp, 3);
        PutU16(&rp, 1);
        PutU16(&rp, 0x0409);
        PutU16(&rp, ids[i]);
        PutU16(&rp, (uint16_t)slen);
        PutU16(&rp, (uint16_t)(storage - header));
        {
            uint8_t *sp = buf + storage;
            PutUtf16beAscii(&sp, strs[i]);
        }
        storage += slen;
    }
    return storage;
}

static size_t BuildCmapFmt4Ascii(uint8_t *buf, uint16_t cp, uint16_t gid)
{
    uint8_t *p = buf;
    uint8_t *lenPtr;
    PutU16(&p, 0);
    PutU16(&p, 1);
    PutU16(&p, 3);
    PutU16(&p, 1);
    PutU32(&p, 12u);
    PutU16(&p, 4);
    lenPtr = p;
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 4);
    PutU16(&p, 4);
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutU16(&p, cp);
    PutU16(&p, 0xFFFF);
    PutU16(&p, 0);
    PutU16(&p, cp);
    PutU16(&p, 0xFFFF);
    PutI16(&p, (int16_t)((int)gid - (int)cp));
    PutI16(&p, 1);
    PutU16(&p, 0);
    PutU16(&p, 0);
    {
        uint16_t len = (uint16_t)(p - (buf + 12));
        lenPtr[0] = (uint8_t)(len >> 8);
        lenPtr[1] = (uint8_t)len;
    }
    return (size_t)(p - buf);
}

static size_t BuildCmapFmt12Range(uint8_t *buf, size_t cap, uint32_t first,
                                     uint32_t last, uint32_t gid)
{
    uint8_t *p = buf;
    uint32_t n;
    uint32_t i;
    uint32_t body;
    if (last < first) {
        return 0u;
    }
    n = last - first + 1u;
    body = 16u + n * 12u;
    if (cap < 12u + body) {
        return 0u;
    }
    PutU16(&p, 0);
    PutU16(&p, 1);
    PutU16(&p, 3);
    PutU16(&p, 10);
    PutU32(&p, 12u);
    PutU16(&p, 12);
    PutU16(&p, 0);
    PutU32(&p, body);
    PutU32(&p, 0);
    PutU32(&p, n);
    for (i = 0u; i < n; i++) {
        uint32_t cp = first + i;
        PutU32(&p, cp);
        PutU32(&p, cp);
        PutU32(&p, gid);
    }
    return (size_t)(p - buf);
}

/* CPAL: 2 palettes × 2 entries; known BGRA colors. */
static size_t BuildCpal2x2(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU16(&p, 0); /* version */
    PutU16(&p, 2); /* numPaletteEntries */
    PutU16(&p, 2); /* numPalettes */
    PutU16(&p, 4); /* numColorRecords */
    PutU32(&p, 16u); /* colorRecordsArrayOffset */
    PutU16(&p, 0); /* palette 0 first index */
    PutU16(&p, 2); /* palette 1 first index */
    /* BGRA records */
    PutU8(&p, 0);
    PutU8(&p, 0);
    PutU8(&p, 255);
    PutU8(&p, 255); /* red */
    PutU8(&p, 255);
    PutU8(&p, 0);
    PutU8(&p, 0);
    PutU8(&p, 255); /* green */
    PutU8(&p, 255);
    PutU8(&p, 0);
    PutU8(&p, 0);
    PutU8(&p, 255); /* blue */
    PutU8(&p, 0);
    PutU8(&p, 255);
    PutU8(&p, 255);
    PutU8(&p, 255); /* yellow */
    return (size_t)(p - buf);
}

/* CPAL with invalid palette index array. */
static size_t BuildCpalBad(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU16(&p, 0);
    PutU16(&p, 2);
    PutU16(&p, 2);
    PutU16(&p, 2);
    PutU32(&p, 16u);
    PutU16(&p, 0);
    PutU16(&p, 5); /* starts past numColorRecords */
    PutU8(&p, 0);
    PutU8(&p, 0);
    PutU8(&p, 255);
    PutU8(&p, 255);
    PutU8(&p, 0);
    PutU8(&p, 255);
    PutU8(&p, 0);
    PutU8(&p, 255);
    return (size_t)(p - buf);
}

/* COLR v0: gid 1 → 2 layers. */
static size_t BuildColrV0Layers(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU16(&p, 0); /* version */
    PutU16(&p, 1); /* numBaseGlyphRecords */
    PutU32(&p, 14u); /* baseGlyphRecordsOffset */
    PutU32(&p, 20u); /* layerRecordsOffset */
    PutU16(&p, 2); /* numLayerRecords */
    /* base glyph record */
    PutU16(&p, 1); /* gid */
    PutU16(&p, 0); /* firstLayerIndex */
    PutU16(&p, 2); /* numLayers */
    /* layer records */
    PutU16(&p, 2); /* layer gid */
    PutU16(&p, 0); /* palette index */
    PutU16(&p, 3); /* layer gid */
    PutU16(&p, 1); /* palette index */
    return (size_t)(p - buf);
}

/* COLR v1: gid 1 → PaintSolid (format 2). */
static size_t BuildColrV1Solid(uint8_t *buf, int recursiveColrGlyph)
{
    uint8_t *p = buf;
    const uint32_t baseListOff = 34u;
    const uint32_t paintOff = 39u;
    PutU16(&p, 1); /* version */
    PutU16(&p, 1); /* numBaseGlyphRecords */
    PutU32(&p, 14u);
    PutU32(&p, 14u);
    PutU16(&p, 0); /* numLayerRecords */
    PutU32(&p, baseListOff); /* baseGlyphListOffset */
    PutU32(&p, 0); /* layerListOffset */
    PutU32(&p, 0); /* clipListOffset */
    PutU32(&p, 0); /* varIndexMapOffset */
    PutU32(&p, 0); /* itemVariationStoreOffset */
    /* base glyph list record */
    PutU16(&p, 1); /* gid */
    PutOffset24(&p, paintOff);
    /* paint */
    if (recursiveColrGlyph) {
        PutU8(&p, 11); /* PaintColrGlyph */
        PutU16(&p, 1); /* self gid */
    } else {
        PutU8(&p, 2); /* PaintSolid */
        PutU16(&p, 0); /* paletteIndex */
        PutI16(&p, 0x4000); /* alpha = 1.0 */
    }
    return (size_t)(p - buf);
}

/* sbix: version 1, 1 strike ppem=32, numGlyphs=2, glyph 1 has png stub. */
static size_t BuildSbixPng(uint8_t *buf, uint16_t numGlyphs)
{
    uint8_t *p = buf;
    const uint32_t strikeOff = 12u;
    const uint32_t glyphArrayOff = 20u;
    const uint32_t imageOff = 32u;
    const uint32_t imageEnd = 44u;
    PutU16(&p, 1); /* version */
    PutU16(&p, 1); /* flags */
    PutU32(&p, 1u); /* numStrikes */
    PutU32(&p, strikeOff);
    /* strike */
    PutU16(&p, 32); /* ppem */
    PutU16(&p, 72); /* ppi */
    PutU32(&p, glyphArrayOff);
    /* glyph offset array (numGlyphs + 1) */
    {
        uint16_t i;
        for (i = 0u; i <= numGlyphs; i++) {
            if (i == 0u) {
                PutU32(&p, 0u);
            } else if (i == 1u) {
                PutU32(&p, imageOff - glyphArrayOff);
            } else {
                PutU32(&p, imageEnd - glyphArrayOff);
            }
        }
    }
    /* glyph 1 image */
    PutTag(&p, RoboFontTag('p', 'n', 'g', ' '));
    PutU8(&p, 0x89);
    PutU8(&p, 'P');
    PutU8(&p, 'N');
    PutU8(&p, 'G');
    PutU8(&p, 0x0D);
    PutU8(&p, 0x0A);
    PutU8(&p, 0x1A);
    PutU8(&p, 0x0A);
    return (size_t)(p - buf);
}

/* Minimal CBLC: header + one 48-byte BitmapSize + stub IndexSubtableList. */
static size_t BuildCblcMin(uint8_t *buf)
{
    uint8_t *p = buf;
    const uint32_t indexArrayOff = 56u; /* after header(8) + BitmapSize(48) */
    uint8_t i;

    PutU16(&p, 3); /* majorVersion (OpenType CBLC = 3) */
    PutU16(&p, 0); /* minor */
    PutU32(&p, 1u); /* numSizes */

    /* BitmapSize (48 bytes) */
    PutU32(&p, indexArrayOff); /* indexSubtableListOffset */
    PutU32(&p, 8u);              /* indexSubtableListSize */
    PutU32(&p, 1u);              /* numberOfIndexSubtables */
    PutU32(&p, 0u);              /* colorRef */
    for (i = 0u; i < 24u; i++) {  /* hori + vert SbitLineMetrics */
        PutU8(&p, 0);
    }
    PutU16(&p, 0); /* startGlyphIndex */
    PutU16(&p, 1); /* endGlyphIndex */
    PutU8(&p, 32); /* ppemX */
    PutU8(&p, 32); /* ppemY */
    PutU8(&p, 32); /* bitDepth (BGRA) */
    PutU8(&p, 1);  /* flags */

    /* IndexSubtableList stub: numTables=1 + one IndexSubHeader offset */
    PutU32(&p, 1u); /* numTables (some fonts use uint32; OT uses uint16 —
                      * keep 8 bytes total as declared size) */
    PutU32(&p, 0u);
    return (size_t)(p - buf);
}

static size_t BuildCbdtStub(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU8(&p, 0x01);
    PutU8(&p, 0x02);
    PutU8(&p, 0x03);
    PutU8(&p, 0x04);
    return (size_t)(p - buf);
}

static size_t BuildSvgDoc(uint8_t *buf, size_t cap, const char *svgBytes,
                            size_t svgLen, int oversizedClaim)
{
    uint8_t *p = buf;
    const uint32_t docListOff = 6u;
    /* DocumentList at 6: numEntries(2) + record(12) = 14 → payload at 20 */
    const uint32_t payloadOffFromList = 14u;
    size_t need;
    if (!svgBytes || cap < 20u + svgLen) {
        return 0u;
    }
    need = 20u + svgLen;
    PutU16(&p, 0); /* version */
    PutU32(&p, docListOff);
    PutU16(&p, 1); /* numEntries */
    PutU16(&p, 1); /* startGlyphID */
    PutU16(&p, 1); /* endGlyphID */
    PutU32(&p, payloadOffFromList); /* offset from DocumentList */
    if (oversizedClaim) {
        PutU32(&p, (uint32_t)(RoboFontColorMaxSvgPayload + 1u));
        return 20u; /* truncated payload */
    }
    PutU32(&p, (uint32_t)svgLen);
    memcpy(buf + 20u, svgBytes, svgLen);
    return need;
}

static size_t BuildFvarStub(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutU16(&p, 16);
    PutU16(&p, 0);
    PutU16(&p, 1); /* axisCount */
    PutU16(&p, 20);
    PutU16(&p, 0);
    PutU16(&p, 4);
    PutU32(&p, RoboFontTagWght);
    PutI32(&p, 6553600); /* 100 */
    PutI32(&p, 26214400); /* 400 */
    PutI32(&p, 58982400); /* 900 */
    PutU16(&p, 0);
    PutU16(&p, 256);
    return (size_t)(p - buf);
}

enum {
    COLOR_FONT_NONE = 0,
    COLOR_FONT_CPAL = 1u << 0,
    COLOR_FONT_COLR_V0 = 1u << 1,
    COLOR_FONT_COLR_V1 = 1u << 2,
    COLOR_FONT_COLR_V1_RECURSE = 1u << 3,
    COLOR_FONT_SBIX = 1u << 4,
    COLOR_FONT_CBLC = 1u << 5,
    COLOR_FONT_CBDT = 1u << 6,
    COLOR_FONT_SVG = 1u << 7,
    COLOR_FONT_EMOJI_CMAP = 1u << 8,
    COLOR_FONT_FVAR = 1u << 9,
    COLOR_FONT_SVG_OVERSIZED = 1u << 10,
    COLOR_FONT_BAD_CPAL = 1u << 11
};

typedef struct {
    const char *family;
    const char *ps;
    uint16_t numGlyphs;
    uint32_t flags;
    uint32_t emojiFirst;
    uint32_t emojiLast;
} ColorFontSpec;

static size_t BuildColorFont(uint8_t *out, size_t cap, const ColorFontSpec *spec)
{
    uint8_t head[64];
    uint8_t hhea[64];
    uint8_t maxp[64];
    uint8_t hmtx[256];
    uint8_t loca[256];
    uint8_t post[64];
    uint8_t os2[128];
    uint8_t name[512];
    uint8_t cmap[1024];
    uint8_t cpal[64];
    uint8_t colr[128];
    uint8_t sbix[128];
    uint8_t cblc[128];
    uint8_t cbdt[32];
    uint8_t svg[512];
    uint8_t fvar[128];
    SynthTable tabs[24];
    uint32_t nt = 0u;
    size_t nn;
    size_t nc = 0u;
    uint16_t glyphs;
    static const char kSvg[] = "<svg xmlns=\"http://www.w3.org/2000/svg\"/>";

    if (!spec || spec->numGlyphs < 2u) {
        return 0u;
    }
    glyphs = spec->numGlyphs;
    nn = BuildNameTable(name, sizeof(name), spec->family, spec->ps);
    if (nn == 0u) {
        return 0u;
    }
    if (spec->flags & COLOR_FONT_EMOJI_CMAP) {
        nc = BuildCmapFmt12Range(cmap, sizeof(cmap), spec->emojiFirst,
                                    spec->emojiLast, 1u);
    } else {
        nc = BuildCmapFmt4Ascii(cmap, 0x41u, 1u);
    }
    if (nc == 0u) {
        return 0u;
    }

    tabs[nt++] = (SynthTable){RoboFontTagHead, head, (uint32_t)BuildHead(head)};
    tabs[nt++] =
        (SynthTable){RoboFontTagHhea, hhea, (uint32_t)BuildHhea(hhea, glyphs)};
    tabs[nt++] =
        (SynthTable){RoboFontTagMaxp, maxp, (uint32_t)BuildMaxp(maxp, glyphs)};
    tabs[nt++] =
        (SynthTable){RoboFontTagHmtx, hmtx, (uint32_t)BuildHmtx(hmtx, glyphs)};
    tabs[nt++] =
        (SynthTable){RoboFontTagLoca, loca, (uint32_t)BuildLoca(loca, glyphs)};
    tabs[nt++] = (SynthTable){RoboFontTagGlyf, head, 0u};
    tabs[nt++] = (SynthTable){RoboFontTagPost, post, (uint32_t)BuildPost(post)};
    tabs[nt++] = (SynthTable){RoboFontTagOs2, os2, (uint32_t)BuildOs2(os2)};
    tabs[nt++] = (SynthTable){RoboFontTagName, name, (uint32_t)nn};
    tabs[nt++] = (SynthTable){RoboFontTagCmap, cmap, (uint32_t)nc};

    if (spec->flags & (COLOR_FONT_CPAL | COLOR_FONT_BAD_CPAL)) {
        size_t cpalLen = (spec->flags & COLOR_FONT_BAD_CPAL)
                              ? BuildCpalBad(cpal)
                              : BuildCpal2x2(cpal);
        tabs[nt++] = (SynthTable){RoboFontTagCpal, cpal, (uint32_t)cpalLen};
    }
    if (spec->flags & COLOR_FONT_COLR_V0) {
        tabs[nt++] = (SynthTable){RoboFontTagColr, colr,
                                      (uint32_t)BuildColrV0Layers(colr)};
    }
    if (spec->flags & (COLOR_FONT_COLR_V1 | COLOR_FONT_COLR_V1_RECURSE)) {
        tabs[nt++] = (SynthTable){RoboFontTagColr, colr,
                                      (uint32_t)BuildColrV1Solid(
                                          colr, (spec->flags & COLOR_FONT_COLR_V1_RECURSE) ? 1 : 0)};
    }
    if (spec->flags & COLOR_FONT_SBIX) {
        tabs[nt++] = (SynthTable){RoboFontTagSbix, sbix,
                                      (uint32_t)BuildSbixPng(sbix, glyphs)};
    }
    if (spec->flags & COLOR_FONT_CBLC) {
        tabs[nt++] = (SynthTable){RoboFontTagCblc, cblc,
                                      (uint32_t)BuildCblcMin(cblc)};
    }
    if (spec->flags & COLOR_FONT_CBDT) {
        tabs[nt++] = (SynthTable){RoboFontTagCbdt, cbdt,
                                      (uint32_t)BuildCbdtStub(cbdt)};
    }
    if (spec->flags & COLOR_FONT_SVG) {
        size_t sn = BuildSvgDoc(svg, sizeof(svg), kSvg, sizeof(kSvg) - 1u,
                                  (spec->flags & COLOR_FONT_SVG_OVERSIZED) ? 1 : 0);
        if (sn == 0u) {
            return 0u;
        }
        tabs[nt++] = (SynthTable){RoboFontTagSvg, svg, (uint32_t)sn};
    }
    if (spec->flags & COLOR_FONT_FVAR) {
        tabs[nt++] = (SynthTable){RoboFontTagFvar, fvar,
                                      (uint32_t)BuildFvarStub(fvar)};
    }
    return BuildSfnt(out, cap, tabs, nt);
}

static void LoadFace(const uint8_t *font, size_t n, FontSource **src,
                      FontFace **face)
{
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(*src, 0u, face), RoboOk);
}

static void TestMono(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontSfnt *sfnt = NULL;
    FontColorInfo *info = NULL;
    ColorFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "MonoSans";
    spec.ps = "MonoSans";
    spec.numGlyphs = 2u;
    spec.flags = COLOR_FONT_NONE;
    n = BuildColorFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);
    LoadFace(font, n, &src, &face);

    RoboCheckEqInt(FontFaceParseColor(face), RoboNotFound);
    RoboCheck(FontFaceColor(face) == NULL);

    RoboCheckEqInt(FontFaceParseSfnt(face), RoboOk);
    sfnt = (FontSfnt *)FontFaceSfnt(face);
    RoboCheck(sfnt != NULL);
    RoboCheckEqInt(FontColorInfoCreate(sfnt, NULL, &info), RoboNotFound);
    RoboCheck(info == NULL);

    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestColrCpal(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontColorInfo *info = NULL;
    FontColorPalette pal;
    FontColorRgba rgba;
    FontColorGlyph glyph;
    const FontColorGlyphLayer *layers = NULL;
    uint32_t layerCount = 0u;
    FontColorPaletteSelection sel;
    ColorFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "ColrCpal";
    spec.ps = "ColrCpal";
    spec.numGlyphs = 4u;
    spec.flags = COLOR_FONT_CPAL | COLOR_FONT_COLR_V0;
    n = BuildColorFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);
    LoadFace(font, n, &src, &face);
    RoboCheckEqInt(FontColorInfoCreateFromFace(face, NULL, &info), RoboOk);
    RoboCheck(info != NULL);

    RoboCheck((FontColorInfoCapabilities(info) & RoboFontColorCapColr) != 0u);
    RoboCheck((FontColorInfoCapabilities(info) & RoboFontColorCapCpal) != 0u);
    RoboCheck((FontColorInfoCapabilities(info) & RoboFontColorCapColrV0) != 0u);
    RoboCheck(FontColorInfoIsColorCapable(info));
    RoboCheckEqInt((int)FontColorInfoColrVersion(info), 0);
    RoboCheckEqInt((int)FontColorInfoPaletteCount(info), 2);

    RoboCheckEqInt(FontColorInfoPalette(info, 0u, &pal), RoboOk);
    RoboCheckEqInt((int)pal.entryCount, 2);
    RoboCheckEqInt(FontColorInfoPaletteEntry(info, 0u, 0u, &rgba), RoboOk);
    RoboCheckEqInt((int)rgba.r, 255);
    RoboCheckEqInt((int)rgba.g, 0);
    RoboCheckEqInt((int)rgba.b, 0);
    RoboCheckEqInt((int)rgba.a, 255);
    RoboCheckEqInt(FontColorInfoPaletteEntry(info, 1u, 1u, &rgba), RoboOk);
    RoboCheckEqInt((int)rgba.r, 255);
    RoboCheckEqInt((int)rgba.g, 255);
    RoboCheckEqInt((int)rgba.b, 0);

    RoboCheckEqInt(FontColorInfoGlyph(info, 1u, &glyph), RoboOk);
    RoboCheckEqInt((int)glyph.format, (int)RoboFontColorGlyphLayers);
    RoboCheckEqInt((int)glyph.layerCount, 2);
    RoboCheckEqInt(FontColorInfoGlyphLayers(info, 1u, &layers, &layerCount),
                      RoboOk);
    RoboCheck(layers != NULL);
    RoboCheckEqInt((int)layerCount, 2);
    RoboCheckEqInt((int)layers[0].glyphId, 2);
    RoboCheckEqInt((int)layers[1].glyphId, 3);

    FontColorPaletteSelectionInit(&sel);
    sel.paletteIndex = 0u;
    RoboCheckEqInt(FontColorResolvePaletteColor(info, &sel, 1u, &rgba), RoboOk);
    RoboCheckEqInt((int)rgba.g, 0);
    RoboCheckEqInt(
        FontColorResolvePaletteColor(info, &sel, RoboFontColorForegroundPalette,
                                              &rgba),
        RoboNotFound);
    RoboCheckEqInt(FontColorInfoPalette(info, 99u, &pal), RoboNotFound);
    RoboCheckEqInt(FontColorInfoPaletteEntry(info, 0u, 99u, &rgba), RoboNotFound);

    FontColorInfoRelease(info);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestColrV1Paint(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontColorInfo *info = NULL;
    FontColorGlyph glyph;
    const FontColorPaint *nodes = NULL;
    uint32_t nodeCount = 0u;
    uint32_t root = 0u;
    ColorFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "ColrV1";
    spec.ps = "ColrV1";
    spec.numGlyphs = 4u;
    spec.flags = COLOR_FONT_CPAL | COLOR_FONT_COLR_V1;
    n = BuildColorFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);
    LoadFace(font, n, &src, &face);
    RoboCheckEqInt(FontColorInfoCreateFromFace(face, NULL, &info), RoboOk);

    RoboCheck((FontColorInfoCapabilities(info) & RoboFontColorCapColrV1) != 0u);
    RoboCheckEqInt((int)FontColorInfoColrVersion(info), 1);
    RoboCheckEqInt(FontColorInfoGlyph(info, 1u, &glyph), RoboOk);
    RoboCheckEqInt((int)glyph.format, (int)RoboFontColorGlyphPaint);

    RoboCheckEqInt(FontColorInfoGlyphPaint(info, 1u, &nodes, &nodeCount, &root),
                      RoboOk);
    RoboCheck(nodes != NULL);
    RoboCheck(nodeCount >= 1u);
    RoboCheck(root < nodeCount);
    RoboCheckEqInt((int)nodes[root].op, (int)RoboFontColorPaintSolid);
    RoboCheckEqInt((int)nodes[root].format, 2);

    FontColorInfoRelease(info);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestSbix(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontColorInfo *info = NULL;
    FontColorGlyph glyph;
    FontColorGlyphImage img;
    ColorFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "SbixFont";
    spec.ps = "SbixFont";
    spec.numGlyphs = 2u;
    spec.flags = COLOR_FONT_SBIX;
    n = BuildColorFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);
    LoadFace(font, n, &src, &face);
    RoboCheckEqInt(FontColorInfoCreateFromFace(face, NULL, &info), RoboOk);

    RoboCheck((FontColorInfoCapabilities(info) & RoboFontColorCapSbix) != 0u);
    RoboCheckEqInt((int)FontColorInfoSbixStrikeCount(info), 1);
    RoboCheckEqInt(FontColorInfoGlyphImage(info, 1u, 32u, &img), RoboOk);
    RoboCheckEqInt((int)img.ppemX, 32);
    RoboCheckEqInt((int)img.graphicType, (int)RoboFontTag('p', 'n', 'g', ' '));
    RoboCheck(img.data.size > 0u);

    RoboCheckEqInt(FontColorInfoGlyph(info, 1u, &glyph), RoboOk);
    RoboCheckEqInt((int)glyph.format, (int)RoboFontColorGlyphBitmap);
    RoboCheckEqInt((int)glyph.hasImage, 1);

    FontColorInfoRelease(info);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestCblcCbdt(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontColorInfo *info = NULL;
    FontColorGlyphImage img;
    ColorFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "CbdtFont";
    spec.ps = "CbdtFont";
    spec.numGlyphs = 2u;
    spec.flags = COLOR_FONT_CBLC | COLOR_FONT_CBDT;
    n = BuildColorFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);
    LoadFace(font, n, &src, &face);
    RoboCheckEqInt(FontColorInfoCreateFromFace(face, NULL, &info), RoboOk);

    RoboCheck((FontColorInfoCapabilities(info) & RoboFontColorCapCblc) != 0u);
    RoboCheck((FontColorInfoCapabilities(info) & RoboFontColorCapCbdt) != 0u);
    RoboCheckEqInt((int)FontColorInfoCblcStrikeCount(info), 1);
    /* Image lookup may be limited; must not crash. */
    (void)FontColorInfoGlyphImage(info, 1u, 32u, &img);

    /* Malformed CBLC: corrupt last byte; must not crash. */
    {
        uint8_t bad[4096];
        size_t bn;
        FontSource *bsrc = NULL;
        FontSfnt *sfnt = NULL;
        FontColorInfo *binfo = NULL;
        memset(&spec, 0, sizeof(spec));
        spec.family = "BadCblc";
        spec.ps = "BadCblc";
        spec.numGlyphs = 2u;
        spec.flags = COLOR_FONT_CBLC | COLOR_FONT_CBDT;
        bn = BuildColorFont(bad, sizeof(bad), &spec);
        RoboCheck(bn > 0u);
        bad[bn - 1u] = 0xFFu;
        RoboCheckEqInt(FontSourceCreateMemoryCopy(bad, bn, &bsrc), RoboOk);
        RoboCheckEqInt(FontSfntParse(bsrc, 0u, 0u, &sfnt), RoboOk);
        (void)FontColorInfoCreate(sfnt, NULL, &binfo);
        if (binfo) {
            FontColorInfoRelease(binfo);
        }
        FontSfntRelease(sfnt);
        FontSourceRelease(bsrc);
    }

    FontColorInfoRelease(info);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestSvg(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontColorInfo *info = NULL;
    FontColorGlyph glyph;
    FontColorGlyphSvg svg;
    ColorFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "SvgFont";
    spec.ps = "SvgFont";
    spec.numGlyphs = 2u;
    spec.flags = COLOR_FONT_SVG;
    n = BuildColorFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);
    LoadFace(font, n, &src, &face);
    RoboCheckEqInt(FontColorInfoCreateFromFace(face, NULL, &info), RoboOk);

    RoboCheck((FontColorInfoCapabilities(info) & RoboFontColorCapSvg) != 0u);
    RoboCheckEqInt((int)FontColorInfoSvgDocumentCount(info), 1);
    RoboCheckEqInt(FontColorInfoGlyphSvg(info, 1u, &svg), RoboOk);
    RoboCheckEqInt((int)svg.startGlyphId, 1);
    RoboCheckEqInt((int)svg.endGlyphId, 1);
    RoboCheck(svg.document.size > 0u);
    RoboCheckEqInt((int)svg.hasExternalRefHint, 1);

    RoboCheckEqInt(FontColorInfoGlyph(info, 1u, &glyph), RoboOk);
    RoboCheckEqInt((int)glyph.format, (int)RoboFontColorGlyphSvg);

    /* Oversized / truncated SVG payload handled without crash. */
    {
        uint8_t big[4096];
        size_t bn;
        FontSource *bsrc = NULL;
        FontFace *bface = NULL;
        FontColorInfo *binfo = NULL;
        memset(&spec, 0, sizeof(spec));
        spec.family = "SvgBig";
        spec.ps = "SvgBig";
        spec.numGlyphs = 2u;
        spec.flags = COLOR_FONT_SVG | COLOR_FONT_SVG_OVERSIZED;
        bn = BuildColorFont(big, sizeof(big), &spec);
        RoboCheck(bn > 0u);
        LoadFace(big, bn, &bsrc, &bface);
        (void)FontColorInfoCreateFromFace(bface, NULL, &binfo);
        if (binfo) {
            (void)FontColorInfoGlyphSvg(binfo, 1u, &svg);
            FontColorInfoRelease(binfo);
        }
        FontFaceRelease(bface);
        FontSourceRelease(bsrc);
    }

    /* Malformed SVG header. */
    {
        uint8_t junk[256];
        FontSource *jsrc = NULL;
        FontSfnt *sfnt = NULL;
        FontColorInfo *badInfo = NULL;
        SynthTable tabs[2];
        uint8_t svgBad[8];
        uint8_t *sp = svgBad;
        size_t jn;
        PutU16(&sp, 0);
        PutU32(&sp, 9999u);
        tabs[0] = (SynthTable){RoboFontTagHead, junk, 0u};
        tabs[1] = (SynthTable){RoboFontTagSvg, svgBad, 6u};
        jn = BuildSfnt(junk, sizeof(junk), tabs, 2u);
        RoboCheck(jn > 0u);
        RoboCheckEqInt(FontSourceCreateMemoryCopy(junk, jn, &jsrc), RoboOk);
        RoboCheckEqInt(FontSfntParse(jsrc, 0u, 0u, &sfnt), RoboOk);
        (void)FontColorInfoCreate(sfnt, NULL, &badInfo);
        if (badInfo) {
            FontColorInfoRelease(badInfo);
        }
        FontSfntRelease(sfnt);
        FontSourceRelease(jsrc);
    }

    FontColorInfoRelease(info);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestMulti(void)
{
    uint8_t font[8192];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontColorInfo *info = NULL;
    uint32_t caps;
    ColorFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "MultiColor";
    spec.ps = "MultiColor";
    spec.numGlyphs = 4u;
    spec.flags = COLOR_FONT_CPAL | COLOR_FONT_COLR_V0 | COLOR_FONT_SBIX;
    n = BuildColorFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);
    LoadFace(font, n, &src, &face);
    RoboCheckEqInt(FontFaceParseColor(face), RoboOk);
    RoboCheck(FontFaceColor(face) != NULL);

    info = (FontColorInfo *)FontFaceColor(face);
    caps = FontColorInfoCapabilities(info);
    RoboCheck((caps & RoboFontColorCapColr) != 0u);
    RoboCheck((caps & RoboFontColorCapCpal) != 0u);
    RoboCheck((caps & RoboFontColorCapSbix) != 0u);
    RoboCheck(FontColorInfoIsColorCapable(info));

    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestEmoji(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontEmojiInfo *emoji = NULL;
    FontColorInfo *color = NULL;
    ColorFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "ColorEmoji";
    spec.ps = "ColorEmoji";
    spec.numGlyphs = 4u;
    spec.flags = COLOR_FONT_CPAL | COLOR_FONT_COLR_V0 | COLOR_FONT_EMOJI_CMAP;
    spec.emojiFirst = 0x1F600u;
    spec.emojiLast = 0x1F603u;
    n = BuildColorFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);
    LoadFace(font, n, &src, &face);

    RoboCheckEqInt(FontEmojiInfoCreateFromFace(face, NULL, &emoji), RoboOk);
    RoboCheck(FontEmojiInfoSupportsColorEmoji(emoji));
    RoboCheckEqInt((int)FontEmojiInfoKind(emoji),
                      (int)RoboFontEmojiKindColor);

    RoboCheckEqInt(FontFaceParseColor(face), RoboOk);
    RoboCheck(FontFaceColor(face) != NULL);
    color = (FontColorInfo *)FontFaceColor(face);
    RoboCheck(FontColorInfoIsColorCapable(color));

    FontEmojiInfoRelease(emoji);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestVariation(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontColorInfo *info = NULL;
    const FontVariationCoords *coords;
    ColorFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "VarColor";
    spec.ps = "VarColor";
    spec.numGlyphs = 4u;
    spec.flags = COLOR_FONT_CPAL | COLOR_FONT_COLR_V0 | COLOR_FONT_FVAR;
    n = BuildColorFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);
    LoadFace(font, n, &src, &face);

    RoboCheckEqInt(FontColorInfoCreateFromFace(face, NULL, &info), RoboOk);
    coords = FontColorInfoVariationCoords(info);
    RoboCheck(coords == NULL);
    FontColorInfoRelease(info);
    info = NULL;

    RoboCheckEqInt(FontFaceParseColor(face), RoboOk);
    RoboCheck(FontFaceColor(face) != NULL);
    RoboCheck(FontColorInfoVariationCoords(FontFaceColor(face)) == NULL);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestMalformed(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    FontColorInfo *info = NULL;
    const FontColorPaint *nodes = NULL;
    uint32_t nodeCount = 0u;
    uint32_t root = 0u;
    ColorFontSpec spec;

    /* Truncated COLR. */
    memset(&spec, 0, sizeof(spec));
    spec.family = "TruncColr";
    spec.ps = "TruncColr";
    spec.numGlyphs = 4u;
    spec.flags = COLOR_FONT_CPAL | COLOR_FONT_COLR_V0;
    n = BuildColorFont(font, sizeof(font), &spec);
    RoboCheck(n > 4u);
    font[n - 4u] = 0u;
    font[n - 3u] = 0u;
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontSfntParse(src, 0u, 0u, &sfnt), RoboOk);
    (void)FontColorInfoCreate(sfnt, NULL, &info);
    if (info) {
        FontColorInfoRelease(info);
        info = NULL;
    }
    FontSfntRelease(sfnt);
    FontSourceRelease(src);
    src = NULL;
    sfnt = NULL;

    /* Bad CPAL palette indices (second palette starts past color records). */
    memset(&spec, 0, sizeof(spec));
    spec.family = "BadCpal";
    spec.ps = "BadCpal";
    spec.numGlyphs = 2u;
    spec.flags = COLOR_FONT_BAD_CPAL | COLOR_FONT_COLR_V0;
    n = BuildColorFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontSfntParse(src, 0u, 0u, &sfnt), RoboOk);
    (void)FontColorInfoCreate(sfnt, NULL, &info);
    if (info) {
        FontColorInfoRelease(info);
        info = NULL;
    }
    FontSfntRelease(sfnt);
    FontSourceRelease(src);
    src = NULL;
    sfnt = NULL;

    /* Recursive COLR v1 paint — no crash. */
    memset(&spec, 0, sizeof(spec));
    spec.family = "RecurPaint";
    spec.ps = "RecurPaint";
    spec.numGlyphs = 4u;
    spec.flags = COLOR_FONT_CPAL | COLOR_FONT_COLR_V1 | COLOR_FONT_COLR_V1_RECURSE;
    n = BuildColorFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0u, NULL), RoboInvalidArgument);
    {
        FontFace *face = NULL;
        RoboCheckEqInt(FontFaceCreate(src, 0u, &face), RoboOk);
        RoboCheckEqInt(FontColorInfoCreateFromFace(face, NULL, &info), RoboOk);
        (void)FontColorInfoGlyphPaint(info, 1u, &nodes, &nodeCount, &root);
        FontColorInfoRelease(info);
        info = NULL;
        FontFaceRelease(face);
    }
    FontSourceRelease(src);
}

static void TestFuzz(void)
{
    uint8_t junk[128];
    uint8_t font[8192];
    size_t n;
    uint32_t i;
    ColorFontSpec spec;

    for (i = 0u; i < sizeof(junk); i++) {
        junk[i] = (uint8_t)(i * 37u + 11u);
    }
    RoboCheckEqInt(FontColorFuzzParse(NULL, 0u), RoboOk);
    RoboCheckEqInt(FontColorFuzzParse(junk, sizeof(junk)), RoboOk);

    memset(&spec, 0, sizeof(spec));
    spec.family = "FuzzColor";
    spec.ps = "FuzzColor";
    spec.numGlyphs = 4u;
    spec.flags = COLOR_FONT_CPAL | COLOR_FONT_COLR_V0 | COLOR_FONT_SBIX;
    n = BuildColorFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontColorFuzzParse(font, n), RoboOk);

    /* Random-ish buffer: perturb valid font. */
    for (i = 0u; i < 64u; i++) {
        font[i % n] ^= (uint8_t)(i * 13u + 7u);
        (void)FontColorFuzzParse(font, n);
    }
}

static void TestBench(void)
{
    uint8_t font[8192];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontColorInfo *info = NULL;
    FontColorRgba rgba;
    FontColorGlyph glyph;
    const FontColorPaint *nodes = NULL;
    uint32_t nodeCount = 0u;
    uint32_t root = 0u;
    FontColorPaletteSelection sel;
    int hits = 0;
    int i;
    ColorFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "BenchColor";
    spec.ps = "BenchColor";
    spec.numGlyphs = 4u;
    spec.flags = COLOR_FONT_CPAL | COLOR_FONT_COLR_V0 | COLOR_FONT_SBIX;
    n = BuildColorFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);
    LoadFace(font, n, &src, &face);
    RoboCheckEqInt(FontColorInfoCreateFromFace(face, NULL, &info), RoboOk);
    FontColorPaletteSelectionInit(&sel);

    for (i = 0; i < 200; i++) {
        if (FontColorInfoPaletteCount(info) > 0u) {
            hits++;
        }
        sel.paletteIndex = (uint16_t)(i % 2u);
        if (FontColorInfoPaletteEntry(info, sel.paletteIndex, 0u, &rgba) == RoboOk) {
            hits++;
        }
        if (FontColorResolvePaletteColor(info, &sel, 0u, &rgba) == RoboOk) {
            hits++;
        }
        if (FontColorInfoGlyph(info, 1u, &glyph) == RoboOk) {
            hits++;
        }
        if (FontColorInfoGlyphLayers(info, 1u, NULL, NULL) == RoboOk) {
            hits++;
        }
        if (FontColorInfoGlyphPaint(info, 1u, &nodes, &nodeCount, &root) == RoboOk) {
            hits++;
        }
    }
    printf("font.color.bench hits=%d\n", hits);
    RoboCheck(hits >= 200);

    FontColorInfoRelease(info);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

void TestRegisterFontColor(void)
{
    TestRegister("font.color.mono", TestMono);
    TestRegister("font.color.colr_cpal", TestColrCpal);
    TestRegister("font.color.colr_v1_paint", TestColrV1Paint);
    TestRegister("font.color.sbix", TestSbix);
    TestRegister("font.color.cblc_cbdt", TestCblcCbdt);
    TestRegister("font.color.svg", TestSvg);
    TestRegister("font.color.multi", TestMulti);
    TestRegister("font.color.emoji", TestEmoji);
    TestRegister("font.color.variation", TestVariation);
    TestRegister("font.color.malformed", TestMalformed);
    TestRegister("font.color.fuzz", TestFuzz);
    TestRegister("font.color.bench", TestBench);
}
