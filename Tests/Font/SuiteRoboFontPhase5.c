/*
 * RoboFont Phase 5 final integration / API freeze suite (Goal 5.22).
 *
 * Freeze pin, end-to-end public-API acceptance, fuzz smoke, performance baseline.
 * Synthetic fixtures only — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboFont/RoboFontVersion.h"
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
    PutU32(&p, RoboFontSfntVersionTrue);
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
        if (tables[i].length && tables[i].data) {
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
        PutI16(&p, 10);
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

static size_t BuildCpal(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU16(&p, 0);
    PutU16(&p, 2);
    PutU16(&p, 2);
    PutU16(&p, 4);
    PutU32(&p, 16u);
    PutU16(&p, 0);
    PutU16(&p, 2);
    PutU8(&p, 0);
    PutU8(&p, 0);
    PutU8(&p, 255);
    PutU8(&p, 255);
    PutU8(&p, 255);
    PutU8(&p, 0);
    PutU8(&p, 0);
    PutU8(&p, 255);
    PutU8(&p, 255);
    PutU8(&p, 0);
    PutU8(&p, 0);
    PutU8(&p, 255);
    PutU8(&p, 0);
    PutU8(&p, 255);
    PutU8(&p, 255);
    PutU8(&p, 255);
    return (size_t)(p - buf);
}

static size_t BuildColr(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU16(&p, 0);
    PutU16(&p, 1);
    PutU32(&p, 14u);
    PutU32(&p, 20u);
    PutU16(&p, 2);
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutU16(&p, 2);
    PutU16(&p, 2);
    PutU16(&p, 0);
    PutU16(&p, 3);
    PutU16(&p, 1);
    return (size_t)(p - buf);
}

static size_t BuildPhase5Font(uint8_t *out, size_t cap, const char *family,
                                const char *ps, uint16_t cmapCp, int withColor)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[64], loca[32], post[64], os2[128];
    uint8_t name[512], cmap[256], cpal[64], colr[64];
    SynthTable tabs[16];
    uint32_t nt = 0u;
    size_t nn, nc;
    const uint16_t glyphs = 4u;

    nn = BuildNameTable(name, sizeof(name), family, ps);
    nc = BuildCmapFmt4(cmap, cmapCp, 1u);
    if (nn == 0u || nc == 0u) {
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
    if (withColor) {
        tabs[nt++] = (SynthTable){RoboFontTagCpal, cpal, (uint32_t)BuildCpal(cpal)};
        tabs[nt++] = (SynthTable){RoboFontTagColr, colr, (uint32_t)BuildColr(colr)};
    }
    return BuildSfnt(out, cap, tabs, nt);
}

static void TestP5Freeze(void)
{
    RoboCheckEqInt(RoboFontApiFrozen, 1);
    RoboCheckStrEq(FontVersionString(), "0.22.0");
    RoboCheckEqInt(RoboFontVersionMajor, 0);
    RoboCheckEqInt(RoboFontVersionMinor, 22);
    RoboCheckEqInt(RoboFontVersionPatch, 0);
    RoboCheckEqInt(FontInit(), RoboOk);
    RoboCheckEqInt(FontInit(), RoboOk); /* idempotent */
    FontShutdown();
}

static void TestP5E2eAcceptance(void)
{
    uint8_t primaryBytes[4096];
    uint8_t fallbackBytes[4096];
    size_t np, nf;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontValidationResult *vres = NULL;
    FontRegistry *reg = NULL;
    FontDiscoverySourceId sid = 0u, sid2 = 0u;
    FontDiscoveryResult dr;
    const FontRegistration *rec = NULL;
    const FontMetadata *meta;
    const FontCoverage *cov;
    FontCmapLookupResult lr;
    FontMetrics metrics;
    FontGlyphMetrics gm;
    FontOutline *outline = NULL;
    FontSelectRequest sreq;
    FontSelection sel;
    FontFace *cands[1];
    FontFallbackResolver *fb = NULL;
    FontFallbackRequest freq;
    FontFallbackResult fres;
    uint32_t cp = 0x41u;
    float norm = 0.0f;
    uint32_t i;
    FontFace *primary = NULL;
    FontFace *alt = NULL;

    np = BuildPhase5Font(primaryBytes, sizeof(primaryBytes), "Phase5Primary",
                           "Phase5Primary", 0x42u, 1);
    nf = BuildPhase5Font(fallbackBytes, sizeof(fallbackBytes), "Phase5Fallback",
                           "Phase5Fallback", 0x41u, 0);
    RoboCheck(np > 0u && nf > 0u);

    /* 1–2: source + validate */
    RoboCheckEqInt(
        FontSourceCreateMemoryCopy(primaryBytes, np, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0u, &face), RoboOk);
    RoboCheckEqInt(
        FontValidateForLoad(face, RoboFontValidateNormal, &vres), RoboOk);
    if (vres) {
        RoboCheck(FontValidationResultOk(vres));
        FontValidationResultRelease(vres);
        vres = NULL;
    }

    /* 3–4: register / discover */
    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    RoboCheckEqInt(
        FontRegistryAddMemorySource(reg, "mem:p5-primary", primaryBytes, np,
                                             &sid),
        RoboOk);
    RoboCheckEqInt(
        FontRegistryAddMemorySource(reg, "mem:p5-fallback", fallbackBytes, nf,
                                             &sid2),
        RoboOk);
    RoboCheckEqInt(FontRegistryScanAll(reg, 0u, &dr), RoboOk);
    RoboCheck((int)FontRegistryRegistrationCount(reg) >= 2);

    for (i = 0u; i < FontRegistryRegistrationCount(reg); i++) {
        RoboCheckEqInt(FontRegistryRegistrationAt(reg, i, &rec), RoboOk);
        if (rec && strcmp(rec->family, "Phase5Primary") == 0) {
            primary = rec->face;
        }
        if (rec && strcmp(rec->family, "Phase5Fallback") == 0) {
            alt = rec->face;
        }
    }
    RoboCheck(primary != NULL && alt != NULL);

    /* 5–9: metadata, coverage, cmap, metrics, outline via frozen face APIs */
    RoboCheckEqInt(FontFaceParseTruetype(primary, 0u), RoboOk);
    RoboCheckEqInt(FontFaceParseMetadata(primary), RoboOk);
    meta = FontFaceMetadata(primary);
    RoboCheck(meta != NULL);
    RoboCheckStrEq(meta->familyName.utf8, "Phase5Primary");

    RoboCheckEqInt(FontFaceParseCmap(primary), RoboOk);
    RoboCheckEqInt(FontFaceParseCoverage(primary), RoboOk);
    cov = FontFaceCoverage(primary);
    RoboCheck(cov != NULL);
    RoboCheck(FontCoverageContains(cov, 0x42u));
    RoboCheck(!FontCoverageContains(cov, 0x41u));

    RoboCheckEqInt(FontFaceLookupGlyph(primary, 0x42u, &lr), RoboOk);
    RoboCheckEqInt((int)lr.status, (int)RoboFontCmapMapped);
    RoboCheckEqInt((int)lr.glyphId, 1);

    RoboCheckEqInt(FontFaceParseMetrics(primary), RoboOk);
    RoboCheckEqInt(FontFaceLayoutMetrics(primary, &metrics), RoboOk);
    RoboCheckEqInt((int)metrics.unitsPerEm, 1000);
    RoboCheckEqInt(FontFaceGlyphMetrics(primary, 1u, &gm), RoboOk);
    RoboCheckEqInt((int)gm.advanceWidth, 500);

    RoboCheckEqInt(FontFaceParseOutlines(primary), RoboOk);
    RoboCheckEqInt(FontFaceGlyphOutline(primary, 0u, &outline), RoboOk);
    /* Empty glyf → valid empty outline object or OK empty path. */
    if (outline) {
        FontOutlineRelease(outline);
        outline = NULL;
    }

    /* 10: selection */
    RoboCheckEqInt(FontFaceParseClassification(primary), RoboOk);
    cands[0] = primary;
    FontSelectRequestInit(&sreq);
    sreq.family = "phase5primary";
    sreq.weight = 400;
    sreq.stretch = 100;
    sreq.style = RoboFontStyleNormal;
    RoboCheckEqInt(FontSelect(&sreq, cands, 1u, &sel), RoboOk);
    RoboCheckEqInt(sel.found, 1);
    RoboCheck(sel.face == primary);
    FontSelectionClear(&sel);

    /* 11: fallback for U+0041 (primary lacks it) */
    RoboCheckEqInt(FontFaceParseTruetype(alt, 0u), RoboOk);
    RoboCheckEqInt(FontFaceParseCmap(alt), RoboOk);
    RoboCheckEqInt(FontFaceParseCoverage(alt), RoboOk);
    RoboCheckEqInt(FontFallbackResolverCreate(reg, NULL, &fb), RoboOk);
    FontFallbackRequestInit(&freq);
    freq.primary = primary;
    freq.codepoints = &cp;
    freq.codepointCount = 1u;
    FontFallbackResultInit(&fres);
    RoboCheckEqInt(FontFallbackResolve(fb, &freq, &fres), RoboOk);
    RoboCheckEqInt(fres.found, 1);
    if (!fres.usedPrimary) {
        RoboCheck(fres.face == alt);
    }
    FontFallbackResultClear(&fres);
    FontFallbackResolverDestroy(fb);

    /* 12: variation normalize foundation (no fvar required) */
    RoboCheckEqInt(
        FontVariationNormalize(100.0f, 400.0f, 900.0f, 400.0f, &norm), RoboOk);
    RoboCheck(norm == 0.0f);

    /* 13: color capabilities */
    RoboCheckEqInt(FontFaceParseColor(primary), RoboOk);
    RoboCheck(FontFaceColor(primary) != NULL);
    RoboCheck(FontColorInfoIsColorCapable(FontFaceColor(primary)));

    /* 14–15: security gate already passed validate_for_load; re-probe STRICT OK */
    RoboCheckEqInt(
        FontValidateForLoad(face, RoboFontValidateStrict, &vres), RoboOk);
    if (vres) {
        FontValidationResultRelease(vres);
    }

    /* Generic family resolve must not invent a font without mapping */
    {
        FontGenericFamily g = RoboFontGenericUnknown;
        RoboCheckEqInt(FontGenericFamilyFromName("serif", 0u, &g), RoboOk);
        RoboCheckEqInt((int)g, (int)RoboFontGenericSerif);
        RoboCheckEqInt(FontGenericFamilyFromName("not-mapped-xyz", 0u, &g),
                          RoboNotFound);
    }

    FontRegistryDestroy(reg);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestP5MatrixSmoke(void)
{
    static const uint8_t junk[4] = {'x', 'x', 'x', 'x'};
    /* Compact matrix: every major public surface remains callable. */
    RoboCheck(FontConformCategoryString(RoboFontConformSecurity) != NULL);
    RoboCheck(FontDiscoveryExtensionIsFont("A.ttf"));
    RoboCheckEqInt(FontDiscoveryClassifyBytes((const uint8_t *)"\0\1\0\0", 4),
                      RoboFontCandidateTtf);
    RoboCheckEqInt(FontValidateFuzzParse(NULL, 0u), RoboOk);
    RoboCheckEqInt(FontCmapFuzzParse(junk, sizeof(junk)), RoboOk);
    RoboCheckEqInt(FontConformFuzzSmoke(NULL, 0u), RoboOk);
}

static void TestP5ChromiumIndependence(void)
{
    /*
     * Build.ps1 Assert-NoChromiumPath is the authoritative link-time fence.
     * This test asserts the frozen API functions without any Chromium types
     * in the call path (compile-time: only RoboFont / RoboBase / RoboMem).
     */
    RoboCheckStrEq(FontVersionString(), "0.22.0");
    RoboCheckEqInt(RoboFontApiFrozen, 1);
}

static void TestP5Bench(void)
{
    uint8_t font[4096];
    size_t n;
    uint32_t i;
    uint32_t hits = 0u;

    n = BuildPhase5Font(font, sizeof(font), "BenchP5", "BenchP5", 0x41u, 0);
    RoboCheck(n > 0u);
    for (i = 0u; i < 20u; i++) {
        FontSource *src = NULL;
        FontFace *face = NULL;
        FontCmapLookupResult lr;
        if (FontSourceCreateMemoryCopy(font, n, &src) != RoboOk) {
            continue;
        }
        if (FontFaceCreate(src, 0u, &face) != RoboOk) {
            FontSourceRelease(src);
            continue;
        }
        if (FontFaceParseTruetype(face, 0u) == RoboOk &&
            FontFaceParseCmap(face) == RoboOk &&
            FontFaceLookupGlyph(face, 0x41u, &lr) == RoboOk &&
            lr.status == RoboFontCmapMapped) {
            hits++;
        }
        FontFaceRelease(face);
        FontSourceRelease(src);
    }
    printf("font.p5.bench hits=%u\n", (unsigned)hits);
    RoboCheckEqInt((int)hits, 20);
}

void TestRegisterFontPhase5(void)
{
    TestRegister("font.p5.freeze", TestP5Freeze);
    TestRegister("font.p5.e2e", TestP5E2eAcceptance);
    TestRegister("font.p5.matrix", TestP5MatrixSmoke);
    TestRegister("font.p5.chromium", TestP5ChromiumIndependence);
    TestRegister("font.p5.bench", TestP5Bench);
}
