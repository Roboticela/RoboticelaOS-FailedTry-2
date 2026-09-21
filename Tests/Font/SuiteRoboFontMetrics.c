/*
 * RoboFont metrics suite (Goal 5.7).
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
    PutI16(&p, -50);
    PutI16(&p, -200);
    PutI16(&p, 1000);
    PutI16(&p, 800);
    PutU16(&p, 0u);
    PutU16(&p, 8u);
    PutI16(&p, 2);
    PutI16(&p, 1); /* long loca */
    PutI16(&p, 0);
    return (size_t)(p - b);
}

static size_t BuildHhea(uint8_t *b, uint16_t nHMetrics, int16_t asc, int16_t desc,
                         int16_t gap)
{
    uint8_t *p = b;
    int i;
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutI16(&p, asc);
    PutI16(&p, desc);
    PutI16(&p, gap);
    PutU16(&p, 900);
    PutI16(&p, -10);
    PutI16(&p, -20);
    PutI16(&p, 880);
    PutI16(&p, 1);
    PutI16(&p, 0);
    PutI16(&p, 0);
    for (i = 0; i < 4; i++) {
        PutI16(&p, 0);
    }
    PutI16(&p, 0);
    PutU16(&p, nHMetrics);
    return (size_t)(p - b);
}

static size_t BuildMaxp(uint8_t *b, uint16_t numGlyphs)
{
    uint8_t *p = b;
    PutU32(&p, 0x00010000u);
    PutU16(&p, numGlyphs);
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
    PutU16(&p, 0);
    PutU16(&p, 0);
    return (size_t)(p - b);
}

static size_t BuildHmtxFull(uint8_t *b, uint16_t n)
{
    uint8_t *p = b;
    uint16_t i;
    for (i = 0u; i < n; i++) {
        PutU16(&p, (uint16_t)(100u + i * 10u));
        PutI16(&p, (int16_t)(i + 1));
    }
    return (size_t)(p - b);
}

static size_t BuildHmtxCompact(uint8_t *b, uint16_t nMetrics, uint16_t numGlyphs)
{
    uint8_t *p = b;
    uint16_t i;
    for (i = 0u; i < nMetrics; i++) {
        PutU16(&p, (uint16_t)(500u + i));
        PutI16(&p, 10);
    }
    for (i = nMetrics; i < numGlyphs; i++) {
        PutI16(&p, (int16_t)(20 + i));
    }
    return (size_t)(p - b);
}

static size_t BuildPost(uint8_t *b)
{
    uint8_t *p = b;
    PutU32(&p, 0x00030000u);
    PutU32(&p, 0u);
    PutI16(&p, -125);
    PutI16(&p, 50);
    PutU32(&p, 0u);
    PutU32(&p, 0u);
    PutU32(&p, 0u);
    PutU32(&p, 0u);
    PutU32(&p, 0u);
    return (size_t)(p - b);
}

static size_t BuildOs2(uint8_t *buf, uint16_t version, uint16_t fsSelection,
                        int16_t typoAsc, int16_t typoDesc, int16_t typoGap)
{
    uint8_t *p = buf;
    int i;
    PutU16(&p, version);
    PutI16(&p, 480); /* xAvgCharWidth */
    PutU16(&p, 400);
    PutU16(&p, 5);
    PutU16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 40);  /* strikeout size */
    PutI16(&p, 250); /* strikeout pos */
    PutI16(&p, 0);
    for (i = 0; i < 10; i++) {
        PutU8(&p, 0);
    }
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU8(&p, 'R');
    PutU8(&p, 'O');
    PutU8(&p, 'B');
    PutU8(&p, 'O');
    PutU16(&p, fsSelection);
    PutU16(&p, 32);
    PutU16(&p, 126);
    PutI16(&p, typoAsc);
    PutI16(&p, typoDesc);
    PutI16(&p, typoGap);
    PutU16(&p, 850);
    PutU16(&p, 250);
    if (version >= 1u) {
        PutU32(&p, 1);
        PutU32(&p, 0);
    }
    if (version >= 2u) {
        PutI16(&p, 500); /* sxHeight */
        PutI16(&p, 700); /* sCapHeight */
        PutU16(&p, 0);
        PutU16(&p, 32);
        PutU16(&p, 0);
    }
    return (size_t)(p - buf);
}

static size_t BuildVhea(uint8_t *b, uint16_t nV)
{
    uint8_t *p = b;
    int i;
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutI16(&p, 800);
    PutI16(&p, -200);
    PutI16(&p, 0);
    PutI16(&p, 1000);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 1000);
    PutI16(&p, 0);
    PutI16(&p, 1);
    PutI16(&p, 0);
    for (i = 0; i < 4; i++) {
        PutI16(&p, 0);
    }
    PutI16(&p, 0);
    PutU16(&p, nV);
    return (size_t)(p - b);
}

static size_t BuildVmtx(uint8_t *b, uint16_t nMetrics, uint16_t numGlyphs)
{
    uint8_t *p = b;
    uint16_t i;
    for (i = 0u; i < nMetrics; i++) {
        PutU16(&p, (uint16_t)(1000u + i));
        PutI16(&p, 5);
    }
    for (i = nMetrics; i < numGlyphs; i++) {
        PutI16(&p, 7);
    }
    return (size_t)(p - b);
}

static size_t BuildEmptyLocaGlyf(uint8_t *loca, uint8_t *glyf, uint16_t n,
                                    size_t *glyfN)
{
    uint8_t *lp = loca;
    uint16_t i;
    for (i = 0u; i <= n; i++) {
        PutU32(&lp, 0u);
    }
    *glyfN = 0u;
    (void)glyf;
    return (size_t)(lp - loca);
}

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

static void TestMetricsScale(void)
{
    float px = 0.0f;
    RoboCheck(FontUnitsPerEmIsValid(1000u));
    RoboCheck(!FontUnitsPerEmIsValid(0u));
    RoboCheck(!FontUnitsPerEmIsValid(20000u));
    RoboCheckEqInt(FontScaleFontUnits(500, 1000u, 20.0f,
                                                 RoboFontRoundNearest, &px),
                      RoboOk);
    RoboCheck(px > 9.5f && px < 10.5f);
    RoboCheckEqInt(FontScaleFontUnits(1, 1000u, 16.0f, RoboFontRoundFloor, &px),
                      RoboOk);
    RoboCheck(px == 0.0f);
    RoboCheck(FontScaleFontUnits(1, 0u, 16.0f, RoboFontRoundNearest, &px) !=
               RoboOk);
}

static void TestMetricsGlobalAndTypo(void)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[64], post[40], os2[128];
    uint8_t loca[32], glyf[8];
    uint8_t font[512];
    size_t glyfN = 0;
    size_t n;
    SynthTable tabs[8];
    uint32_t nt = 0;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontMetrics layout;
    const FontMetricsGlobal *g;

    size_t headN = BuildHead(head, 1000);
    size_t hheaN = BuildHhea(hhea, 2, 800, -200, 90);
    size_t maxpN = BuildMaxp(maxp, 2);
    size_t hmtxN = BuildHmtxFull(hmtx, 2);
    size_t postN = BuildPost(post);
    size_t os2N =
        BuildOs2(os2, 2, RoboFontOs2UseTypoMetrics, 750, -180, 50);
    size_t locaN = BuildEmptyLocaGlyf(loca, glyf, 2, &glyfN);

    tabs[nt].tag = RoboFontTagHead;
    tabs[nt].data = head;
    tabs[nt].length = (uint32_t)headN;
    nt++;
    tabs[nt].tag = RoboFontTagHhea;
    tabs[nt].data = hhea;
    tabs[nt].length = (uint32_t)hheaN;
    nt++;
    tabs[nt].tag = RoboFontTagMaxp;
    tabs[nt].data = maxp;
    tabs[nt].length = (uint32_t)maxpN;
    nt++;
    tabs[nt].tag = RoboFontTagHmtx;
    tabs[nt].data = hmtx;
    tabs[nt].length = (uint32_t)hmtxN;
    nt++;
    tabs[nt].tag = RoboFontTagLoca;
    tabs[nt].data = loca;
    tabs[nt].length = (uint32_t)locaN;
    nt++;
    tabs[nt].tag = RoboFontTagGlyf;
    tabs[nt].data = glyf;
    tabs[nt].length = 0u;
    nt++;
    tabs[nt].tag = RoboFontTagPost;
    tabs[nt].data = post;
    tabs[nt].length = (uint32_t)postN;
    nt++;
    tabs[nt].tag = RoboFontTagOs2;
    tabs[nt].data = os2;
    tabs[nt].length = (uint32_t)os2N;
    nt++;

    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, tabs, nt);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseMetrics(face), RoboOk);
    g = FontMetricsInfoGlobal(FontFaceMetricsInfo(face));
    RoboCheck(g != NULL);
    RoboCheckEqInt((int)g->unitsPerEm, 1000);
    RoboCheckEqInt((int)g->layoutSource, (int)RoboFontLineFromOs2Typo);
    RoboCheckEqInt(FontFaceLayoutMetrics(face, &layout), RoboOk);
    RoboCheckEqInt((int)layout.ascent, 750);
    RoboCheckEqInt((int)layout.descent, 180);
    RoboCheckEqInt((int)layout.lineGap, 50);
    RoboCheckEqInt((int)FontMetricsLineHeight(&layout), 980);
    RoboCheckEqInt((int)g->capHeight.avail, (int)RoboFontMetricPresent);
    RoboCheckEqInt((int)g->capHeight.value, 700);
    RoboCheckEqInt((int)g->underlinePosition.avail, (int)RoboFontMetricPresent);
    RoboCheckEqInt((int)g->underlinePosition.value, -125);
    RoboCheckEqInt((int)g->strikeoutThickness.value, 40);
    RoboCheckEqInt((int)g->winAscent, 850);
    RoboCheckEqInt((int)g->fontBboxAvail, (int)RoboFontMetricPresent);

    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestMetricsHheaDefaultLine(void)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[32];
    uint8_t font[256];
    SynthTable tabs[4];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontMetrics layout;
    const FontMetricsGlobal *g;

    tabs[0].tag = RoboFontTagHead;
    tabs[0].data = head;
    tabs[0].length = (uint32_t)BuildHead(head, 2048);
    tabs[1].tag = RoboFontTagHhea;
    tabs[1].data = hhea;
    tabs[1].length = (uint32_t)BuildHhea(hhea, 1, 1800, -400, 100);
    tabs[2].tag = RoboFontTagMaxp;
    tabs[2].data = maxp;
    tabs[2].length = (uint32_t)BuildMaxp(maxp, 1);
    tabs[3].tag = RoboFontTagHmtx;
    tabs[3].data = hmtx;
    tabs[3].length = (uint32_t)BuildHmtxFull(hmtx, 1);

    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, tabs, 4);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseMetrics(face), RoboOk);
    g = FontMetricsInfoGlobal(FontFaceMetricsInfo(face));
    RoboCheckEqInt((int)g->layoutSource, (int)RoboFontLineFromHhea);
    RoboCheckEqInt(FontFaceLayoutMetrics(face, &layout), RoboOk);
    RoboCheckEqInt((int)layout.ascent, 1800);
    RoboCheckEqInt((int)layout.descent, 400);
    RoboCheckEqInt((int)g->underlinePosition.avail, (int)RoboFontMetricFallback);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestMetricsCompactHmtx(void)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[64];
    uint8_t font[256];
    SynthTable tabs[4];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontGlyphMetrics gm;

    tabs[0].tag = RoboFontTagHead;
    tabs[0].data = head;
    tabs[0].length = (uint32_t)BuildHead(head, 1000);
    tabs[1].tag = RoboFontTagHhea;
    tabs[1].data = hhea;
    tabs[1].length = (uint32_t)BuildHhea(hhea, 1, 800, -200, 0); /* 1 of 3 */
    tabs[2].tag = RoboFontTagMaxp;
    tabs[2].data = maxp;
    tabs[2].length = (uint32_t)BuildMaxp(maxp, 3);
    tabs[3].tag = RoboFontTagHmtx;
    tabs[3].data = hmtx;
    tabs[3].length = (uint32_t)BuildHmtxCompact(hmtx, 1, 3);

    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, tabs, 4);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseMetrics(face), RoboOk);

    RoboCheckEqInt(FontFaceGlyphMetrics(face, 0, &gm), RoboOk);
    RoboCheckEqInt((int)gm.status, (int)RoboFontGlyphMetricsOk);
    RoboCheckEqInt((int)gm.advanceWidth, 500);
    RoboCheckEqInt((int)gm.leftSideBearing, 10);

    RoboCheckEqInt(FontFaceGlyphMetrics(face, 2, &gm), RoboOk);
    RoboCheckEqInt((int)gm.advanceWidth, 500); /* reused */
    RoboCheckEqInt((int)gm.leftSideBearing, 22);

    RoboCheckEqInt(FontFaceGlyphMetrics(face, 99, &gm), RoboOk);
    RoboCheckEqInt((int)gm.status, (int)RoboFontGlyphMetricsInvalidId);

    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestMetricsGlyphBboxRsb(void)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[16], loca[16], glyf[64];
    uint8_t font[384];
    SynthTable tabs[6];
    uint32_t offs[3];
    size_t gn;
    size_t n;
    uint8_t *lp;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontGlyphMetrics gm;

    gn = BuildSquareGlyf(glyf);
    offs[0] = 0;
    offs[1] = (uint32_t)gn;
    offs[2] = (uint32_t)gn; /* glyph 1 empty */
    lp = loca;
    PutU32(&lp, offs[0]);
    PutU32(&lp, offs[1]);
    PutU32(&lp, offs[2]);

    /* hmtx: gid0 advance 200, lsb 10 → RSB = 200-10-100 = 90 */
    {
        uint8_t *p = hmtx;
        PutU16(&p, 200);
        PutI16(&p, 10);
        PutU16(&p, 50);
        PutI16(&p, 0);
    }

    tabs[0].tag = RoboFontTagHead;
    tabs[0].data = head;
    tabs[0].length = (uint32_t)BuildHead(head, 1000);
    tabs[1].tag = RoboFontTagHhea;
    tabs[1].data = hhea;
    tabs[1].length = (uint32_t)BuildHhea(hhea, 2, 800, -200, 0);
    tabs[2].tag = RoboFontTagMaxp;
    tabs[2].data = maxp;
    tabs[2].length = (uint32_t)BuildMaxp(maxp, 2);
    tabs[3].tag = RoboFontTagHmtx;
    tabs[3].data = hmtx;
    tabs[3].length = 8u;
    tabs[4].tag = RoboFontTagLoca;
    tabs[4].data = loca;
    tabs[4].length = 12u;
    tabs[5].tag = RoboFontTagGlyf;
    tabs[5].data = glyf;
    tabs[5].length = (uint32_t)gn;

    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, tabs, 6);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseMetrics(face), RoboOk);
    RoboCheckEqInt(FontFaceGlyphMetrics(face, 0, &gm), RoboOk);
    RoboCheck(gm.hasBbox);
    RoboCheck(gm.hasRsb);
    RoboCheckEqInt((int)gm.rightSideBearing, 90);
    RoboCheckEqInt(FontFaceGlyphMetrics(face, 1, &gm), RoboOk);
    RoboCheck(gm.hasBbox);
    RoboCheck(gm.hasRsb);
    RoboCheckEqInt((int)gm.rightSideBearing, 50); /* empty: adv-lsb */

    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestMetricsVertical(void)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[16], vhea[64], vmtx[16];
    uint8_t font[384];
    SynthTable tabs[6];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontVerticalGlyphMetrics vm;
    const FontMetricsGlobal *g;

    tabs[0].tag = RoboFontTagHead;
    tabs[0].data = head;
    tabs[0].length = (uint32_t)BuildHead(head, 1000);
    tabs[1].tag = RoboFontTagHhea;
    tabs[1].data = hhea;
    tabs[1].length = (uint32_t)BuildHhea(hhea, 2, 800, -200, 0);
    tabs[2].tag = RoboFontTagMaxp;
    tabs[2].data = maxp;
    tabs[2].length = (uint32_t)BuildMaxp(maxp, 2);
    tabs[3].tag = RoboFontTagHmtx;
    tabs[3].data = hmtx;
    tabs[3].length = (uint32_t)BuildHmtxFull(hmtx, 2);
    tabs[4].tag = RoboFontTagVhea;
    tabs[4].data = vhea;
    tabs[4].length = (uint32_t)BuildVhea(vhea, 1);
    tabs[5].tag = RoboFontTagVmtx;
    tabs[5].data = vmtx;
    tabs[5].length = (uint32_t)BuildVmtx(vmtx, 1, 2);

    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, tabs, 6);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseMetrics(face), RoboOk);
    g = FontMetricsInfoGlobal(FontFaceMetricsInfo(face));
    RoboCheck(g->hasVertical);
    RoboCheck(FontMetricsInfoVhea(FontFaceMetricsInfo(face)) != NULL);
    RoboCheckEqInt(
        FontMetricsInfoGlyphVertical(FontFaceMetricsInfo(face), 0, &vm),
        RoboOk);
    RoboCheckEqInt((int)vm.advanceHeight, 1000);
    RoboCheckEqInt(
        FontMetricsInfoGlyphVertical(FontFaceMetricsInfo(face), 1, &vm),
        RoboOk);
    RoboCheckEqInt((int)vm.advanceHeight, 1000);
    RoboCheckEqInt((int)vm.topSideBearing, 7);

    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestMetricsInstanceAndFuzz(void)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[16];
    uint8_t font[256];
    SynthTable tabs[4];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontInstance *inst = NULL;
    FontMetrics m;
    float px = 0.0f;

    tabs[0].tag = RoboFontTagHead;
    tabs[0].data = head;
    tabs[0].length = (uint32_t)BuildHead(head, 1000);
    tabs[1].tag = RoboFontTagHhea;
    tabs[1].data = hhea;
    tabs[1].length = (uint32_t)BuildHhea(hhea, 1, 800, -200, 0);
    tabs[2].tag = RoboFontTagMaxp;
    tabs[2].data = maxp;
    tabs[2].length = (uint32_t)BuildMaxp(maxp, 1);
    tabs[3].tag = RoboFontTagHmtx;
    tabs[3].data = hmtx;
    tabs[3].length = (uint32_t)BuildHmtxFull(hmtx, 1);

    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, tabs, 4);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseMetrics(face), RoboOk);
    RoboCheckEqInt(FontInstanceCreate(face, 16.0f, NULL, &inst), RoboOk);
    RoboCheckEqInt(FontInstanceMetrics(inst, &m), RoboOk);
    RoboCheck(m.valid);
    RoboCheckEqInt(FontInstanceScaleAdvance(inst, 500, RoboFontRoundNearest, &px),
                      RoboOk);
    RoboCheck(px > 7.5f && px < 8.5f);

    RoboCheckEqInt(FontMetricsFuzzParse(font, n), RoboOk);
    RoboCheckEqInt(FontMetricsFuzzParse((const uint8_t *)"xxxx", 4), RoboOk);

    FontInstanceRelease(inst);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

void TestRegisterFontMetrics(void)
{
    TestRegister("font.metrics.scale", TestMetricsScale);
    TestRegister("font.metrics.global_typo", TestMetricsGlobalAndTypo);
    TestRegister("font.metrics.hhea_line", TestMetricsHheaDefaultLine);
    TestRegister("font.metrics.compact_hmtx", TestMetricsCompactHmtx);
    TestRegister("font.metrics.glyph_bbox", TestMetricsGlyphBboxRsb);
    TestRegister("font.metrics.vertical", TestMetricsVertical);
    TestRegister("font.metrics.instance_fuzz", TestMetricsInstanceAndFuzz);
}
