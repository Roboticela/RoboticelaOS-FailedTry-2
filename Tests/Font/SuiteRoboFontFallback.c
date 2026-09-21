/*
 * RoboFont fallback suite (Goal 5.15).
 * Synthetic registry fixtures — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboMem/RoboMem.h"
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
static void PutI64(uint8_t **p, int64_t v)
{
    PutU32(p, (uint32_t)((uint64_t)v >> 32));
    PutU32(p, (uint32_t)((uint64_t)v));
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
    PutI16(&p, 0); /* indexToLocFormat short */
    PutI16(&p, 0);
    return (size_t)(p - buf);
}

static size_t BuildHhea(uint8_t *buf)
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
    PutU16(&p, 2); /* numberOfHMetrics — two glyphs */
    return (size_t)(p - buf);
}

static size_t BuildMaxp(uint8_t *buf)
{
    uint8_t *p = buf;
    int i;
    PutU32(&p, 0x00010000u);
    PutU16(&p, 2); /* .notdef + one mapped glyph */
    for (i = 0; i < 13; i++) {
        PutU16(&p, 0);
    }
    return (size_t)(p - buf);
}

static size_t BuildHmtx(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU16(&p, 500);
    PutI16(&p, 0);
    PutU16(&p, 500);
    PutI16(&p, 0);
    return (size_t)(p - buf);
}

static size_t BuildLoca(uint8_t *buf)
{
    uint8_t *p = buf;
    /* short format: numGlyphs+1 offsets */
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 0);
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

static size_t BuildOs2(uint8_t *buf, uint16_t weight)
{
    uint8_t *p = buf;
    int i;
    PutU16(&p, 4);
    PutI16(&p, 500);
    PutU16(&p, weight);
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

/* Format-4 single BMP mapping. */
static size_t BuildCmapFmt4(uint8_t *buf, uint16_t cp, uint16_t gid)
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

static size_t BuildFontWithCp(uint8_t *out, size_t cap, const char *family,
                                 const char *ps, uint16_t weight, uint16_t cp,
                                 uint16_t gid)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[16], loca[16], post[64], os2[128];
    uint8_t name[512], cmap[128];
    SynthTable tabs[10];
    uint32_t nt = 0u;
    size_t nn = BuildNameTable(name, sizeof(name), family, ps);
    size_t nc;
    if (nn == 0u) {
        return 0u;
    }
    if (gid == 0u || gid > 1u) {
        /* Fixture only models .notdef + glyph 1. */
        gid = 1u;
    }
    nc = BuildCmapFmt4(cmap, cp, gid);
    tabs[nt++] = (SynthTable){RoboFontTagHead, head, (uint32_t)BuildHead(head)};
    tabs[nt++] = (SynthTable){RoboFontTagHhea, hhea, (uint32_t)BuildHhea(hhea)};
    tabs[nt++] = (SynthTable){RoboFontTagMaxp, maxp, (uint32_t)BuildMaxp(maxp)};
    tabs[nt++] = (SynthTable){RoboFontTagHmtx, hmtx, (uint32_t)BuildHmtx(hmtx)};
    tabs[nt++] = (SynthTable){RoboFontTagLoca, loca, (uint32_t)BuildLoca(loca)};
    tabs[nt++] = (SynthTable){RoboFontTagGlyf, head, 0u};
    tabs[nt++] = (SynthTable){RoboFontTagPost, post, (uint32_t)BuildPost(post)};
    tabs[nt++] =
        (SynthTable){RoboFontTagOs2, os2, (uint32_t)BuildOs2(os2, weight)};
    tabs[nt++] = (SynthTable){RoboFontTagName, name, (uint32_t)nn};
    tabs[nt++] = (SynthTable){RoboFontTagCmap, cmap, (uint32_t)nc};
    return BuildSfnt(out, cap, tabs, nt);
}

static FontFace *FaceAt(FontRegistry *reg, uint32_t index)
{
    const FontRegistration *rec = NULL;
    if (FontRegistryRegistrationAt(reg, index, &rec) != RoboOk || !rec) {
        return NULL;
    }
    return rec->face;
}

static void TestPrimaryCoversNoFallback(void)
{
    uint8_t font[4096];
    size_t n;
    FontRegistry *reg = NULL;
    FontDiscoverySourceId sid = 0u;
    FontDiscoveryResult dr;
    FontFallbackResolver *fb = NULL;
    FontFallbackRequest req;
    FontFallbackResult res;
    uint32_t cp = 0x41u;

    n = BuildFontWithCp(font, sizeof(font), "LatinFace", "LatinFace", 400, 0x41u, 1u);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    RoboCheckEqInt(
        FontRegistryAddMemorySource(reg, "mem:latin", font, n, &sid), RoboOk);
    RoboCheckEqInt(FontRegistryScanSource(reg, sid, 0u, &dr), RoboOk);
    RoboCheckEqInt(FontFallbackResolverCreate(reg, NULL, &fb), RoboOk);

    FontFallbackRequestInit(&req);
    req.primary = FaceAt(reg, 0u);
    req.codepoints = &cp;
    req.codepointCount = 1u;
    FontFallbackResultInit(&res);
    RoboCheckEqInt(FontFallbackResolve(fb, &req, &res), RoboOk);
    RoboCheckEqInt(res.found, 1);
    RoboCheckEqInt(res.usedPrimary, 1);
    RoboCheckEqInt(res.stage, RoboFontFallbackStagePrimary);
    RoboCheck(res.face == req.primary);
    FontFallbackResultClear(&res);
    FontFallbackResolverDestroy(fb);
    FontRegistryDestroy(reg);
}

static void TestScriptFallbackArabic(void)
{
    uint8_t latin[4096], arab[4096];
    size_t nl, na;
    FontRegistry *reg = NULL;
    FontDiscoverySourceId s1 = 0u, s2 = 0u;
    FontDiscoveryResult dr;
    FontFallbackResolver *fb = NULL;
    FontFallbackRequest req;
    FontFallbackResult res;
    uint32_t cp = 0x0627u; /* Arabic Alef */
    FontFace *primary;
    FontFace *arabicFace;

    nl = BuildFontWithCp(latin, sizeof(latin), "OnlyLatin", "OnlyLatin", 400, 0x41u, 1u);
    na = BuildFontWithCp(arab, sizeof(arab), "ArabScript", "ArabScript", 400, 0x0627u,
                            1u);
    RoboCheck(nl > 0u && na > 0u);

    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    RoboCheckEqInt(
        FontRegistryAddMemorySource(reg, "mem:latin", latin, nl, &s1), RoboOk);
    RoboCheckEqInt(
        FontRegistryAddMemorySource(reg, "mem:arab", arab, na, &s2), RoboOk);
    RoboCheckEqInt(FontRegistryScanAll(reg, 0u, &dr), RoboOk);
    RoboCheck((int)FontRegistryRegistrationCount(reg) >= 2);

    primary = FaceAt(reg, 0u);
    arabicFace = FaceAt(reg, 1u);
    /* Order may vary — pick by family. */
    {
        uint32_t i;
        for (i = 0u; i < FontRegistryRegistrationCount(reg); i++) {
            const FontRegistration *rec = NULL;
            RoboCheckEqInt(FontRegistryRegistrationAt(reg, i, &rec), RoboOk);
            if (rec && strcmp(rec->family, "OnlyLatin") == 0) {
                primary = rec->face;
            }
            if (rec && strcmp(rec->family, "ArabScript") == 0) {
                arabicFace = rec->face;
            }
        }
    }
    RoboCheck(primary && arabicFace && primary != arabicFace);

    RoboCheckEqInt(FontFallbackResolverCreate(reg, NULL, &fb), RoboOk);
    FontFallbackRequestInit(&req);
    req.primary = primary;
    req.codepoints = &cp;
    req.codepointCount = 1u;
    req.hasScript = 1;
    req.script = RoboUScriptArabic;
    FontFallbackResultInit(&res);
    RoboCheckEqInt(FontFallbackResolve(fb, &req, &res), RoboOk);
    RoboCheckEqInt(res.found, 1);
    RoboCheckEqInt(res.usedPrimary, 0);
    RoboCheck(res.face == arabicFace);
    RoboCheck(res.stage == RoboFontFallbackStageScript ||
               res.stage == RoboFontFallbackStageGeneral);
    FontFallbackResultClear(&res);
    FontFallbackResolverDestroy(fb);
    FontRegistryDestroy(reg);
}

static void TestPreferredAndStyleAndNone(void)
{
    uint8_t a[4096], b[4096], c[4096];
    size_t na, nb, nc;
    FontRegistry *reg = NULL;
    FontDiscoverySourceId s1 = 0u, s2 = 0u, s3 = 0u;
    FontDiscoveryResult dr;
    FontFallbackPolicy pol;
    FontFallbackResolver *fb = NULL;
    FontFallbackRequest req;
    FontFallbackResult res;
    uint32_t cp = 0x0410u; /* Cyrillic A */
    const char *prefs[] = {"CyrilPref"};
    FontFace *primary = NULL;
    FontFace *pref = NULL;
    uint32_t i;

    na = BuildFontWithCp(a, sizeof(a), "PrimaryLat", "PrimaryLat", 600, 0x41u, 1u);
    nb = BuildFontWithCp(b, sizeof(b), "CyrilPref", "CyrilPref", 600, 0x0410u, 1u);
    nc = BuildFontWithCp(c, sizeof(c), "CyrilOther", "CyrilOther", 400, 0x0410u, 1u);
    RoboCheck(na && nb && nc);

    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    RoboCheckEqInt(FontRegistryAddMemorySource(reg, "m1", a, na, &s1),
                      RoboOk);
    RoboCheckEqInt(FontRegistryAddMemorySource(reg, "m2", b, nb, &s2),
                      RoboOk);
    RoboCheckEqInt(FontRegistryAddMemorySource(reg, "m3", c, nc, &s3),
                      RoboOk);
    RoboCheckEqInt(FontRegistryScanAll(reg, 0u, &dr), RoboOk);

    for (i = 0u; i < FontRegistryRegistrationCount(reg); i++) {
        const FontRegistration *rec = NULL;
        RoboCheckEqInt(FontRegistryRegistrationAt(reg, i, &rec), RoboOk);
        if (!strcmp(rec->family, "PrimaryLat")) {
            primary = rec->face;
        }
        if (!strcmp(rec->family, "CyrilPref")) {
            pref = rec->face;
        }
    }
    RoboCheck(primary && pref);

    FontFallbackPolicyInit(&pol);
    pol.preferredFamilies = prefs;
    pol.preferredFamilyCount = 1u;
    RoboCheckEqInt(FontFallbackResolverCreate(reg, &pol, &fb), RoboOk);

    FontFallbackRequestInit(&req);
    req.primary = primary;
    req.codepoints = &cp;
    req.codepointCount = 1u;
    req.weight = 600;
    FontFallbackResultInit(&res);
    RoboCheckEqInt(FontFallbackResolve(fb, &req, &res), RoboOk);
    RoboCheckEqInt(res.found, 1);
    RoboCheck(res.face == pref);
    RoboCheckEqInt(res.stage, RoboFontFallbackStagePreferred);
    FontFallbackResultClear(&res);

    /* No fallback for uncovered scalar. */
    {
        uint32_t missing = 0x0E01u; /* Thai — none of our fonts */
        req.codepoints = &missing;
        RoboCheckEqInt(FontFallbackResolve(fb, &req, &res), RoboOk);
        RoboCheckEqInt(res.found, 0);
        RoboCheckEqInt(res.stage, RoboFontFallbackStageNone);
        RoboCheck(res.face == NULL);
        FontFallbackResultClear(&res);
    }

    FontFallbackResolverDestroy(fb);
    FontRegistryDestroy(reg);
}

static void TestDeterminismCacheInvalidate(void)
{
    uint8_t a[4096], b[4096];
    size_t na, nb;
    FontRegistry *reg = NULL;
    FontDiscoverySourceId s1 = 0u, s2 = 0u;
    FontDiscoveryResult dr;
    FontFallbackResolver *fb = NULL;
    FontFallbackRequest req;
    FontFallbackResult r1, r2;
    uint32_t cp = 0x0391u; /* Greek */
    FontFace *primary = NULL;
    FontFaceId win1, win2;
    FontFallbackCacheStats st;
    uint32_t i;

    na = BuildFontWithCp(a, sizeof(a), "P", "P", 400, 0x41u, 1u);
    nb = BuildFontWithCp(b, sizeof(b), "G", "G", 400, 0x0391u, 1u);
    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    /* Add in opposite order across two runs conceptually — same registry both. */
    RoboCheckEqInt(FontRegistryAddMemorySource(reg, "b", b, nb, &s2), RoboOk);
    RoboCheckEqInt(FontRegistryAddMemorySource(reg, "a", a, na, &s1), RoboOk);
    RoboCheckEqInt(FontRegistryScanAll(reg, 0u, &dr), RoboOk);

    for (i = 0u; i < FontRegistryRegistrationCount(reg); i++) {
        const FontRegistration *rec = NULL;
        RoboCheckEqInt(FontRegistryRegistrationAt(reg, i, &rec), RoboOk);
        if (!strcmp(rec->family, "P")) {
            primary = rec->face;
        }
    }
    RoboCheck(primary);

    RoboCheckEqInt(FontFallbackResolverCreate(reg, NULL, &fb), RoboOk);
    FontFallbackRequestInit(&req);
    req.primary = primary;
    req.codepoints = &cp;
    req.codepointCount = 1u;

    FontFallbackResultInit(&r1);
    RoboCheckEqInt(FontFallbackResolve(fb, &req, &r1), RoboOk);
    RoboCheckEqInt(r1.found, 1);
    win1 = r1.faceId;
    FontFallbackResultClear(&r1);

    FontFallbackResultInit(&r2);
    RoboCheckEqInt(FontFallbackResolve(fb, &req, &r2), RoboOk);
    RoboCheckEqInt(r2.fromCache, 1);
    win2 = r2.faceId;
    RoboCheckEqInt((int)win1, (int)win2);
    FontFallbackResultClear(&r2);

    RoboCheckEqInt(FontRegistryRemoveDiscoverySource(reg, s2), RoboOk);
    /* Generation bumped → cache miss / recompute → no fallback. */
    RoboCheckEqInt(FontFallbackResolve(fb, &req, &r1), RoboOk);
    RoboCheckEqInt(r1.found, 0);
    FontFallbackResultClear(&r1);

    FontFallbackResolverCacheStats(fb, &st);
    RoboCheck(st.hits >= 1u);

    /* Bench: primary-hit path. */
    {
        uint32_t ascii = 0x41u;
        volatile int hits = 0;
        req.codepoints = &ascii;
        for (i = 0u; i < 5000u; i++) {
            FontFallbackResultInit(&r1);
            RoboCheckEqInt(FontFallbackResolve(fb, &req, &r1), RoboOk);
            hits += r1.usedPrimary;
            FontFallbackResultClear(&r1);
        }
        RoboCheck(hits == 5000);
        printf("font.fallback.bench.primary_hits=%d\n", hits);
    }

    FontFallbackResolverDestroy(fb);
    FontRegistryDestroy(reg);
}

static void TestSequenceAndLanguageAndFuzz(void)
{
    uint8_t a[4096], b[4096];
    size_t na, nb;
    FontRegistry *reg = NULL;
    FontDiscoverySourceId s1 = 0u, s2 = 0u;
    FontDiscoveryResult dr;
    FontFallbackPolicy pol;
    FontFallbackResolver *fb = NULL;
    FontFallbackRequest req;
    FontFallbackResult res;
    uint32_t seq[2] = {0x0627u, 0x064Bu}; /* alef + fatha — only first in font B */
    const char *langPrefs[] = {"LangArab"};
    FontFace *primary = NULL;
    FontFace *langFace = NULL;
    uint32_t i;
    static const uint8_t junk[] = {0x01, 0x41, 0xFF, 0x00};

    na = BuildFontWithCp(a, sizeof(a), "Base", "Base", 400, 0x41u, 1u);
    nb = BuildFontWithCp(b, sizeof(b), "LangArab", "LangArab", 400, 0x0627u, 1u);
    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    RoboCheckEqInt(FontRegistryAddMemorySource(reg, "a", a, na, &s1), RoboOk);
    RoboCheckEqInt(FontRegistryAddMemorySource(reg, "b", b, nb, &s2), RoboOk);
    RoboCheckEqInt(FontRegistryScanAll(reg, 0u, &dr), RoboOk);
    for (i = 0u; i < FontRegistryRegistrationCount(reg); i++) {
        const FontRegistration *rec = NULL;
        RoboCheckEqInt(FontRegistryRegistrationAt(reg, i, &rec), RoboOk);
        if (!strcmp(rec->family, "Base")) {
            primary = rec->face;
        }
        if (!strcmp(rec->family, "LangArab")) {
            langFace = rec->face;
        }
    }

    FontFallbackPolicyInit(&pol);
    pol.languagePreferredFamilies = langPrefs;
    pol.languagePreferredFamilyCount = 1u;
    /* REQUIRE_ALL: sequence fails because mark missing → no fallback. */
    RoboCheckEqInt(FontFallbackResolverCreate(reg, &pol, &fb), RoboOk);
    FontFallbackRequestInit(&req);
    req.primary = primary;
    req.codepoints = seq;
    req.codepointCount = 2u;
    req.language = "ar";
    FontFallbackResultInit(&res);
    RoboCheckEqInt(FontFallbackResolve(fb, &req, &res), RoboOk);
    RoboCheckEqInt(res.found, 0); /* mark not covered */
    FontFallbackResultClear(&res);

    /* Single base with language hint → LangArab. */
    req.codepointCount = 1u;
    RoboCheckEqInt(FontFallbackResolve(fb, &req, &res), RoboOk);
    RoboCheckEqInt(res.found, 1);
    RoboCheck(res.face == langFace);
    FontFallbackResultClear(&res);

    RoboCheckEqInt(FontFallbackFuzzParse(junk, sizeof(junk)), RoboOk);
    RoboCheckEqInt(FontFallbackFuzzParse(NULL, 0u), RoboOk);

    FontFallbackResolverDestroy(fb);
    FontRegistryDestroy(reg);
}

void TestRegisterFontFallback(void)
{
    TestRegister("font.fallback.primary_hit", TestPrimaryCoversNoFallback);
    TestRegister("font.fallback.script_arabic", TestScriptFallbackArabic);
    TestRegister("font.fallback.preferred_none", TestPreferredAndStyleAndNone);
    TestRegister("font.fallback.cache_determinism",
                       TestDeterminismCacheInvalidate);
    TestRegister("font.fallback.sequence_lang_fuzz",
                       TestSequenceAndLanguageAndFuzz);
}
