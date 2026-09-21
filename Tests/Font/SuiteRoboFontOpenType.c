/*
 * RoboFont OpenType structural suite (Goal 5.4).
 * Synthetic fixtures — no Chromium / system fonts. No shaping execution.
 */

#include "RoboFont/RoboFont.h"
#include "RoboTest.h"

#include <string.h>

static void PutU16(uint8_t **p, uint16_t v)
{
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}

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

static size_t BuildSfnt(uint8_t *out,
                         size_t outCap,
                         uint32_t version,
                         const SynthTable *tables,
                         uint32_t n)
{
    size_t dirEnd = 12u + (size_t)n * 16u;
    size_t need = dirEnd;
    size_t cursor;
    uint32_t i;
    uint8_t *p;
    uint16_t pow2 = 1u;
    uint16_t entry = 0u;

    for (i = 0u; i < n; i++) {
        need += ((size_t)tables[i].length + 3u) & ~(size_t)3u;
    }
    if (need > outCap) {
        return 0u;
    }
    memset(out, 0, need);
    while ((uint16_t)(pow2 * 2u) <= (uint16_t)n) {
        pow2 = (uint16_t)(pow2 * 2u);
        entry++;
    }
    p = out;
    PutU32(&p, version);
    PutU16(&p, (uint16_t)n);
    PutU16(&p, (uint16_t)(pow2 * 16u));
    PutU16(&p, entry);
    PutU16(&p, (uint16_t)(n * 16u - pow2 * 16u));
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

static void TestOtOutlineDetect(void)
{
    uint8_t maxp[6];
    uint8_t *p = maxp;
    SynthTable tabs[2];
    uint8_t font[256];
    size_t n;
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;

    PutU32(&p, 0x00005000u);
    PutU16(&p, 1);
    tabs[0].tag = RoboFontTagMaxp;
    tabs[0].data = maxp;
    tabs[0].length = 6;
    tabs[1].tag = RoboFontTagCff;
    tabs[1].data = (const uint8_t *)"CFF ";
    tabs[1].length = 4;
    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionOtto, tabs, 2);
    RoboCheckEqInt(FontSourceCreateMemoryBorrow(font, n, &src), RoboOk);
    RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboOk);
    RoboCheckEqInt((int)FontOtDetectOutline(sfnt), (int)RoboFontOutlineCff);
    RoboCheckEqInt((int)MakeFontOtTableFamily(RoboFontTagGsub),
                      (int)RoboFontOtFamilyLayout);
    RoboCheckEqInt((int)MakeFontOtTableFamily(RoboFontTagColr),
                      (int)RoboFontOtFamilyColor);
    FontSfntRelease(sfnt);
    FontSourceRelease(src);
}

static void TestOtCoverageClassdef(void)
{
    uint8_t cov1[16];
    uint8_t cov2[20];
    uint8_t cls1[16];
    uint8_t cls2[20];
    uint8_t *p;
    FontSpan span;
    FontOtCoverage c;
    FontOtClassDef d;
    uint16_t idx = 99;
    uint16_t cl = 99;

    p = cov1;
    PutU16(&p, 1);
    PutU16(&p, 3);
    PutU16(&p, 10);
    PutU16(&p, 20);
    PutU16(&p, 30);
    span.data = cov1;
    span.size = (size_t)(p - cov1);
    memset(&c, 0, sizeof(c));
    RoboCheckEqInt(FontOtCoverageParse(span, 0, &c), RoboOk);
    RoboCheckEqInt(FontOtCoverageIndex(&c, 20, &idx), RoboOk);
    RoboCheckEqInt(idx, 1);
    RoboCheckEqInt(FontOtCoverageIndex(&c, 11, &idx), RoboNotFound);
    FontOtCoverageClear(&c);

    p = cov2;
    PutU16(&p, 2);
    PutU16(&p, 1);
    PutU16(&p, 100);
    PutU16(&p, 102);
    PutU16(&p, 0);
    span.data = cov2;
    span.size = (size_t)(p - cov2);
    RoboCheckEqInt(FontOtCoverageParse(span, 0, &c), RoboOk);
    RoboCheckEqInt(FontOtCoverageIndex(&c, 101, &idx), RoboOk);
    RoboCheckEqInt(idx, 1);
    FontOtCoverageClear(&c);

    p = cls1;
    PutU16(&p, 1);
    PutU16(&p, 5);
    PutU16(&p, 2);
    PutU16(&p, 1);
    PutU16(&p, 2);
    span.data = cls1;
    span.size = (size_t)(p - cls1);
    memset(&d, 0, sizeof(d));
    RoboCheckEqInt(FontOtClassDefParse(span, 0, &d), RoboOk);
    RoboCheckEqInt(FontOtClassDefGet(&d, 5, &cl), RoboOk);
    RoboCheckEqInt(cl, 1);
    RoboCheckEqInt(FontOtClassDefGet(&d, 4, &cl), RoboOk);
    RoboCheckEqInt(cl, 0);
    FontOtClassDefClear(&d);

    p = cls2;
    PutU16(&p, 2);
    PutU16(&p, 1);
    PutU16(&p, 10);
    PutU16(&p, 12);
    PutU16(&p, 3);
    span.data = cls2;
    span.size = (size_t)(p - cls2);
    RoboCheckEqInt(FontOtClassDefParse(span, 0, &d), RoboOk);
    RoboCheckEqInt(FontOtClassDefGet(&d, 11, &cl), RoboOk);
    RoboCheckEqInt(cl, 3);
    FontOtClassDefClear(&d);

    /* malformed: unsorted coverage */
    p = cov1;
    PutU16(&p, 1);
    PutU16(&p, 2);
    PutU16(&p, 30);
    PutU16(&p, 10);
    span.data = cov1;
    span.size = 8;
    RoboCheckEqInt(FontOtCoverageParse(span, 0, &c), RoboParseError);
}

static void TestOtCmapStructural(void)
{
    /* cmap with one format 4 encoding (minimal header only for bounds) */
    uint8_t cmap[64];
    uint8_t *p = cmap;
    uint8_t *sub;
    FontCmap c;
    size_t subOff;

    PutU16(&p, 0); /* version */
    PutU16(&p, 1); /* numTables */
    PutU16(&p, 3); /* platform */
    PutU16(&p, 1); /* encoding */
    subOff = 12u;
    PutU32(&p, (uint32_t)subOff);
    sub = cmap + subOff;
    PutU16(&sub, 4);  /* format */
    PutU16(&sub, 16); /* length — minimal stub (will fail if we required full fmt4) */
    PutU16(&sub, 0);  /* language */
    /* pad rest of claimed length */
    while ((size_t)(sub - cmap) < subOff + 16u) {
        *sub++ = 0;
    }

    memset(&c, 0, sizeof(c));
    {
        FontSpan span = {cmap, subOff + 16u};
        RoboCheckEqInt(FontCmapParse(span, &c), RoboOk);
        RoboCheckEqInt(c.numTables, 1);
        RoboCheckEqInt(c.encodings[0].format, 4);
        RoboCheck(FontCmapFormatIsRecognized(4));
        RoboCheck(FontCmapFormatIsRecognized(12));
        RoboCheck(!FontCmapFormatIsRecognized(99));
        FontCmapClear(&c);
    }

    /* truncated encoding list */
    {
        FontSpan span = {cmap, 8};
        RoboCheckEqInt(FontCmapParse(span, &c), RoboParseError);
    }
}

static size_t BuildMinGsub(uint8_t *buf, size_t cap)
{
    /*
     * Layout:
     * 0: header v1.0
     * ScriptList at 10: count=1, script 'latn' -> Script at S
     * FeatureList at F: count=1, feature 'liga' -> Feature at FE
     * LookupList at L: count=1, Lookup at LU (type 1, flag 0, 1 subtable)
     */
    uint8_t *p = buf;
    size_t scriptList = 10;
    size_t featureList;
    size_t lookupList;
    size_t scriptTable;
    size_t featureTable;
    size_t lookupTable;
    size_t subtable;

    if (cap < 128u) {
        return 0u;
    }
    memset(buf, 0, cap);

    /* ScriptList */
    p = buf + scriptList;
    PutU16(&p, 1);
    PutU32(&p, RoboFontTag('l', 'a', 't', 'n'));
    scriptTable = (size_t)(p - buf) + 2u; /* after offset field */
    PutU16(&p, (uint16_t)(scriptTable - scriptList));

    /* Script: no default, 0 langsys */
    p = buf + scriptTable;
    PutU16(&p, 0); /* defaultLangSys */
    PutU16(&p, 0); /* langSysCount */

    featureList = (size_t)(p - buf);
    /* FeatureList */
    PutU16(&p, 1);
    PutU32(&p, RoboFontTag('l', 'i', 'g', 'a'));
    featureTable = (size_t)(p - buf) + 2u;
    PutU16(&p, (uint16_t)(featureTable - featureList));

    p = buf + featureTable;
    PutU16(&p, 0); /* featureParams */
    PutU16(&p, 1); /* lookupIndexCount */
    PutU16(&p, 0); /* lookup index 0 */

    lookupList = (size_t)(p - buf);
    PutU16(&p, 1);
    lookupTable = (size_t)(p - buf) + 2u;
    PutU16(&p, (uint16_t)(lookupTable - lookupList));

    p = buf + lookupTable;
    PutU16(&p, 1); /* type SingleSubst */
    PutU16(&p, 0); /* flags */
    PutU16(&p, 1); /* subtableCount */
    subtable = (size_t)(p - buf) + 2u;
    PutU16(&p, (uint16_t)(subtable - lookupTable));
    /* stub subtable bytes */
    p = buf + subtable;
    PutU16(&p, 1); /* format */
    PutU16(&p, 0); /* coverage offset placeholder */
    PutU16(&p, 0); /* delta */

    /* Header at 0 */
    p = buf;
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutU16(&p, (uint16_t)scriptList);
    PutU16(&p, (uint16_t)featureList);
    PutU16(&p, (uint16_t)lookupList);

    return (size_t)(subtable + 6u);
}

static void TestOtGsubGposGdef(void)
{
    uint8_t gsub[256];
    uint8_t gdef[32];
    uint8_t *p;
    size_t gn;
    FontOtLayout L;
    FontGdef g;
    FontSpan span;

    gn = BuildMinGsub(gsub, sizeof(gsub));
    RoboCheck(gn > 20u);
    span.data = gsub;
    span.size = gn;
    memset(&L, 0, sizeof(L));
    RoboCheckEqInt(FontGsubParse(span, &L), RoboOk);
    RoboCheckEqInt(L.scripts.scriptCount, 1);
    RoboCheckEqInt((int)L.scripts.scripts[0].scriptTag,
                      (int)RoboFontTag('l', 'a', 't', 'n'));
    RoboCheckEqInt(L.features.featureCount, 1);
    RoboCheckEqInt(L.lookups.lookupCount, 1);
    RoboCheckEqInt(L.lookups.lookups[0].lookupType, 1);
    RoboCheck(FontGsubLookupTypeIsKnown(4));
    RoboCheck(FontGposLookupTypeIsKnown(2));
    FontOtLayoutClear(&L);

    /* Same bytes as GPOS should also parse (shared header). */
    RoboCheckEqInt(FontGposParse(span, &L), RoboOk);
    /* But lookup type 1 is valid for GPOS too (single adjustment). */
    FontOtLayoutClear(&L);

    /* GDEF with glyph class def format 2 empty */
    p = gdef;
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutU16(&p, 12); /* glyphClassDef offset */
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 2); /* ClassDef format 2 */
    PutU16(&p, 0); /* range count 0 */
    span.data = gdef;
    span.size = (size_t)(p - gdef);
    memset(&g, 0, sizeof(g));
    RoboCheckEqInt(FontGdefParse(span, &g), RoboOk);
    RoboCheck(g.hasGlyphClassDef);
    FontGdefClear(&g);

    /* truncated GSUB */
    span.size = 8;
    RoboCheckEqInt(FontGsubParse(span, &L), RoboParseError);
}

static void TestOtExtraTables(void)
{
    uint8_t base[16];
    uint8_t math[16];
    uint8_t cpal[32];
    uint8_t colr[32];
    uint8_t stat[48];
    uint8_t kern[32];
    uint8_t *p;
    FontSpan span;
    FontBase b;
    FontMath m;
    FontCpal cp;
    FontColr co;
    FontStat st;
    FontKern k;

    p = base;
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutU16(&p, 8); /* horiz Axis */
    PutU16(&p, 0);
    PutU16(&p, 0); /* BaseTagList offset stub */
    PutU16(&p, 0); /* BaseScriptList */
    span.data = base;
    span.size = 12;
    RoboCheckEqInt(FontBaseParse(span, &b), RoboOk);
    RoboCheck(b.hasHoriz);

    p = math;
    PutU32(&p, 0x00010000u);
    PutU16(&p, 10);
    PutU16(&p, 10);
    PutU16(&p, 10);
    span.data = math;
    span.size = 10;
    RoboCheckEqInt(FontMathParse(span, &m), RoboOk);

    p = cpal;
    PutU16(&p, 0);
    PutU16(&p, 1); /* entries per palette */
    PutU16(&p, 1); /* num palettes */
    PutU16(&p, 1); /* num color records */
    PutU32(&p, 14); /* color records offset */
    PutU16(&p, 0);  /* palette 0 first color index */
    PutU32(&p, 0xFF0000FFu); /* BGRA */
    span.data = cpal;
    span.size = 18;
    RoboCheckEqInt(FontCpalParse(span, &cp), RoboOk);
    RoboCheckEqInt(cp.numPalette, 1);

    p = colr;
    PutU16(&p, 0);
    PutU16(&p, 1);
    PutU32(&p, 14);
    PutU32(&p, 20);
    PutU16(&p, 1);
    /* base glyph record */
    PutU16(&p, 5);
    PutU16(&p, 0);
    PutU16(&p, 1);
    /* layer */
    PutU16(&p, 6);
    PutU16(&p, 0);
    span.data = colr;
    span.size = 24;
    RoboCheckEqInt(FontColrParse(span, &co), RoboOk);

    p = stat;
    PutU16(&p, 1);
    PutU16(&p, 1);
    PutU16(&p, 8); /* axis size */
    PutU16(&p, 1);
    PutU32(&p, 20); /* axes offset */
    PutU16(&p, 0);
    PutU32(&p, 0);
    PutU16(&p, 1); /* elidedFallbackNameID */
    /* axis record at 20 */
    while ((size_t)(p - stat) < 20u) {
        *p++ = 0;
    }
    p = stat + 20;
    PutU32(&p, RoboFontTag('w', 'g', 'h', 't'));
    PutU16(&p, 256);
    PutU16(&p, 0);
    span.data = stat;
    span.size = 28;
    memset(&st, 0, sizeof(st));
    RoboCheckEqInt(FontStatParse(span, &st), RoboOk);
    RoboCheckEqInt(st.designAxisCount, 1);
    RoboCheckEqInt((int)st.axes[0].tag, (int)RoboFontTag('w', 'g', 'h', 't'));
    FontStatClear(&st);

    p = kern;
    PutU16(&p, 0);
    PutU16(&p, 1);
    PutU16(&p, 0);  /* subtable version */
    PutU16(&p, 8);  /* length */
    PutU16(&p, 1);  /* coverage */
    PutU16(&p, 0);  /* pad to length 8 from subtable start: version+len+cov = 6, need 2 more */
    span.data = kern;
    span.size = 12;
    memset(&k, 0, sizeof(k));
    RoboCheckEqInt(FontKernParse(span, &k), RoboOk);
    RoboCheckEqInt(k.nTables, 1);
    FontKernClear(&k);
}

static void TestOtIntegratedBundle(void)
{
    uint8_t gsub[256];
    uint8_t cmap[64];
    uint8_t *cp;
    size_t gn;
    size_t cmapN;
    SynthTable tabs[2];
    uint8_t font[1024];
    size_t n;
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    FontOt *ot = NULL;

    gn = BuildMinGsub(gsub, sizeof(gsub));
    cp = cmap;
    PutU16(&cp, 0);
    PutU16(&cp, 1);
    PutU16(&cp, 0);
    PutU16(&cp, 0);
    PutU32(&cp, 12);
    PutU16(&cp, 0); /* format 0 */
    PutU16(&cp, 262); /* length for format 0 is typically 262 */
    PutU16(&cp, 0);
    /* Don't claim full 262 — use truncated carefully: length must fit */
    {
        /* Use format 4 minimal instead */
        uint8_t *q = cmap;
        PutU16(&q, 0);
        PutU16(&q, 1);
        PutU16(&q, 3);
        PutU16(&q, 1);
        PutU32(&q, 12);
        PutU16(&q, 4);
        PutU16(&q, 16);
        PutU16(&q, 0);
        memset(q, 0, 10);
        cmapN = 28;
    }

    tabs[0].tag = RoboFontTagGsub;
    tabs[0].data = gsub;
    tabs[0].length = (uint32_t)gn;
    tabs[1].tag = RoboFontTagCmap;
    tabs[1].data = cmap;
    tabs[1].length = (uint32_t)cmapN;
    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionOtto, tabs, 2);

    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontSfntParse(src, 0, 0, &sfnt), RoboOk);
    RoboCheckEqInt(FontOtParse(sfnt, RoboFontOtFlagsParseAll, &ot), RoboOk);
    RoboCheck(FontOtGsub(ot) != NULL);
    RoboCheck(FontOtCmap(ot) != NULL);
    RoboCheckEqInt(FontOtCmap(ot)->encodings[0].format, 4);
    RoboCheckEqInt(FontOtFuzzParse(font, n), RoboOk);

    FontOtRelease(ot);
    FontSfntRelease(sfnt);
    FontSourceRelease(src);
}

static void TestOtMalformed(void)
{
    uint8_t bad[8] = {0};
    FontSpan span = {bad, 8};
    FontOtLayout L;
    FontOtCoverage c;
    memset(&L, 0, sizeof(L));
    memset(&c, 0, sizeof(c));
    RoboCheckEqInt(FontGsubParse(span, &L), RoboParseError);
    RoboCheckEqInt(FontOtCoverageParse(span, 0, &c), RoboParseError);
    RoboCheck(FontOtFuzzParse((const uint8_t *)"xxxx", 4) != RoboOk);
    RoboCheckEqInt(FontOtResolveOffset16(span, 0, 100, NULL),
                      RoboInvalidArgument);
    {
        size_t abs = 0;
        RoboCheckEqInt(FontOtResolveOffset16(span, 0, 100, &abs),
                          RoboParseError);
    }
}

void TestRegisterFontOt(void)
{
    TestRegister("font.ot.outline", TestOtOutlineDetect);
    TestRegister("font.ot.coverage_class", TestOtCoverageClassdef);
    TestRegister("font.ot.cmap", TestOtCmapStructural);
    TestRegister("font.ot.layout", TestOtGsubGposGdef);
    TestRegister("font.ot.extra", TestOtExtraTables);
    TestRegister("font.ot.bundle", TestOtIntegratedBundle);
    TestRegister("font.ot.malformed", TestOtMalformed);
}
