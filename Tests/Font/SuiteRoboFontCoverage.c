/*
 * RoboFont coverage suite (Goal 5.14).
 * Synthetic cmap fixtures only — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

#include <stdio.h>
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

static void CmapBegin(uint8_t **p, uint16_t platform, uint16_t encoding)
{
    PutU16(p, 0);
    PutU16(p, 1);
    PutU16(p, platform);
    PutU16(p, encoding);
    PutU32(p, 12u);
}

static size_t BuildFmt4Single(uint8_t *buf, uint16_t cp, uint16_t gid)
{
    uint8_t *p = buf;
    uint8_t *lenPtr;
    CmapBegin(&p, 3, 1);
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

static size_t BuildFmt4Range(uint8_t *buf, uint16_t first, uint16_t last,
                               uint16_t startGid)
{
    uint8_t *p = buf;
    uint8_t *lenPtr;
    CmapBegin(&p, 3, 1);
    PutU16(&p, 4);
    lenPtr = p;
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 4);
    PutU16(&p, 4);
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutU16(&p, last);
    PutU16(&p, 0xFFFF);
    PutU16(&p, 0);
    PutU16(&p, first);
    PutU16(&p, 0xFFFF);
    PutI16(&p, (int16_t)((int)startGid - (int)first));
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

static size_t BuildFmt12Groups(uint8_t *buf, const uint32_t *starts,
                                 const uint32_t *ends, const uint32_t *gids,
                                 uint32_t n)
{
    uint8_t *p = buf;
    uint8_t *lenPtr;
    uint32_t i;
    CmapBegin(&p, 3, 10);
    PutU16(&p, 12);
    PutU16(&p, 0);
    lenPtr = p;
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, n);
    for (i = 0u; i < n; i++) {
        PutU32(&p, starts[i]);
        PutU32(&p, ends[i]);
        PutU32(&p, gids[i]);
    }
    {
        uint32_t len = (uint32_t)(p - (buf + 12));
        lenPtr[0] = (uint8_t)(len >> 24);
        lenPtr[1] = (uint8_t)(len >> 16);
        lenPtr[2] = (uint8_t)(len >> 8);
        lenPtr[3] = (uint8_t)len;
    }
    return (size_t)(p - buf);
}

static void TestCoverageUnicodeScalars(void)
{
    uint8_t buf[128];
    size_t n = BuildFmt4Single(buf, 0x41u, 1u);
    FontCmapMap *map = NULL;
    FontCoverage *cov = NULL;
    FontCoverageQueryResult q;

    RoboCheckEqInt(
        FontCmapMapCreateFromSpan((FontSpan){buf, n}, 16u, &map),
        RoboOk);
    RoboCheckEqInt(FontCoverageCreateFromCmap(map, NULL, &cov), RoboOk);

    RoboCheckEqInt(FontCoverageQuery(cov, 0x41u, &q), RoboOk);
    RoboCheckEqInt(q.status, RoboFontCoverageMappedUsable);
    RoboCheck(FontCoverageContains(cov, 0x41u));
    RoboCheck(!FontCoverageContains(cov, 0xD800u));
    RoboCheckEqInt(FontCoverageQuery(cov, 0xD800u, &q), RoboOk);
    RoboCheckEqInt(q.status, RoboFontCoverageInvalidCodePoint);
    RoboCheckEqInt(FontCoverageQuery(cov, 0x110000u, &q), RoboOk);
    RoboCheckEqInt(q.status, RoboFontCoverageInvalidCodePoint);
    RoboCheckEqInt(FontCoverageQuery(cov, 0x10FFFFu, &q), RoboOk);
    RoboCheckEqInt(q.status, RoboFontCoverageUnsupported);

    FontCoverageRelease(cov);
    FontCmapMapRelease(map);
}

static void TestCoverageNotdefAndRanges(void)
{
    uint8_t buf[64];
    uint8_t *p = buf;
    uint8_t *lenPtr;
    FontCmapMap *map = NULL;
    FontCoverage *cov = NULL;
    const FontCoverageStats *st;
    const FontCoverageRange *r;
    FontCoverageQueryResult q;
    GlyphId gid = 99u;

    CmapBegin(&p, 3, 1);
    PutU16(&p, 6);
    lenPtr = p;
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 0x40);
    PutU16(&p, 3);
    PutU16(&p, 0); /* '@' → .notdef */
    PutU16(&p, 7); /* 'A' */
    PutU16(&p, 8); /* 'B' */
    {
        uint16_t len = (uint16_t)(p - (buf + 12));
        lenPtr[0] = (uint8_t)(len >> 8);
        lenPtr[1] = (uint8_t)len;
    }

    RoboCheckEqInt(
        FontCmapMapCreateFromSpan((FontSpan){buf, (size_t)(p - buf)},
                                            16u, &map),
        RoboOk);
    RoboCheckEqInt(FontCoverageCreateFromCmap(map, NULL, &cov), RoboOk);
    st = GetFontCoverageStats(cov);
    RoboCheck(st != NULL);
    RoboCheckEqInt((int)st->mappedScalars, 3);
    RoboCheckEqInt((int)st->usableMappings, 2);
    RoboCheckEqInt((int)st->notdefMappings, 1);
    RoboCheckEqInt((int)st->rangeCount, 1);
    r = GetFontCoverageRange(cov, 0);
    RoboCheck(r != NULL);
    RoboCheckEqInt((int)r->first, 0x41);
    RoboCheckEqInt((int)r->last, 0x42);
    RoboCheck(!FontCoverageContains(cov, 0x40u));
    RoboCheckEqInt(FontCoverageQuery(cov, 0x40u, &q), RoboOk);
    RoboCheckEqInt(q.status, RoboFontCoverageMappedNotdef);
    RoboCheckEqInt(FontCoverageGlyph(cov, 0x40u, &gid), RoboNotFound);
    RoboCheckEqInt((int)gid, 0);
    RoboCheckEqInt(FontCoverageGlyph(cov, 0x41u, &gid), RoboOk);
    RoboCheckEqInt((int)gid, 7);

    FontCoverageRelease(cov);
    FontCmapMapRelease(map);
}

static void TestCoverageSparseMerge(void)
{
    uint8_t buf[128];
    size_t n;
    uint8_t *p;
    uint8_t *lenPtr;
    FontCmapMap *map = NULL;
    FontCoverage *cov = NULL;
    const FontCoverageStats *st;

    /* Two non-contiguous singles via format 4 segments: A and C */
    p = buf;
    CmapBegin(&p, 3, 1);
    PutU16(&p, 4);
    lenPtr = p;
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 6); /* 3 segs */
    PutU16(&p, 4);
    PutU16(&p, 1);
    PutU16(&p, 2);
    PutU16(&p, 0x41);
    PutU16(&p, 0x43);
    PutU16(&p, 0xFFFF);
    PutU16(&p, 0);
    PutU16(&p, 0x41);
    PutU16(&p, 0x43);
    PutU16(&p, 0xFFFF);
    PutI16(&p, (int16_t)(1 - 0x41));
    PutI16(&p, (int16_t)(2 - 0x43));
    PutI16(&p, 1);
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 0);
    {
        uint16_t len = (uint16_t)(p - (buf + 12));
        lenPtr[0] = (uint8_t)(len >> 8);
        lenPtr[1] = (uint8_t)len;
    }
    n = (size_t)(p - buf);

    RoboCheckEqInt(
        FontCmapMapCreateFromSpan((FontSpan){buf, n}, 16u, &map),
        RoboOk);
    RoboCheckEqInt(FontCoverageCreateFromCmap(map, NULL, &cov), RoboOk);
    st = GetFontCoverageStats(cov);
    RoboCheckEqInt((int)st->usableMappings, 2);
    RoboCheckEqInt((int)st->rangeCount, 2);
    RoboCheck(FontCoverageContains(cov, 0x41u));
    RoboCheck(!FontCoverageContains(cov, 0x42u));
    RoboCheck(FontCoverageContains(cov, 0x43u));
    /* Must not claim whole Latin block from one letter. */
    RoboCheck(!FontCoverageContains(cov, 0x7Au));

    FontCoverageRelease(cov);
    FontCmapMapRelease(map);
}

static void TestCoverageScripts(void)
{
    struct {
        uint16_t cp;
        UnicodeScript script;
    } cases[] = {
        {0x0041u, RoboUScriptLatin},
        {0x0410u, RoboUScriptCyrillic},
        {0x0391u, RoboUScriptGreek},
        {0x0627u, RoboUScriptArabic},
        {0x05D0u, RoboUScriptHebrew},
        {0x0905u, RoboUScriptDevanagari},
        {0x0985u, RoboUScriptBengali},
        {0x0E01u, RoboUScriptThai},
    };
    size_t i;
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint8_t buf[128];
        size_t n = BuildFmt4Single(buf, cases[i].cp, 1u);
        FontCmapMap *map = NULL;
        FontCoverage *cov = NULL;
        RoboCheckEqInt(
            FontCmapMapCreateFromSpan((FontSpan){buf, n}, 16u, &map),
            RoboOk);
        RoboCheckEqInt(FontCoverageCreateFromCmap(map, NULL, &cov),
                          RoboOk);
        RoboCheck(FontCoverageHasScript(cov, cases[i].script));
        /* Script coverage is not language support. */
        RoboCheck(FontCoverageScriptCount(cov) >= 1u);
        FontCoverageRelease(cov);
        FontCmapMapRelease(map);
    }
}

static void TestCoverageCjkSupplementary(void)
{
    uint8_t buf[96];
    uint32_t starts[2] = {0x4E00u, 0x1F600u};
    uint32_t ends[2] = {0x4E05u, 0x1F601u};
    uint32_t gids[2] = {1u, 10u};
    size_t n = BuildFmt12Groups(buf, starts, ends, gids, 2u);
    FontCmapMap *map = NULL;
    FontCoverage *cov = NULL;
    const FontCoverageStats *st;
    GlyphId gid = 0u;

    RoboCheckEqInt(
        FontCmapMapCreateFromSpan((FontSpan){buf, n}, 32u, &map),
        RoboOk);
    RoboCheckEqInt(FontCoverageCreateFromCmap(map, NULL, &cov), RoboOk);
    st = GetFontCoverageStats(cov);
    RoboCheckEqInt((int)st->usableMappings, 8); /* 6 Han + 2 emoji */
    RoboCheck(FontCoverageContains(cov, 0x4E00u));
    RoboCheck(FontCoverageContains(cov, 0x4E05u));
    RoboCheck(!FontCoverageContains(cov, 0x4E06u));
    RoboCheck(FontCoverageHasScript(cov, RoboUScriptHan));
    RoboCheckEqInt(FontCoverageGlyph(cov, 0x1F600u, &gid), RoboOk);
    RoboCheckEqInt((int)gid, 10);
    RoboCheck(st->emojiScalars >= 1u);
    RoboCheckEqInt((int)st->minCodePoint, 0x4E00);
    RoboCheckEqInt((int)st->maxCodePoint, 0x1F601);

    FontCoverageRelease(cov);
    FontCmapMapRelease(map);
}

static void TestCoverageUvs(void)
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
    FontCoverage *cov = NULL;
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
    RoboCheckEqInt(FontCoverageCreateFromCmap(map, NULL, &cov), RoboOk);
    RoboCheck(GetFontCoverageStats(cov)->hasUvs);
    RoboCheck(FontCoverageContains(cov, 0x41u));
    RoboCheckEqInt(FontCoverageQueryUvs(cov, 0x41u, 0xFE0Fu, &u), RoboOk);
    RoboCheckEqInt(u.status, RoboFontCmapUvsDefault);
    RoboCheckEqInt(FontCoverageQueryUvs(cov, 0x42u, 0xFE0Fu, &u), RoboOk);
    RoboCheckEqInt(u.status, RoboFontCmapUvsUnsupported);
    RoboCheckEqInt(FontCoverageQueryUvs(cov, 0x41u, 0xFE0Eu, &u), RoboOk);
    RoboCheckEqInt(u.status, RoboFontCmapUvsNone);

    FontCoverageRelease(cov);
    FontCmapMapRelease(map);
}

static void TestCoverageFaceLazyAndBench(void)
{
    uint8_t sfnt[512];
    uint8_t maxp[6];
    uint8_t cmap[128];
    uint8_t head[54];
    size_t cmapN;
    size_t i;
    typedef struct {
        uint32_t tag;
        const uint8_t *data;
        uint32_t length;
    } SynthTable;
    uint8_t *p;
    size_t dirEnd;
    size_t cursor;
    SynthTable tables[3];

    memset(head, 0, sizeof(head));
    p = head;
    PutU32(&p, 0x00010000u);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0x5F0F3CF5u);
    PutU16(&p, 0x0800);
    PutU16(&p, 2048);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutI16(&p, 0);
    PutU16(&p, 0);
    PutI16(&p, 0);

    p = maxp;
    PutU32(&p, 0x00005000u);
    PutU16(&p, 128); /* enough GIDs for U+0020..U+007E → 1..95 */

    cmapN = BuildFmt4Range(cmap, 0x20u, 0x7Eu, 1u);
    tables[0].tag = 0x68656164u;
    tables[0].data = head;
    tables[0].length = 54u;
    tables[1].tag = 0x6D617870u;
    tables[1].data = maxp;
    tables[1].length = 6u;
    tables[2].tag = 0x636D6170u;
    tables[2].data = cmap;
    tables[2].length = (uint32_t)cmapN;

    dirEnd = 12u + 3u * 16u;
    cursor = dirEnd;
    memset(sfnt, 0, sizeof(sfnt));
    p = sfnt;
    PutU32(&p, 0x00010000u);
    PutU16(&p, 3);
    PutU16(&p, 32);
    PutU16(&p, 1);
    PutU16(&p, 16);
    for (i = 0u; i < 3u; i++) {
        uint32_t pad = (uint32_t)(((size_t)tables[i].length + 3u) & ~(size_t)3u);
        uint8_t *rec = sfnt + 12u + i * 16u;
        p = rec;
        PutU32(&p, tables[i].tag);
        PutU32(&p, 0u);
        PutU32(&p, (uint32_t)cursor);
        PutU32(&p, tables[i].length);
        memcpy(sfnt + cursor, tables[i].data, tables[i].length);
        cursor += pad;
    }

    {
        FontSource *src = NULL;
        FontFace *face = NULL;
        const FontCoverage *cov;
        volatile int hits = 0;
        RoboCheckEqInt(FontSourceCreateMemoryCopy(sfnt, cursor, &src),
                          RoboOk);
        RoboCheckEqInt(FontFaceCreate(src, 0u, &face), RoboOk);
        RoboCheck(FontFaceCoverage(face) == NULL);
        RoboCheckEqInt(FontFaceParseCoverage(face), RoboOk);
        cov = FontFaceCoverage(face);
        RoboCheck(cov != NULL);
        RoboCheck(FontCoverageContains(cov, 0x41u));
        RoboCheckEqInt(FontFaceParseCoverage(face), RoboOk);

        for (i = 0u; i < 10000u; i++) {
            hits += FontCoverageContains(cov, (uint32_t)(0x20u + (i % 95u)));
        }
        RoboCheck(hits > 0);
        printf("font.coverage.bench.lookup hits=%d\n", hits);

        FontFaceRelease(face);
        FontSourceRelease(src);
    }
}

static void TestCoverageFuzzEdge(void)
{
    static const uint8_t junk[] = {0x00, 0x01, 0xFF, 0xFE, 0x41, 0x00};
    uint8_t emptyCmap[16];
    uint8_t *p = emptyCmap;
    FontCmapMap *map = NULL;
    FontCoverage *cov = NULL;

    RoboCheckEqInt(FontCoverageFuzzParse(junk, sizeof(junk)), RoboOk);
    RoboCheckEqInt(FontCoverageFuzzParse(NULL, 0u), RoboOk);

    /* Header claiming one table but no body — create may fail safely. */
    PutU16(&p, 0);
    PutU16(&p, 1);
    PutU16(&p, 3);
    PutU16(&p, 1);
    PutU32(&p, 12u);
    (void)FontCmapMapCreateFromSpan(
        (FontSpan){emptyCmap, sizeof(emptyCmap)}, 16u, &map);
    if (map) {
        (void)FontCoverageCreateFromCmap(map, NULL, &cov);
        if (cov) {
            FontCoverageRelease(cov);
        }
        FontCmapMapRelease(map);
    }
}

void TestRegisterFontCoverage(void)
{
    TestRegister("font.coverage.unicode", TestCoverageUnicodeScalars);
    TestRegister("font.coverage.notdef_ranges", TestCoverageNotdefAndRanges);
    TestRegister("font.coverage.sparse", TestCoverageSparseMerge);
    TestRegister("font.coverage.scripts", TestCoverageScripts);
    TestRegister("font.coverage.cjk_emoji", TestCoverageCjkSupplementary);
    TestRegister("font.coverage.uvs", TestCoverageUvs);
    TestRegister("font.coverage.face_bench", TestCoverageFaceLazyAndBench);
    TestRegister("font.coverage.fuzz", TestCoverageFuzzEdge);
}
