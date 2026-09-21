/*
 * RoboFont outline suite (Goal 5.8).
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
static void PutI64(uint8_t **p, int64_t v)
{
    int i;
    for (i = 7; i >= 0; i--) {
        PutU8(p, (uint8_t)((uint64_t)v >> (i * 8)));
    }
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

static size_t BuildSfnt(uint8_t *out, size_t outCap, uint32_t version,
                         const SynthTable *tables, uint32_t n)
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

static size_t BuildHead(uint8_t *b, uint16_t upem)
{
    uint8_t *p = b;
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutU32(&p, 0x00010000u);
    PutU32(&p, 0u);
    PutU32(&p, RoboFontHeadMagic);
    PutU16(&p, 0u);
    PutU16(&p, upem);
    PutI64(&p, 0);
    PutI64(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 100);
    PutI16(&p, 100);
    PutU16(&p, 0u);
    PutU16(&p, 8u);
    PutI16(&p, 2);
    PutI16(&p, 1);
    PutI16(&p, 0);
    return (size_t)(p - b);
}

static size_t BuildHhea(uint8_t *b, uint16_t nH)
{
    uint8_t *p = b;
    int i;
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutI16(&p, 800);
    PutI16(&p, -200);
    PutI16(&p, 0);
    PutU16(&p, 1000);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 100);
    PutI16(&p, 1);
    PutI16(&p, 0);
    PutI16(&p, 0);
    for (i = 0; i < 4; i++) {
        PutI16(&p, 0);
    }
    PutI16(&p, 0);
    PutU16(&p, nH);
    return (size_t)(p - b);
}

static size_t BuildMaxp(uint8_t *b, uint16_t ng)
{
    uint8_t *p = b;
    PutU32(&p, 0x00010000u);
    PutU16(&p, ng);
    PutU16(&p, 8);
    PutU16(&p, 2);
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 2);
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 4);
    PutU16(&p, 4);
    return (size_t)(p - b);
}

static size_t BuildHmtx(uint8_t *b, uint16_t n)
{
    uint8_t *p = b;
    uint16_t i;
    for (i = 0u; i < n; i++) {
        PutU16(&p, 200);
        PutI16(&p, 0);
    }
    return (size_t)(p - b);
}

/* One-contour square: on-curve corners only → lines */
static size_t BuildSquareGlyf(uint8_t *b)
{
    uint8_t *p = b;
    PutI16(&p, 1);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 100);
    PutI16(&p, 100);
    PutU16(&p, 3);
    PutU16(&p, 0);
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXSameOrPositive |
                          RoboFontGlyfFlagYSameOrPositive));
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXShort |
                          RoboFontGlyfFlagXSameOrPositive |
                          RoboFontGlyfFlagYSameOrPositive));
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagYShort |
                          RoboFontGlyfFlagXSameOrPositive |
                          RoboFontGlyfFlagYSameOrPositive));
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXShort |
                          RoboFontGlyfFlagYSameOrPositive));
    PutU8(&p, 100);
    PutU8(&p, 100);
    PutU8(&p, 100);
    return (size_t)(p - b);
}

/* Contour with consecutive off-curve points → implied midpoints / quads */
static size_t BuildOffCurveGlyf(uint8_t *b)
{
    uint8_t *p = b;
    /* 3 points: on, off, off — with wrap will insert mids */
    PutI16(&p, 1);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 100);
    PutI16(&p, 100);
    PutU16(&p, 2); /* endPts last index 2 */
    PutU16(&p, 0);
    /* p0 on (0,0), p1 off (50,100), p2 off (100,0) — absolute via deltas */
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXSameOrPositive |
                          RoboFontGlyfFlagYSameOrPositive));
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagXShort | RoboFontGlyfFlagXSameOrPositive |
                          RoboFontGlyfFlagYShort | RoboFontGlyfFlagYSameOrPositive));
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagXShort | RoboFontGlyfFlagXSameOrPositive |
                          RoboFontGlyfFlagYShort)); /* y negative */
    PutU8(&p, 50);  /* x +50 */
    PutU8(&p, 50);  /* x +50 → 100 */
    PutU8(&p, 100); /* y +100 */
    PutU8(&p, 100); /* y -100 → 0 */
    return (size_t)(p - b);
}

static void TestOutlineTtSimple(void)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[16], loca[16], glyf[64];
    uint8_t font[384];
    SynthTable tabs[6];
    size_t gn = BuildSquareGlyf(glyf);
    uint8_t *lp = loca;
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontOutline *o = NULL;
    char dump[1024];
    FontBbox bb;

    PutU32(&lp, 0);
    PutU32(&lp, (uint32_t)gn);
    PutU32(&lp, (uint32_t)gn); /* glyph1 empty */

    tabs[0].tag = RoboFontTagHead;
    tabs[0].data = head;
    tabs[0].length = (uint32_t)BuildHead(head, 1000);
    tabs[1].tag = RoboFontTagHhea;
    tabs[1].data = hhea;
    tabs[1].length = (uint32_t)BuildHhea(hhea, 2);
    tabs[2].tag = RoboFontTagMaxp;
    tabs[2].data = maxp;
    tabs[2].length = (uint32_t)BuildMaxp(maxp, 2);
    tabs[3].tag = RoboFontTagHmtx;
    tabs[3].data = hmtx;
    tabs[3].length = (uint32_t)BuildHmtx(hmtx, 2);
    tabs[4].tag = RoboFontTagLoca;
    tabs[4].data = loca;
    tabs[4].length = 12u;
    tabs[5].tag = RoboFontTagGlyf;
    tabs[5].data = glyf;
    tabs[5].length = (uint32_t)gn;

    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, tabs, 6);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseOutlines(face), RoboOk);
    RoboCheckEqInt(FontFaceGlyphOutline(face, 0, &o), RoboOk);
    RoboCheckEqInt((int)MakeFontOutlineStatus(o), (int)RoboFontOutlineStatusOk);
    RoboCheck(FontOutlineContourCount(o) >= 1u);
    RoboCheck(FontOutlineHasBounds(o));
    RoboCheckEqInt(FontOutlineBounds(o, &bb), RoboOk);
    RoboCheckEqInt((int)bb.xMin, 0);
    RoboCheckEqInt((int)bb.xMax, 100);
    RoboCheckEqInt(FontOutlineDump(o, dump, sizeof(dump), NULL), RoboOk);
    RoboCheck(strstr(dump, "MOVE") != NULL);
    RoboCheck(strstr(dump, "CLOSE") != NULL);
    FontOutlineRelease(o);

    RoboCheckEqInt(FontFaceGlyphOutline(face, 1, &o), RoboOk);
    RoboCheckEqInt((int)MakeFontOutlineStatus(o), (int)RoboFontOutlineStatusEmpty);
    FontOutlineRelease(o);

    RoboCheckEqInt(FontOutlineFuzzParse(font, n), RoboOk);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestOutlineTtImplied(void)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[8], loca[12], glyf[64];
    uint8_t font[384];
    SynthTable tabs[6];
    size_t gn = BuildOffCurveGlyf(glyf);
    uint8_t *lp = loca;
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontOutline *o = NULL;
    char dump[2048];

    PutU32(&lp, 0);
    PutU32(&lp, (uint32_t)gn);

    tabs[0].tag = RoboFontTagHead;
    tabs[0].data = head;
    tabs[0].length = (uint32_t)BuildHead(head, 1000);
    tabs[1].tag = RoboFontTagHhea;
    tabs[1].data = hhea;
    tabs[1].length = (uint32_t)BuildHhea(hhea, 1);
    tabs[2].tag = RoboFontTagMaxp;
    tabs[2].data = maxp;
    tabs[2].length = (uint32_t)BuildMaxp(maxp, 1);
    tabs[3].tag = RoboFontTagHmtx;
    tabs[3].data = hmtx;
    tabs[3].length = (uint32_t)BuildHmtx(hmtx, 1);
    tabs[4].tag = RoboFontTagLoca;
    tabs[4].data = loca;
    tabs[4].length = 8u;
    tabs[5].tag = RoboFontTagGlyf;
    tabs[5].data = glyf;
    tabs[5].length = (uint32_t)gn;

    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, tabs, 6);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseOutlines(face), RoboOk);
    RoboCheckEqInt(FontFaceGlyphOutline(face, 0, &o), RoboOk);
    RoboCheckEqInt((int)MakeFontOutlineStatus(o), (int)RoboFontOutlineStatusOk);
    RoboCheckEqInt(FontOutlineDump(o, dump, sizeof(dump), NULL), RoboOk);
    RoboCheck(strstr(dump, "QUAD") != NULL);
    FontOutlineRelease(o);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestOutlineTtCompositeCycle(void)
{
    /* gid0 composite → gid0 (cycle) */
    uint8_t head[64], hhea[64], maxp[64], hmtx[8], loca[12], glyf[32];
    uint8_t font[384];
    SynthTable tabs[6];
    uint8_t *p = glyf;
    uint8_t *lp = loca;
    size_t gn;
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontOutline *o = NULL;

    PutI16(&p, -1);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutU16(&p, (uint16_t)(RoboFontGlyfCompArgsWords | RoboFontGlyfCompArgsXy));
    PutU16(&p, 0); /* self */
    PutI16(&p, 0);
    PutI16(&p, 0);
    gn = (size_t)(p - glyf);
    PutU32(&lp, 0);
    PutU32(&lp, (uint32_t)gn);

    tabs[0].tag = RoboFontTagHead;
    tabs[0].data = head;
    tabs[0].length = (uint32_t)BuildHead(head, 1000);
    tabs[1].tag = RoboFontTagHhea;
    tabs[1].data = hhea;
    tabs[1].length = (uint32_t)BuildHhea(hhea, 1);
    tabs[2].tag = RoboFontTagMaxp;
    tabs[2].data = maxp;
    tabs[2].length = (uint32_t)BuildMaxp(maxp, 1);
    tabs[3].tag = RoboFontTagHmtx;
    tabs[3].data = hmtx;
    tabs[3].length = (uint32_t)BuildHmtx(hmtx, 1);
    tabs[4].tag = RoboFontTagLoca;
    tabs[4].data = loca;
    tabs[4].length = 8u;
    tabs[5].tag = RoboFontTagGlyf;
    tabs[5].data = glyf;
    tabs[5].length = (uint32_t)gn;

    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, tabs, 6);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseOutlines(face), RoboOk);
    RoboCheckEqInt(FontFaceGlyphOutline(face, 0, &o), RoboOk);
    RoboCheckEqInt((int)MakeFontOutlineStatus(o), (int)RoboFontOutlineStatusMalformed);
    FontOutlineRelease(o);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestOutlineAffine(void)
{
    FontAffine a, b, c;
    FontUnit x, y;
    FontAffineIdentity(&a);
    RoboCheckEqInt(FontAffineApply(&a, 10, 20, &x, &y), RoboOk);
    RoboCheckEqInt((int)x, 10);
    RoboCheckEqInt((int)y, 20);
    a.xscale = 0x2000; /* 0.5 */
    a.yscale = 0x2000;
    a.dx = 5;
    RoboCheckEqInt(FontAffineApply(&a, 10, 20, &x, &y), RoboOk);
    RoboCheckEqInt((int)x, 10); /* 5+5 */
    RoboCheckEqInt((int)y, 10);
    FontAffineIdentity(&b);
    b.dx = 3;
    RoboCheckEqInt(FontAffineMultiply(&a, &b, &c), RoboOk);
}

static size_t BuildCompositeGlyf(uint8_t *b, uint16_t componentGid)
{
    uint8_t *p = b;
    PutI16(&p, -1);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 100);
    PutI16(&p, 100);
    PutU16(&p, (uint16_t)(RoboFontGlyfCompArgsWords | RoboFontGlyfCompArgsXy));
    PutU16(&p, componentGid);
    PutI16(&p, 10);
    PutI16(&p, 20);
    return (size_t)(p - b);
}

static void TestOutlineTtComposite(void)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[16], loca[16], glyf[128];
    uint8_t font[512];
    SynthTable tabs[6];
    size_t g0 = BuildSquareGlyf(glyf);
    size_t g1 = BuildCompositeGlyf(glyf + g0, 0);
    uint8_t *lp = loca;
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontOutline *o = NULL;
    FontBbox bb;

    PutU32(&lp, 0);
    PutU32(&lp, (uint32_t)g0);
    PutU32(&lp, (uint32_t)(g0 + g1));

    tabs[0].tag = RoboFontTagHead;
    tabs[0].data = head;
    tabs[0].length = (uint32_t)BuildHead(head, 1000);
    tabs[1].tag = RoboFontTagHhea;
    tabs[1].data = hhea;
    tabs[1].length = (uint32_t)BuildHhea(hhea, 2);
    tabs[2].tag = RoboFontTagMaxp;
    tabs[2].data = maxp;
    tabs[2].length = (uint32_t)BuildMaxp(maxp, 2);
    tabs[3].tag = RoboFontTagHmtx;
    tabs[3].data = hmtx;
    tabs[3].length = (uint32_t)BuildHmtx(hmtx, 2);
    tabs[4].tag = RoboFontTagLoca;
    tabs[4].data = loca;
    tabs[4].length = 12u;
    tabs[5].tag = RoboFontTagGlyf;
    tabs[5].data = glyf;
    tabs[5].length = (uint32_t)(g0 + g1);

    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, tabs, 6);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseOutlines(face), RoboOk);
    RoboCheckEqInt(FontFaceGlyphOutline(face, 1, &o), RoboOk);
    RoboCheckEqInt((int)MakeFontOutlineStatus(o), (int)RoboFontOutlineStatusOk);
    RoboCheckEqInt(FontOutlineBounds(o, &bb), RoboOk);
    RoboCheckEqInt((int)bb.xMin, 10);
    RoboCheckEqInt((int)bb.yMin, 20);
    RoboCheckEqInt((int)bb.xMax, 110);
    RoboCheckEqInt((int)bb.yMax, 120);
    FontOutlineRelease(o);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestOutlineMetricsCross(void)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[8], loca[12], glyf[64];
    uint8_t font[384];
    SynthTable tabs[6];
    size_t gn = BuildSquareGlyf(glyf);
    uint8_t *lp = loca;
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontOutline *o = NULL;
    FontGlyphMetrics gm;
    FontBbox bb;

    PutU32(&lp, 0);
    PutU32(&lp, (uint32_t)gn);
    tabs[0].tag = RoboFontTagHead;
    tabs[0].data = head;
    tabs[0].length = (uint32_t)BuildHead(head, 1000);
    tabs[1].tag = RoboFontTagHhea;
    tabs[1].data = hhea;
    tabs[1].length = (uint32_t)BuildHhea(hhea, 1);
    tabs[2].tag = RoboFontTagMaxp;
    tabs[2].data = maxp;
    tabs[2].length = (uint32_t)BuildMaxp(maxp, 1);
    tabs[3].tag = RoboFontTagHmtx;
    tabs[3].data = hmtx;
    tabs[3].length = (uint32_t)BuildHmtx(hmtx, 1);
    tabs[4].tag = RoboFontTagLoca;
    tabs[4].data = loca;
    tabs[4].length = 8u;
    tabs[5].tag = RoboFontTagGlyf;
    tabs[5].data = glyf;
    tabs[5].length = (uint32_t)gn;

    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, tabs, 6);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseMetrics(face), RoboOk);
    RoboCheckEqInt(FontFaceParseOutlines(face), RoboOk);
    RoboCheckEqInt(FontFaceGlyphMetrics(face, 0, &gm), RoboOk);
    RoboCheckEqInt(FontFaceGlyphOutline(face, 0, &o), RoboOk);
    RoboCheckEqInt(FontOutlineBounds(o, &bb), RoboOk);
    RoboCheckEqInt((int)gm.advanceWidth, 200);
    RoboCheck(bb.xMax > bb.xMin);
    /* LSB from hmtx is 0; outline xmin is 0 — consistent */
    RoboCheckEqInt((int)bb.xMin, (int)gm.leftSideBearing);
    FontOutlineRelease(o);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

/* CFF1 helpers (subset of SuiteRoboFontCff patterns) */
static void DictPutSmall(uint8_t **p, int32_t v) { PutU8(p, (uint8_t)(v + 139)); }
static void DictPutInt(uint8_t **p, int32_t v)
{
    if (v >= -107 && v <= 107) {
        DictPutSmall(p, v);
    } else if (v >= -32768 && v <= 32767) {
        PutU8(p, 28);
        PutU16(p, (uint16_t)(int16_t)v);
    } else {
        PutU8(p, 29);
        PutU32(p, (uint32_t)v);
    }
}
static size_t WriteIndex1(uint8_t *buf, const uint8_t *obj, uint32_t objLen)
{
    uint8_t *p = buf;
    PutU16(&p, 1u);
    PutU8(&p, 1u);
    PutU8(&p, 1u);
    PutU8(&p, (uint8_t)(1u + objLen));
    memcpy(p, obj, objLen);
    p += objLen;
    return (size_t)(p - buf);
}
static size_t WriteEmptyIndex1(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU16(&p, 0u);
    return 2u;
}

static size_t BuildCff1WithCs(uint8_t *out, size_t cap, const uint8_t *cs, uint32_t csLen)
{
    uint8_t name[] = {'T', 'e', 's', 't'};
    uint8_t topd[32];
    uint8_t *t;
    uint8_t privd[8];
    uint8_t *pv;
    size_t privLen, topLen, pos, csPos, charsetPos, privPos, topIndexSize,
        baseAfterIndexes, estTop = 20u;

    if (cap < 256u || csLen == 0u || csLen > 200u) {
        return 0u;
    }
    pv = privd;
    DictPutSmall(&pv, 0);
    PutU8(&pv, 20);
    privLen = (size_t)(pv - privd);

    topIndexSize = 5u + estTop;
    baseAfterIndexes = 4u + (2u + 1u + 2u + 4u) + topIndexSize + 2u + 2u;
    csPos = baseAfterIndexes;
    charsetPos = csPos + 5u + csLen;
    privPos = charsetPos + 1u;

    t = topd;
    DictPutInt(&t, (int32_t)charsetPos);
    PutU8(&t, 15);
    DictPutInt(&t, (int32_t)csPos);
    PutU8(&t, 17);
    DictPutInt(&t, (int32_t)privLen);
    DictPutInt(&t, (int32_t)privPos);
    PutU8(&t, 18);
    topLen = (size_t)(t - topd);

    topIndexSize = 5u + topLen;
    baseAfterIndexes = 4u + (2u + 1u + 2u + 4u) + topIndexSize + 2u + 2u;
    csPos = baseAfterIndexes;
    charsetPos = csPos + 5u + csLen;
    privPos = charsetPos + 1u;

    t = topd;
    DictPutInt(&t, (int32_t)charsetPos);
    PutU8(&t, 15);
    DictPutInt(&t, (int32_t)csPos);
    PutU8(&t, 17);
    DictPutInt(&t, (int32_t)privLen);
    DictPutInt(&t, (int32_t)privPos);
    PutU8(&t, 18);
    topLen = (size_t)(t - topd);

    memset(out, 0, cap);
    pos = 0u;
    out[pos++] = 1;
    out[pos++] = 0;
    out[pos++] = 4;
    out[pos++] = 1;
    pos += WriteIndex1(out + pos, name, 4u);
    pos += WriteIndex1(out + pos, topd, (uint32_t)topLen);
    pos += WriteEmptyIndex1(out + pos);
    pos += WriteEmptyIndex1(out + pos);
    if (pos != csPos) {
        return 0u;
    }
    pos += WriteIndex1(out + pos, cs, csLen);
    if (pos != charsetPos) {
        return 0u;
    }
    out[pos++] = 0;
    if (pos != privPos) {
        return 0u;
    }
    memcpy(out + pos, privd, privLen);
    pos += privLen;
    return pos;
}

static void TestOutlineCffPath(void)
{
    /* Type 2: no closepath — endchar closes. 0 0 rmoveto; 100 0 / 0 50 rlineto; endchar */
    uint8_t cs[] = {(uint8_t)(0 + 139), (uint8_t)(0 + 139), 21, (uint8_t)(100 + 139),
                    (uint8_t)(0 + 139), 5, (uint8_t)(0 + 139), (uint8_t)(50 + 139), 5,
                    0x0e};
    uint8_t cff[256];
    uint8_t font[512];
    SynthTable tabs[1];
    size_t cffLen;
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontOutline *o = NULL;
    char dump[2048];
    FontBbox bb;

    cffLen = BuildCff1WithCs(cff, sizeof(cff), cs, (uint32_t)sizeof(cs));
    RoboCheck(cffLen > 0u);
    tabs[0].tag = RoboFontTagCff;
    tabs[0].data = cff;
    tabs[0].length = (uint32_t)cffLen;
    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionOtto, tabs, 1u);
    RoboCheck(n > 0u);

    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseOutlines(face), RoboOk);
    RoboCheckEqInt(FontFaceGlyphOutline(face, 0, &o), RoboOk);
    RoboCheckEqInt((int)MakeFontOutlineStatus(o), (int)RoboFontOutlineStatusOk);
    RoboCheckEqInt((int)MakeFontOutlineTech(o), (int)RoboFontOutlineCff);
    RoboCheckEqInt(FontOutlineBounds(o, &bb), RoboOk);
    RoboCheckEqInt((int)bb.xMin, 0);
    RoboCheckEqInt((int)bb.xMax, 100);
    RoboCheckEqInt((int)bb.yMax, 50);
    RoboCheckEqInt(FontOutlineDump(o, dump, sizeof(dump), NULL), RoboOk);
    RoboCheck(strstr(dump, "LINE") != NULL);
    RoboCheck(strstr(dump, "CLOSE") != NULL);
    RoboCheckEqInt(FontOutlineFuzzParse(font, n), RoboOk);
    FontOutlineRelease(o);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestOutlineCffCubic(void)
{
    /* 0 0 rmoveto  30 0  0 40  30 0 rrcurveto  endchar */
    uint8_t cs[] = {(uint8_t)(0 + 139),
                    (uint8_t)(0 + 139),
                    21,
                    (uint8_t)(30 + 139),
                    (uint8_t)(0 + 139),
                    (uint8_t)(0 + 139),
                    (uint8_t)(40 + 139),
                    (uint8_t)(30 + 139),
                    (uint8_t)(0 + 139),
                    8,
                    0x0e};
    uint8_t cff[256];
    uint8_t font[512];
    SynthTable tabs[1];
    size_t cffLen;
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontOutline *o = NULL;
    char dump[2048];

    cffLen = BuildCff1WithCs(cff, sizeof(cff), cs, (uint32_t)sizeof(cs));
    RoboCheck(cffLen > 0u);
    tabs[0].tag = RoboFontTagCff;
    tabs[0].data = cff;
    tabs[0].length = (uint32_t)cffLen;
    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionOtto, tabs, 1u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseOutlines(face), RoboOk);
    RoboCheckEqInt(FontFaceGlyphOutline(face, 0, &o), RoboOk);
    RoboCheckEqInt((int)MakeFontOutlineStatus(o), (int)RoboFontOutlineStatusOk);
    RoboCheckEqInt(FontOutlineDump(o, dump, sizeof(dump), NULL), RoboOk);
    RoboCheck(strstr(dump, "CUBIC") != NULL);
    FontOutlineRelease(o);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestOutlineCffEmpty(void)
{
    uint8_t cs[] = {0x0e};
    uint8_t cff[256];
    uint8_t font[512];
    SynthTable tabs[1];
    size_t cffLen;
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontOutline *o = NULL;

    cffLen = BuildCff1WithCs(cff, sizeof(cff), cs, 1u);
    RoboCheck(cffLen > 0u);
    tabs[0].tag = RoboFontTagCff;
    tabs[0].data = cff;
    tabs[0].length = (uint32_t)cffLen;
    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionOtto, tabs, 1u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseOutlines(face), RoboOk);
    RoboCheckEqInt(FontFaceGlyphOutline(face, 0, &o), RoboOk);
    RoboCheckEqInt((int)MakeFontOutlineStatus(o), (int)RoboFontOutlineStatusEmpty);
    FontOutlineRelease(o);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestOutlineGoldenSquare(void)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[8], loca[12], glyf[64];
    uint8_t font[384];
    SynthTable tabs[6];
    size_t gn = BuildSquareGlyf(glyf);
    uint8_t *lp = loca;
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontOutline *o = NULL;
    char dump[1024];
    const char *expect =
        "gid=0 status=0 tech=1 contours=1 bounds=0,0,100,100\n"
        "C 0 closed=1 segs=6\n"
        "  MOVE 0 0\n"
        "  LINE 100 0\n"
        "  LINE 100 100\n"
        "  LINE 0 100\n"
        "  LINE 0 0\n"
        "  CLOSE\n";

    PutU32(&lp, 0);
    PutU32(&lp, (uint32_t)gn);
    tabs[0].tag = RoboFontTagHead;
    tabs[0].data = head;
    tabs[0].length = (uint32_t)BuildHead(head, 1000);
    tabs[1].tag = RoboFontTagHhea;
    tabs[1].data = hhea;
    tabs[1].length = (uint32_t)BuildHhea(hhea, 1);
    tabs[2].tag = RoboFontTagMaxp;
    tabs[2].data = maxp;
    tabs[2].length = (uint32_t)BuildMaxp(maxp, 1);
    tabs[3].tag = RoboFontTagHmtx;
    tabs[3].data = hmtx;
    tabs[3].length = (uint32_t)BuildHmtx(hmtx, 1);
    tabs[4].tag = RoboFontTagLoca;
    tabs[4].data = loca;
    tabs[4].length = 8u;
    tabs[5].tag = RoboFontTagGlyf;
    tabs[5].data = glyf;
    tabs[5].length = (uint32_t)gn;

    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, tabs, 6);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseOutlines(face), RoboOk);
    RoboCheckEqInt(FontFaceGlyphOutline(face, 0, &o), RoboOk);
    RoboCheckEqInt(FontOutlineDump(o, dump, sizeof(dump), NULL), RoboOk);
    RoboCheckStrEq(dump, expect);
    FontOutlineRelease(o);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

void TestRegisterFontOutline(void)
{
    TestRegister("font.outline.tt_simple", TestOutlineTtSimple);
    TestRegister("font.outline.tt_implied", TestOutlineTtImplied);
    TestRegister("font.outline.tt_cycle", TestOutlineTtCompositeCycle);
    TestRegister("font.outline.tt_composite", TestOutlineTtComposite);
    TestRegister("font.outline.affine", TestOutlineAffine);
    TestRegister("font.outline.metrics_cross", TestOutlineMetricsCross);
    TestRegister("font.outline.cff_path", TestOutlineCffPath);
    TestRegister("font.outline.cff_cubic", TestOutlineCffCubic);
    TestRegister("font.outline.cff_empty", TestOutlineCffEmpty);
    TestRegister("font.outline.golden_square", TestOutlineGoldenSquare);
}
