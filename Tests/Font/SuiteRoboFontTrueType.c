/*
 * RoboFont TrueType table suite (Goal 5.3).
 * Synthetic fixtures only — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboTest.h"

#include <string.h>

static void PutU8(uint8_t **p, uint8_t v)
{
    *(*p)++ = v;
}

static void PutU16(uint8_t **p, uint16_t v)
{
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}

static void PutI16(uint8_t **p, int16_t v)
{
    PutU16(p, (uint16_t)v);
}

static void PutU32(uint8_t **p, uint32_t v)
{
    *(*p)++ = (uint8_t)(v >> 24);
    *(*p)++ = (uint8_t)(v >> 16);
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}

static void PutI64(uint8_t **p, int64_t v)
{
    PutU32(p, (uint32_t)((uint64_t)v >> 32));
    PutU32(p, (uint32_t)(uint64_t)v);
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
        uint8_t *rec = out + 12u + (size_t)i * 16u;
        uint32_t checksum;
        size_t padded = ((size_t)tables[i].length + 3u) & ~(size_t)3u;
        if (tables[i].length && tables[i].data) {
            memcpy(out + cursor, tables[i].data, tables[i].length);
        }
        checksum = FontSfntChecksum(out + cursor, tables[i].length);
        p = rec;
        PutU32(&p, tables[i].tag);
        PutU32(&p, checksum);
        PutU32(&p, (uint32_t)cursor);
        PutU32(&p, tables[i].length);
        cursor += padded;
    }
    return cursor;
}

/* ---- table builders ---- */

static size_t BuildHead(uint8_t *b, uint16_t upem, int16_t locaFmt)
{
    uint8_t *p = b;
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutU32(&p, 0x00010000u);
    PutU32(&p, 0u); /* checksumAdjustment */
    PutU32(&p, RoboFontHeadMagic);
    PutU16(&p, 0u);
    PutU16(&p, upem);
    PutI64(&p, 0);
    PutI64(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, -100);
    PutI16(&p, 1000);
    PutI16(&p, 800);
    PutU16(&p, 0u);
    PutU16(&p, 8u);
    PutI16(&p, 2);
    PutI16(&p, locaFmt);
    PutI16(&p, 0);
    return (size_t)(p - b);
}

static size_t BuildHhea(uint8_t *b, uint16_t nHMetrics)
{
    uint8_t *p = b;
    int i;
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutI16(&p, 800);
    PutI16(&p, -200);
    PutI16(&p, 90);
    PutU16(&p, 1000);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 1000);
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

static size_t BuildMaxpTt(uint8_t *b, uint16_t numGlyphs)
{
    uint8_t *p = b;
    PutU32(&p, 0x00010000u);
    PutU16(&p, numGlyphs);
    PutU16(&p, 8);  /* maxPoints */
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
    PutU16(&p, 4); /* maxComponentElements */
    PutU16(&p, 4); /* maxComponentDepth */
    return (size_t)(p - b);
}

static size_t BuildHmtx(uint8_t *b, uint16_t nMetrics, uint16_t numGlyphs)
{
    uint8_t *p = b;
    uint16_t i;
    for (i = 0u; i < nMetrics; i++) {
        PutU16(&p, (uint16_t)(500u + i));
        PutI16(&p, 10);
    }
    for (i = nMetrics; i < numGlyphs; i++) {
        PutI16(&p, 5);
    }
    return (size_t)(p - b);
}

static size_t BuildLocaLong(uint8_t *b, const uint32_t *offs, uint16_t n)
{
    uint8_t *p = b;
    uint16_t i;
    for (i = 0u; i <= n; i++) {
        PutU32(&p, offs[i]);
    }
    return (size_t)(p - b);
}

static size_t BuildLocaShort(uint8_t *b, const uint32_t *byteOffs, uint16_t n)
{
    uint8_t *p = b;
    uint16_t i;
    for (i = 0u; i <= n; i++) {
        PutU16(&p, (uint16_t)(byteOffs[i] / 2u));
    }
    return (size_t)(p - b);
}

/* Empty glyph = 0 length. Simple one-contour square: 4 points. */
static size_t BuildSimpleGlyf(uint8_t *b)
{
    uint8_t *p = b;
    /* header */
    PutI16(&p, 1); /* contours */
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 100);
    PutI16(&p, 100);
    PutU16(&p, 3); /* endPts: last point index 3 */
    PutU16(&p, 0); /* no instructions */
    /* flags: all on-curve, short vectors positive */
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXShort |
                          RoboFontGlyfFlagYShort | RoboFontGlyfFlagXSameOrPositive |
                          RoboFontGlyfFlagYSameOrPositive));
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXShort |
                          RoboFontGlyfFlagYShort | RoboFontGlyfFlagXSameOrPositive |
                          RoboFontGlyfFlagYSameOrPositive));
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXShort |
                          RoboFontGlyfFlagYShort | RoboFontGlyfFlagXSameOrPositive |
                          RoboFontGlyfFlagYSameOrPositive));
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXShort |
                          RoboFontGlyfFlagYShort | RoboFontGlyfFlagXSameOrPositive |
                          RoboFontGlyfFlagYSameOrPositive));
    /* x deltas */
    PutU8(&p, 0);
    PutU8(&p, 100);
    PutU8(&p, 0);
    PutU8(&p, 100); /* wait - for square we need proper deltas */
    /* Actually redo: start (0,0), (100,0), (100,100), (0,100) */
    /* x: 0, +100, 0, -100 — but we used all positive short. Rebuild flags. */
    (void)0;
    return (size_t)(p - b);
}

/* Cleaner simple glyph builder */
static size_t BuildSimpleSquare(uint8_t *b)
{
    uint8_t *p = b;
    PutI16(&p, 1);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 100);
    PutI16(&p, 100);
    PutU16(&p, 3);
    PutU16(&p, 0);
    /* point0: (0,0) — both same (zero delta) */
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXSameOrPositive |
                          RoboFontGlyfFlagYSameOrPositive));
    /* point1: (+100, 0) */
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXShort |
                          RoboFontGlyfFlagXSameOrPositive |
                          RoboFontGlyfFlagYSameOrPositive));
    /* point2: (0, +100) relative → (100,100) */
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagYShort |
                          RoboFontGlyfFlagXSameOrPositive |
                          RoboFontGlyfFlagYSameOrPositive));
    /* point3: (-100, 0) → (0,100) */
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXShort |
                          RoboFontGlyfFlagYSameOrPositive));
    /* x stream: p1=100, p3=100 (negative because X_SAME clear) */
    PutU8(&p, 100);
    PutU8(&p, 100);
    /* y stream: p2=100 */
    PutU8(&p, 100);
    return (size_t)(p - b);
}

static size_t BuildCompositeGlyf(uint8_t *b, uint16_t componentGid)
{
    uint8_t *p = b;
    PutI16(&p, -1);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 100);
    PutI16(&p, 100);
    /* flags: ARGS_ARE_XY | ARGS_WORDS (no MORE) */
    PutU16(&p, (uint16_t)(RoboFontGlyfCompArgsWords | RoboFontGlyfCompArgsXy));
    PutU16(&p, componentGid);
    PutI16(&p, 10);
    PutI16(&p, 20);
    return (size_t)(p - b);
}

static size_t BuildPost(uint8_t *b)
{
    uint8_t *p = b;
    PutU32(&p, 0x00030000u);
    PutU32(&p, 0u); /* italicAngle */
    PutI16(&p, -100);
    PutI16(&p, 50);
    PutU32(&p, 0u);
    PutU32(&p, 0u);
    PutU32(&p, 0u);
    PutU32(&p, 0u);
    PutU32(&p, 0u);
    return (size_t)(p - b);
}

static size_t BuildOs2V0(uint8_t *b)
{
    uint8_t *p = b;
    int i;
    PutU16(&p, 0);
    PutI16(&p, 500);
    PutU16(&p, 400);
    PutU16(&p, 5);
    PutU16(&p, 0);
    for (i = 0; i < 10; i++) {
        PutI16(&p, 0); /* sub/super/strike — 10 int16s = 5 pairs... actually 10 fields */
    }
    /* We over-wrote — rebuild carefully */
    (void)0;
    return 0;
}

static size_t BuildOs2V1(uint8_t *buf)
{
    uint8_t *p = buf;
    int i;
    PutU16(&p, 1);
    PutI16(&p, 500);
    PutU16(&p, 700); /* bold */
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
    PutI16(&p, 50);
    PutI16(&p, 300);
    PutI16(&p, 0); /* sFamilyClass */
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
    PutU16(&p, 0x0020u); /* fsSelection: BOLD */
    PutU16(&p, 32);
    PutU16(&p, 126);
    PutI16(&p, 800);
    PutI16(&p, -200);
    PutI16(&p, 90);
    PutU16(&p, 800);
    PutU16(&p, 200);
    PutU32(&p, 1);
    PutU32(&p, 0);
    return (size_t)(p - buf);
}

static size_t BuildNameFamily(uint8_t *buf, size_t cap)
{
    /* format 0, 1 record: Windows Unicode "Test" */
    static const uint8_t u16be[] = {0x00, 'T', 0x00, 'e', 0x00, 's', 0x00, 't'};
    uint8_t *p = buf;
    size_t storageOff = 6u + 12u;
    if (cap < storageOff + sizeof(u16be)) {
        return 0u;
    }
    PutU16(&p, 0);
    PutU16(&p, 1);
    PutU16(&p, (uint16_t)storageOff);
    PutU16(&p, 3);    /* platform Windows */
    PutU16(&p, 1);    /* Unicode BMP */
    PutU16(&p, 0x0409);
    PutU16(&p, RoboFontNameIdFamily);
    PutU16(&p, (uint16_t)sizeof(u16be));
    PutU16(&p, 0);
    memcpy(buf + storageOff, u16be, sizeof(u16be));
    return storageOff + sizeof(u16be);
}

/* Full minimal TT font: 2 glyphs (empty + simple), long loca */
static size_t BuildMinTtFont(uint8_t *out, size_t cap, int shortLoca)
{
    uint8_t head[64], hhea[40], maxp[40], hmtx[32], loca[32], glyf[128];
    uint8_t post[40], os2[128], name[64];
    size_t headN, hheaN, maxpN, hmtxN, locaN, glyfN, postN, os2N, nameN;
    uint32_t offs[3];
    size_t g0 = 0;
    size_t g1;
    SynthTable tabs[9];
    uint32_t ntabs = 0;

    headN = BuildHead(head, 1000, shortLoca ? 0 : 1);
    hheaN = BuildHhea(hhea, 1);
    maxpN = BuildMaxpTt(maxp, 2);
    hmtxN = BuildHmtx(hmtx, 1, 2);
    g1 = BuildSimpleSquare(glyf);
    /* pad g1 to even for short loca friendliness */
    while ((g1 & 1u) != 0u) {
        glyf[g1++] = 0;
    }
    glyfN = g1;
    offs[0] = 0;
    offs[1] = 0; /* glyph 0 empty */
    offs[2] = (uint32_t)glyfN;
    /* Move simple glyph to offset 0 actually — glyph0 empty means offs[0]==offs[1]==0,
     * glyf data starts at 0 for glyph1. So copy square at 0 and set offs[1]=0, offs[2]=len.
     * Wait: empty then square: offs = {0, 0, len} means glyph1 is [0,len). Good. */
    locaN = shortLoca ? BuildLocaShort(loca, offs, 2) : BuildLocaLong(loca, offs, 2);
    postN = BuildPost(post);
    os2N = BuildOs2V1(os2);
    nameN = BuildNameFamily(name, sizeof(name));

    tabs[ntabs].tag = RoboFontTagHead;
    tabs[ntabs].data = head;
    tabs[ntabs].length = (uint32_t)headN;
    ntabs++;
    tabs[ntabs].tag = RoboFontTagHhea;
    tabs[ntabs].data = hhea;
    tabs[ntabs].length = (uint32_t)hheaN;
    ntabs++;
    tabs[ntabs].tag = RoboFontTagMaxp;
    tabs[ntabs].data = maxp;
    tabs[ntabs].length = (uint32_t)maxpN;
    ntabs++;
    tabs[ntabs].tag = RoboFontTagHmtx;
    tabs[ntabs].data = hmtx;
    tabs[ntabs].length = (uint32_t)hmtxN;
    ntabs++;
    tabs[ntabs].tag = RoboFontTagLoca;
    tabs[ntabs].data = loca;
    tabs[ntabs].length = (uint32_t)locaN;
    ntabs++;
    tabs[ntabs].tag = RoboFontTagGlyf;
    tabs[ntabs].data = glyf;
    tabs[ntabs].length = (uint32_t)glyfN;
    ntabs++;
    tabs[ntabs].tag = RoboFontTagPost;
    tabs[ntabs].data = post;
    tabs[ntabs].length = (uint32_t)postN;
    ntabs++;
    tabs[ntabs].tag = RoboFontTagOs2;
    tabs[ntabs].data = os2;
    tabs[ntabs].length = (uint32_t)os2N;
    ntabs++;
    tabs[ntabs].tag = RoboFontTagName;
    tabs[ntabs].data = name;
    tabs[ntabs].length = (uint32_t)nameN;
    ntabs++;

    (void)g0;
    (void)BuildSimpleGlyf;
    (void)BuildOs2V0;
    return BuildSfnt(out, cap, RoboFontSfntVersionTrue, tabs, ntabs);
}

static void TestTtHeadValidInvalid(void)
{
    uint8_t head[64];
    size_t n = BuildHead(head, 2048, 1);
    FontSpan span = {head, n};
    FontHead h;
    RoboCheckEqInt(FontHeadParse(span, &h), RoboOk);
    RoboCheckEqInt(h.unitsPerEm, 2048);
    RoboCheckEqInt(h.indexToLocFormat, 1);

    head[12] = 0xDEu;
    head[13] = 0xADu;
    head[14] = 0xBEu;
    head[15] = 0xEFu;
    RoboCheckEqInt(FontHeadParse(span, &h), RoboParseError);

    n = BuildHead(head, 8, 0); /* bad upem */
    span.size = n;
    RoboCheckEqInt(FontHeadParse(span, &h), RoboParseError);

    span.size = 20;
    RoboCheckEqInt(FontHeadParse(span, &h), RoboParseError);
}

static void TestTtIntegratedFont(void)
{
    uint8_t font[2048];
    size_t n = BuildMinTtFont(font, sizeof(font), 0);
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    FontTt *tt = NULL;
    FontGlyfGlyph g;
    uint16_t aw = 0;
    int16_t lsb = 0;
    uint32_t off = 0;
    uint32_t len = 0;
    char namebuf[64];
    size_t namelen = 0;
    FontMetrics metrics;
    FontFace *face = NULL;
    uint32_t gcount = 0;

    RoboCheck(n > 100u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboOk);
    RoboCheckEqInt(FontTtParse(sfnt, RoboFontTtFlagsRequireGlyf, &tt),
                      RoboOk);
    RoboCheckEqInt(FontTtNumGlyphs(tt), 2);
    RoboCheckEqInt(FontTtAdvanceWidth(tt, 0, &aw), RoboOk);
    RoboCheckEqInt(aw, 500);
    RoboCheckEqInt(FontTtAdvanceWidth(tt, 1, &aw), RoboOk);
    RoboCheckEqInt(aw, 500); /* last advance reused */
    RoboCheckEqInt(FontTtLeftSideBearing(tt, 1, &lsb), RoboOk);
    RoboCheckEqInt(lsb, 5);

    RoboCheckEqInt(FontTtGlyphLength(tt, 0, &len), RoboOk);
    RoboCheckEqInt((int)len, 0);
    RoboCheckEqInt(FontTtGlyphOffset(tt, 1, &off), RoboOk);
    RoboCheckEqInt(FontTtGlyphLength(tt, 1, &len), RoboOk);
    RoboCheck(len > 10u);

    memset(&g, 0, sizeof(g));
    RoboCheckEqInt(FontTtParseGlyph(tt, 0, &g), RoboOk);
    RoboCheckEqInt((int)g.kind, (int)RoboFontGlyfEmpty);
    FontGlyfGlyphClear(&g);
    RoboCheckEqInt(FontTtParseGlyph(tt, 1, &g), RoboOk);
    RoboCheckEqInt((int)g.kind, (int)RoboFontGlyfSimple);
    RoboCheckEqInt(g.pointCount, 4);
    RoboCheckEqInt(g.xCoords[0], 0);
    RoboCheckEqInt(g.xCoords[1], 100);
    RoboCheckEqInt(g.yCoords[2], 100);
    RoboCheckEqInt(g.xCoords[3], 0);
    FontGlyfGlyphClear(&g);

    RoboCheck(FontTtName(tt) != NULL);
    RoboCheckEqInt(
        FontNameGetUtf8(FontTtName(tt), RoboFontNameIdFamily, namebuf,
                                sizeof(namebuf), &namelen),
        RoboOk);
    RoboCheckStrEq(namebuf, "Test");

    RoboCheck(FontTtOs2(tt) != NULL);
    RoboCheck(FontTtPost(tt) != NULL);
    RoboCheckEqInt(FontTtMetrics(tt, &metrics), RoboOk);
    RoboCheckEqInt(metrics.unitsPerEm, 1000);
    RoboCheckEqInt(metrics.valid, 1);

    RoboCheckEqInt(FontFaceCreate(src, 0, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseTruetype(face, RoboFontTtFlagsRequireGlyf),
                      RoboOk);
    RoboCheckEqInt(FontFaceGlyphCount(face, &gcount), RoboOk);
    RoboCheckEqInt((int)gcount, 2);

    FontFaceRelease(face);
    FontTtRelease(tt);
    FontSfntRelease(sfnt);
    FontSourceRelease(src);
}

static void TestTtShortLoca(void)
{
    uint8_t font[2048];
    size_t n = BuildMinTtFont(font, sizeof(font), 1);
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    FontTt *tt = NULL;
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryBorrow(font, n, &src), RoboOk);
    RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboOk);
    RoboCheckEqInt(FontTtParse(sfnt, RoboFontTtFlagsRequireGlyf, &tt),
                      RoboOk);
    RoboCheckEqInt(FontTtHead(tt)->indexToLocFormat, 0);
    FontTtRelease(tt);
    FontSfntRelease(sfnt);
    FontSourceRelease(src);
}

static void TestTtComposite(void)
{
    uint8_t head[64], hhea[40], maxp[40], hmtx[16], loca[24], glyf[256];
    size_t headN, hheaN, maxpN, hmtxN, locaN, g0n, g1n;
    uint32_t offs[3];
    SynthTable tabs[6];
    uint8_t font[1024];
    size_t n;
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    FontTt *tt = NULL;
    FontGlyfGlyph g;

    headN = BuildHead(head, 1000, 1);
    hheaN = BuildHhea(hhea, 2);
    maxpN = BuildMaxpTt(maxp, 2);
    hmtxN = BuildHmtx(hmtx, 2, 2);
    g0n = BuildSimpleSquare(glyf);
    while ((g0n & 3u) != 0u) {
        glyf[g0n++] = 0;
    }
    g1n = BuildCompositeGlyf(glyf + g0n, 0);
    offs[0] = 0;
    offs[1] = (uint32_t)g0n;
    offs[2] = (uint32_t)(g0n + g1n);
    locaN = BuildLocaLong(loca, offs, 2);

    tabs[0] = (SynthTable){RoboFontTagHead, head, (uint32_t)headN};
    tabs[1] = (SynthTable){RoboFontTagHhea, hhea, (uint32_t)hheaN};
    tabs[2] = (SynthTable){RoboFontTagMaxp, maxp, (uint32_t)maxpN};
    tabs[3] = (SynthTable){RoboFontTagHmtx, hmtx, (uint32_t)hmtxN};
    tabs[4] = (SynthTable){RoboFontTagLoca, loca, (uint32_t)locaN};
    tabs[5] = (SynthTable){RoboFontTagGlyf, glyf, (uint32_t)(g0n + g1n)};
    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, tabs, 6);

    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboOk);
    RoboCheckEqInt(FontTtParse(sfnt, RoboFontTtFlagsRequireGlyf, &tt),
                      RoboOk);
    memset(&g, 0, sizeof(g));
    RoboCheckEqInt(FontTtParseGlyph(tt, 1, &g), RoboOk);
    RoboCheckEqInt((int)g.kind, (int)RoboFontGlyfComposite);
    RoboCheckEqInt(g.componentCount, 1);
    RoboCheckEqInt(g.components[0].glyphIndex, 0);
    RoboCheckEqInt(g.components[0].arg1, 10);
    RoboCheckEqInt(FontTtValidateCompositeClosure(tt, 1), RoboOk);
    FontGlyfGlyphClear(&g);

    FontTtRelease(tt);
    FontSfntRelease(sfnt);
    FontSourceRelease(src);
}

static void TestTtMalformed(void)
{
    uint8_t head[54];
    FontSpan span;
    FontHead h;
    FontHhea hh;
    FontMaxp mp;
    uint8_t hhea[36];
    uint8_t maxp[6];
    uint8_t *p;

    /* truncated maxp v1 */
    p = maxp;
    PutU32(&p, 0x00010000u);
    PutU16(&p, 10);
    span.data = maxp;
    span.size = 6;
    RoboCheckEqInt(FontMaxpParse(span, &mp), RoboParseError);

    BuildHead(head, 1000, 2); /* bad loca format */
    span.data = head;
    span.size = 54;
    {
        uint8_t *q = head + 50;
        PutI16(&q, 2);
    }
    RoboCheckEqInt(FontHeadParse(span, &h), RoboParseError);

    BuildHhea(hhea, 5);
    span.data = hhea;
    span.size = 20;
    RoboCheckEqInt(FontHheaParse(span, &hh), RoboParseError);

    /* short hmtx via integrated parse */
    {
        uint8_t font[2048];
        size_t n = BuildMinTtFont(font, sizeof(font), 0);
        FontSource *src = NULL;
        FontSfnt *sfnt = NULL;
        FontTt *tt = NULL;
        /* Shrink hmtx length in directory — find hmtx record and set length 2 */
        RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
        RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboOk);
        /* Corrupt after SFNT parse by rebuilding with bad hmtx — easier: */
        FontSfntRelease(sfnt);
        FontSourceRelease(src);
        {
            uint8_t headb[64], hheab[40], maxpb[40], hmtxb[4], locab[32], glyfb[128];
            size_t hn, hen, mn, hmn, ln, gn;
            uint32_t offs[3] = {0, 0, 0};
            SynthTable t[6];
            hn = BuildHead(headb, 1000, 1);
            hen = BuildHhea(hheab, 2);
            mn = BuildMaxpTt(maxpb, 2);
            hmn = 2; /* too short */
            hmtxb[0] = 0;
            hmtxb[1] = 1;
            gn = BuildSimpleSquare(glyfb);
            offs[2] = (uint32_t)gn;
            ln = BuildLocaLong(locab, offs, 2);
            t[0] = (SynthTable){RoboFontTagHead, headb, (uint32_t)hn};
            t[1] = (SynthTable){RoboFontTagHhea, hheab, (uint32_t)hen};
            t[2] = (SynthTable){RoboFontTagMaxp, maxpb, (uint32_t)mn};
            t[3] = (SynthTable){RoboFontTagHmtx, hmtxb, (uint32_t)hmn};
            t[4] = (SynthTable){RoboFontTagLoca, locab, (uint32_t)ln};
            t[5] = (SynthTable){RoboFontTagGlyf, glyfb, (uint32_t)gn};
            n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, t, 6);
            RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
            RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboOk);
            RoboCheckEqInt(FontTtParse(sfnt, RoboFontTtFlagsRequireGlyf, &tt),
                              RoboParseError);
            FontSfntRelease(sfnt);
            FontSourceRelease(src);
        }
    }

    /* non-monotonic loca */
    {
        uint8_t loca[12];
        uint8_t *lp = loca;
        uint32_t *dummy = NULL;
        PutU32(&lp, 0);
        PutU32(&lp, 100);
        PutU32(&lp, 50); /* regress */
        {
            FontSpan ls = {loca, 12};
            /* ParseLoca is static — trigger via full font */
            uint8_t headb[64], hheab[40], maxpb[40], hmtxb[16], glyfb[4];
            SynthTable t[6];
            uint8_t font[512];
            size_t n;
            FontSource *src = NULL;
            FontSfnt *sfnt = NULL;
            FontTt *tt = NULL;
            size_t hn = BuildHead(headb, 1000, 1);
            size_t hen = BuildHhea(hheab, 2);
            size_t mn = BuildMaxpTt(maxpb, 2);
            size_t hmn = BuildHmtx(hmtxb, 2, 2);
            glyfb[0] = 0;
            t[0] = (SynthTable){RoboFontTagHead, headb, (uint32_t)hn};
            t[1] = (SynthTable){RoboFontTagHhea, hheab, (uint32_t)hen};
            t[2] = (SynthTable){RoboFontTagMaxp, maxpb, (uint32_t)mn};
            t[3] = (SynthTable){RoboFontTagHmtx, hmtxb, (uint32_t)hmn};
            t[4] = (SynthTable){RoboFontTagLoca, loca, 12};
            t[5] = (SynthTable){RoboFontTagGlyf, glyfb, 4};
            n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, t, 6);
            RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
            RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboOk);
            RoboCheckEqInt(FontTtParse(sfnt, RoboFontTtFlagsRequireGlyf, &tt),
                              RoboParseError);
            FontSfntRelease(sfnt);
            FontSourceRelease(src);
            (void)dummy;
            (void)ls;
        }
    }

    /* bad name string range */
    {
        uint8_t name[32];
        uint8_t *np = name;
        FontNameTable nt;
        PutU16(&np, 0);
        PutU16(&np, 1);
        PutU16(&np, 18);
        PutU16(&np, 3);
        PutU16(&np, 1);
        PutU16(&np, 0);
        PutU16(&np, 1);
        PutU16(&np, 100); /* length past end */
        PutU16(&np, 0);
        {
            FontSpan ns = {name, 18};
            RoboCheckEqInt(FontNameParse(ns, &nt), RoboParseError);
        }
    }

    /* composite invalid glyph id */
    {
        uint8_t gbuf[64];
        size_t gn = BuildCompositeGlyf(gbuf, 99);
        FontGlyfGlyph g;
        FontSpan gs = {gbuf, gn};
        memset(&g, 0, sizeof(g));
        RoboCheckEqInt(FontTtParseGlyphAt(gs, 0, (uint32_t)gn, 2, &g),
                          RoboParseError);
    }

    RoboCheck(FontTtFuzzParse((const uint8_t *)"notafont", 8) != RoboOk);
}

static void TestTtFlagRepeat(void)
{
    uint8_t b[128];
    uint8_t *p = b;
    FontGlyfGlyph g;
    FontSpan span;
    PutI16(&p, 1);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 10);
    PutI16(&p, 10);
    PutU16(&p, 2); /* 3 points */
    PutU16(&p, 0);
    /* one flag with repeat 2 */
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagRepeat |
                          RoboFontGlyfFlagXSameOrPositive |
                          RoboFontGlyfFlagYSameOrPositive));
    PutU8(&p, 2);
    span.data = b;
    span.size = (size_t)(p - b);
    memset(&g, 0, sizeof(g));
    RoboCheckEqInt(
        FontTtParseGlyphAt(span, 0, (uint32_t)span.size, 1, &g), RoboOk);
    RoboCheckEqInt(g.pointCount, 3);
    FontGlyfGlyphClear(&g);

    /* excess repeat */
    p = b + 12;
    PutU16(&p, 1); /* endPts → 2 points */
    PutU16(&p, 0);
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagRepeat | RoboFontGlyfFlagXSameOrPositive |
                          RoboFontGlyfFlagYSameOrPositive));
    PutU8(&p, 5); /* too many */
    span.size = (size_t)(p - b);
    /* rewrite header endPts for 2 points: offset 10 */
    {
        uint8_t *e = b + 10;
        PutU16(&e, 1);
    }
    RoboCheckEqInt(
        FontTtParseGlyphAt(span, 0, (uint32_t)span.size, 1, &g), RoboParseError);
}

void TestRegisterFontTt(void)
{
    TestRegister("font.tt.head", TestTtHeadValidInvalid);
    TestRegister("font.tt.integrated", TestTtIntegratedFont);
    TestRegister("font.tt.short_loca", TestTtShortLoca);
    TestRegister("font.tt.composite", TestTtComposite);
    TestRegister("font.tt.malformed", TestTtMalformed);
    TestRegister("font.tt.flag_repeat", TestTtFlagRepeat);
}
