/*
 * RoboFont generic-family suite (Goal 5.16).
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
    PutI16(&p, 0);
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
    PutU16(&p, 2);
    return (size_t)(p - buf);
}

static size_t BuildMaxp(uint8_t *buf)
{
    uint8_t *p = buf;
    int i;
    PutU32(&p, 0x00010000u);
    PutU16(&p, 2);
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
                                 const char *ps, uint16_t weight, uint16_t cp)
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
    nc = BuildCmapFmt4(cmap, cp, 1u);
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

static void TestIdsAndNames(void)
{
    FontGenericFamily g = RoboFontGenericUnknown;
    unsigned i;
    RoboCheck(!FontGenericFamilyIsValid(RoboFontGenericUnknown));
    RoboCheck(!FontGenericFamilyIsValid(RoboFontGenericCount));
    for (i = 1u; i < (unsigned)RoboFontGenericCount; i++) {
        RoboCheck(FontGenericFamilyIsValid((FontGenericFamily)i));
        RoboCheck(FontGenericFamilyName((FontGenericFamily)i) != NULL);
        RoboCheckEqInt(
            FontGenericFamilyFromName(
                FontGenericFamilyName((FontGenericFamily)i), 0u, &g),
            RoboOk);
        RoboCheckEqInt((int)g, (int)i);
    }
    RoboCheckEqInt(FontGenericFamilyFromName("Sans-Serif", 0u, &g), RoboOk);
    RoboCheckEqInt((int)g, (int)RoboFontGenericSansSerif);
    RoboCheckEqInt(FontGenericFamilyFromName("nope", 0u, &g), RoboNotFound);
    RoboCheckEqInt((int)FontGenericRelated(RoboFontGenericUiSansSerif),
                      (int)RoboFontGenericSansSerif);
    RoboCheckEqInt((int)FontGenericRelated(RoboFontGenericSystemUi),
                      (int)RoboFontGenericUnknown);
    RoboCheckEqInt((int)FontGenericRelated(RoboFontGenericEmoji),
                      (int)RoboFontGenericUnknown);
    RoboCheckEqInt((int)FontGenericRelated(RoboFontGenericMath),
                      (int)RoboFontGenericUnknown);
}

static void TestConfigPrecedenceAndEmpty(void)
{
    FontRegistry *reg = NULL;
    FontGenericConfig *plat = NULL;
    FontGenericConfig *run = NULL;
    FontGenericConfig *app = NULL;
    FontGenericResolver *r = NULL;
    FontGenericResolverOptions opts;
    FontGenericRequest req;
    FontGenericResult res;
    const char *platF[] = {"PlatformSans"};
    const char *runF[] = {"RuntimeSans"};
    const char *appF[] = {"AppSans", "AppSansB"};
    uint64_t gen0;

    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    RoboCheckEqInt(FontGenericConfigCreateEmptyPlatform(&plat), RoboOk);
    RoboCheckEqInt(FontGenericConfigCreate(&run), RoboOk);
    RoboCheckEqInt(FontGenericConfigCreate(&app), RoboOk);

    RoboCheckEqInt(FontGenericConfigSetMapping(
                          plat, RoboFontGenericSansSerif, platF, 1u),
                      RoboOk);
    RoboCheckEqInt(
        FontGenericConfigSetMapping(run, RoboFontGenericSansSerif, runF, 1u),
        RoboOk);
    RoboCheckEqInt(
        FontGenericConfigSetMapping(app, RoboFontGenericSansSerif, appF, 2u),
        RoboOk);

    FontGenericResolverOptionsInit(&opts);
    opts.platform = plat;
    opts.runtime = run;
    opts.application = app;
    RoboCheckEqInt(FontGenericResolverCreate(reg, &opts, &r), RoboOk);

    FontGenericRequestInit(&req);
    req.generic = RoboFontGenericSansSerif;
    FontGenericResultInit(&res);
    RoboCheckEqInt(FontGenericResolve(r, &req, &res), RoboOk);
    RoboCheckEqInt(res.resolved, 1);
    RoboCheckEqInt((int)res.layer, (int)RoboFontGenericLayerApplication);
    RoboCheckEqInt((int)res.familyCount, 2);
    RoboCheckStrEq(res.families[0], "AppSans");
    RoboCheckStrEq(res.families[1], "AppSansB");
    gen0 = res.configGeneration;
    FontGenericResultClear(&res);

    /* Clear application → runtime wins. */
    RoboCheckEqInt(FontGenericResolverSetApplication(r, NULL), RoboOk);
    RoboCheckEqInt(FontGenericResolve(r, &req, &res), RoboOk);
    RoboCheckEqInt(res.resolved, 1);
    RoboCheckEqInt((int)res.layer, (int)RoboFontGenericLayerRuntime);
    RoboCheckStrEq(res.families[0], "RuntimeSans");
    RoboCheck(res.configGeneration != gen0);
    FontGenericResultClear(&res);

    RoboCheckEqInt(FontGenericResolverSetRuntime(r, NULL), RoboOk);
    RoboCheckEqInt(FontGenericResolve(r, &req, &res), RoboOk);
    RoboCheckEqInt(res.resolved, 1);
    RoboCheckEqInt((int)res.layer, (int)RoboFontGenericLayerPlatform);
    RoboCheckStrEq(res.families[0], "PlatformSans");
    FontGenericResultClear(&res);

    /* Unmapped generic → unresolved (no invented font). */
    req.generic = RoboFontGenericEmoji;
    RoboCheckEqInt(FontGenericResolve(r, &req, &res), RoboOk);
    RoboCheckEqInt(res.resolved, 0);
    RoboCheckEqInt((int)res.layer, (int)RoboFontGenericLayerNone);
    FontGenericResultClear(&res);

    /* Invalid generic. */
    req.generic = RoboFontGenericUnknown;
    RoboCheckEqInt(FontGenericResolve(r, &req, &res), RoboInvalidArgument);

    FontGenericResolverDestroy(r);
    FontGenericConfigRelease(plat);
    FontGenericConfigRelease(run);
    FontGenericConfigRelease(app);
    FontRegistryDestroy(reg);
}

static void TestRelatedAndSpecials(void)
{
    FontRegistry *reg = NULL;
    FontGenericConfig *cfg = NULL;
    FontGenericResolver *r = NULL;
    FontGenericResolverOptions opts;
    FontGenericRequest req;
    FontGenericResult res;
    const char *sans[] = {"UIBaseSans"};
    const char *sys[] = {"SystemUIFace"};
    const char *emoji[] = {"EmojiFace"};
    const char *math[] = {"MathFace"};
    const char *uiround[] = {"RoundFace"};

    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    RoboCheckEqInt(FontGenericConfigCreate(&cfg), RoboOk);
    RoboCheckEqInt(
        FontGenericConfigSetMapping(cfg, RoboFontGenericSansSerif, sans, 1u),
        RoboOk);
    RoboCheckEqInt(
        FontGenericConfigSetMapping(cfg, RoboFontGenericSystemUi, sys, 1u),
        RoboOk);
    RoboCheckEqInt(
        FontGenericConfigSetMapping(cfg, RoboFontGenericEmoji, emoji, 1u),
        RoboOk);
    RoboCheckEqInt(
        FontGenericConfigSetMapping(cfg, RoboFontGenericMath, math, 1u),
        RoboOk);
    RoboCheckEqInt(FontGenericConfigSetMapping(
                          cfg, RoboFontGenericUiRounded, uiround, 1u),
                      RoboOk);

    FontGenericResolverOptionsInit(&opts);
    opts.runtime = cfg;
    opts.flags = RoboFontGenericFlagAllowRelated | RoboFontGenericFlagSkipMissing;
    RoboCheckEqInt(FontGenericResolverCreate(reg, &opts, &r), RoboOk);

    FontGenericRequestInit(&req);
    req.generic = RoboFontGenericUiSansSerif; /* no direct map → related */
    req.flags = RoboFontGenericFlagAllowRelated;
    FontGenericResultInit(&res);
    RoboCheckEqInt(FontGenericResolve(r, &req, &res), RoboOk);
    RoboCheckEqInt(res.resolved, 1);
    RoboCheckEqInt(res.usedRelated, 1);
    RoboCheckEqInt((int)res.effective, (int)RoboFontGenericSansSerif);
    RoboCheckStrEq(res.families[0], "UIBaseSans");
    FontGenericResultClear(&res);

    req.generic = RoboFontGenericSystemUi;
    RoboCheckEqInt(FontGenericResolve(r, &req, &res), RoboOk);
    RoboCheckEqInt(res.resolved, 1);
    RoboCheckStrEq(res.families[0], "SystemUIFace");
    RoboCheck(strcmp(res.families[0], "UIBaseSans") != 0); /* not == sans-serif */
    FontGenericResultClear(&res);

    req.generic = RoboFontGenericEmoji;
    RoboCheckEqInt(FontGenericResolve(r, &req, &res), RoboOk);
    RoboCheckStrEq(res.families[0], "EmojiFace");
    FontGenericResultClear(&res);

    req.generic = RoboFontGenericMath;
    RoboCheckEqInt(FontGenericResolve(r, &req, &res), RoboOk);
    RoboCheckStrEq(res.families[0], "MathFace");
    FontGenericResultClear(&res);

    req.generic = RoboFontGenericUiRounded;
    RoboCheckEqInt(FontGenericResolve(r, &req, &res), RoboOk);
    RoboCheckStrEq(res.families[0], "RoundFace");
    FontGenericResultClear(&res);

    FontGenericResolverDestroy(r);
    FontGenericConfigRelease(cfg);
    FontRegistryDestroy(reg);
}

static void TestSelectAndFallbackIntegration(void)
{
    uint8_t a[4096], b[4096], c[4096];
    size_t na, nb, nc;
    FontRegistry *reg = NULL;
    FontDiscoverySourceId s1 = 0u, s2 = 0u, s3 = 0u;
    FontDiscoveryResult dr;
    FontGenericConfig *cfg = NULL;
    FontGenericResolver *gr = NULL;
    FontFallbackResolver *fb = NULL;
    FontGenericResolverOptions opts;
    FontGenericRequest greq;
    FontSelectRequest sreq;
    FontGenericResult gres;
    FontSelection sel;
    FontFallbackResult fres;
    const char *sans[] = {"MissingFirst", "SansA", "SansB"};
    uint32_t cpOk = 0x41u;
    uint32_t cpMiss = 0x0627u;

    na = BuildFontWithCp(a, sizeof(a), "SansA", "SansA", 400, 0x41u);
    nb = BuildFontWithCp(b, sizeof(b), "SansB", "SansB", 700, 0x41u);
    nc = BuildFontWithCp(c, sizeof(c), "ArabFB", "ArabFB", 400, 0x0627u);
    RoboCheck(na > 0u && nb > 0u && nc > 0u);

    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    RoboCheckEqInt(FontRegistryAddMemorySource(reg, "a", a, na, &s1), RoboOk);
    RoboCheckEqInt(FontRegistryAddMemorySource(reg, "b", b, nb, &s2), RoboOk);
    RoboCheckEqInt(FontRegistryAddMemorySource(reg, "c", c, nc, &s3), RoboOk);
    RoboCheckEqInt(FontRegistryScanAll(reg, 0u, &dr), RoboOk);
    RoboCheck((int)FontRegistryRegistrationCount(reg) >= 3);

    RoboCheckEqInt(FontGenericConfigCreate(&cfg), RoboOk);
    RoboCheckEqInt(
        FontGenericConfigSetMapping(cfg, RoboFontGenericSansSerif, sans, 3u),
        RoboOk);

    FontGenericResolverOptionsInit(&opts);
    opts.application = cfg;
    RoboCheckEqInt(FontGenericResolverCreate(reg, &opts, &gr), RoboOk);
    RoboCheckEqInt(FontFallbackResolverCreate(reg, NULL, &fb), RoboOk);

    FontGenericRequestInit(&greq);
    greq.generic = RoboFontGenericSansSerif;
    FontSelectRequestInit(&sreq);
    sreq.weight = 400;
    sreq.stretch = RoboFontStretchNormal;
    sreq.style = RoboFontStyleNormal;

    FontGenericResultInit(&gres);
    FontSelectionInit(&sel);
    RoboCheckEqInt(FontGenericResolveSelect(gr, &greq, &sreq, &gres, &sel),
                      RoboOk);
    RoboCheckEqInt(gres.resolved, 1);
    RoboCheckEqInt(sel.found, 1);
    RoboCheck(sel.face != NULL);
    /* MissingFirst skipped; SansA preferred over SansB for weight 400. */
    {
        const FontRegistration *rec = NULL;
        RoboCheckEqInt(
            FontRegistryFindByFaceId(reg, sel.faceId, &rec), RoboOk);
        RoboCheck(rec && strcmp(rec->family, "SansA") == 0);
    }
    FontSelectionClear(&sel);
    FontGenericResultClear(&gres);

    /* Weight 700 prefers SansB when walking — first with faces is SansA though.
     * Priority is family order first, then style match within that family.
     * So SansA still wins. Verify bold request still finds a face. */
    sreq.weight = 700;
    RoboCheckEqInt(FontGenericResolveSelect(gr, &greq, &sreq, &gres, &sel),
                      RoboOk);
    RoboCheckEqInt(sel.found, 1);
    FontSelectionClear(&sel);
    FontGenericResultClear(&gres);

    /* Fallback: Arabic missing from sans primary → ArabFB via preferred list
     * won't include ArabFB; registry general scan should find it. */
    sreq.weight = 400;
    FontFallbackResultInit(&fres);
    RoboCheckEqInt(FontGenericResolveFallback(gr, fb, &greq, &sreq, &cpMiss,
                                                         1u, &gres, &sel, &fres),
                      RoboOk);
    RoboCheckEqInt(sel.found, 1);
    RoboCheckEqInt(fres.found, 1);
    RoboCheckEqInt(fres.usedPrimary, 0);
    {
        const FontRegistration *rec = NULL;
        RoboCheckEqInt(
            FontRegistryFindByFaceId(reg, fres.faceId, &rec), RoboOk);
        RoboCheck(rec && strcmp(rec->family, "ArabFB") == 0);
    }
    FontFallbackResultClear(&fres);
    FontSelectionClear(&sel);
    FontGenericResultClear(&gres);

    /* Primary hit path. */
    RoboCheckEqInt(FontGenericResolveFallback(gr, fb, &greq, &sreq, &cpOk, 1u,
                                                         NULL, NULL, &fres),
                      RoboOk);
    RoboCheckEqInt(fres.found, 1);
    RoboCheckEqInt(fres.usedPrimary, 1);
    FontFallbackResultClear(&fres);

    FontFallbackResolverDestroy(fb);
    FontGenericResolverDestroy(gr);
    FontGenericConfigRelease(cfg);
    FontRegistryDestroy(reg);
}

static void TestCacheLanguageFuzz(void)
{
    FontRegistry *reg = NULL;
    FontGenericConfig *cfg = NULL;
    FontGenericResolver *r = NULL;
    FontGenericResolverOptions opts;
    FontGenericRequest req;
    FontGenericResult r1, r2;
    FontGenericCacheStats st;
    const char *defF[] = {"DefaultSerif"};
    const char *arF[] = {"ArabicSerif"};
    uint8_t junk[32];
    uint32_t i;

    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    RoboCheckEqInt(FontGenericConfigCreate(&cfg), RoboOk);
    RoboCheckEqInt(
        FontGenericConfigSetMapping(cfg, RoboFontGenericSerif, defF, 1u),
        RoboOk);
    RoboCheckEqInt(FontGenericConfigSetMappingForLanguage(
                          cfg, RoboFontGenericSerif, "ar", 0u, arF, 1u),
                      RoboOk);

    FontGenericResolverOptionsInit(&opts);
    opts.runtime = cfg;
    RoboCheckEqInt(FontGenericResolverCreate(reg, &opts, &r), RoboOk);

    FontGenericRequestInit(&req);
    req.generic = RoboFontGenericSerif;
    FontGenericResultInit(&r1);
    RoboCheckEqInt(FontGenericResolve(r, &req, &r1), RoboOk);
    RoboCheckStrEq(r1.families[0], "DefaultSerif");

    FontGenericResultInit(&r2);
    RoboCheckEqInt(FontGenericResolve(r, &req, &r2), RoboOk);
    RoboCheckEqInt(r2.fromCache, 1);
    RoboCheckStrEq(r2.families[0], "DefaultSerif");
    FontGenericResultClear(&r1);
    FontGenericResultClear(&r2);

    req.language = "ar";
    RoboCheckEqInt(FontGenericResolve(r, &req, &r1), RoboOk);
    RoboCheckStrEq(r1.families[0], "ArabicSerif");
    FontGenericResultClear(&r1);

    /* Config mutation invalidates. */
    RoboCheckEqInt(
        FontGenericConfigSetMapping(cfg, RoboFontGenericSerif, defF, 1u),
        RoboOk);
    FontGenericResolverCacheClear(r);
    /* Generation bump via set_runtime re-retain. */
    RoboCheckEqInt(FontGenericResolverSetRuntime(r, cfg), RoboOk);
    req.language = NULL;
    RoboCheckEqInt(FontGenericResolve(r, &req, &r1), RoboOk);
    RoboCheckEqInt(r1.fromCache, 0);
    FontGenericResultClear(&r1);

    FontGenericResolverCacheStats(r, &st);
    RoboCheck(st.hits >= 1u);

    for (i = 0u; i < sizeof(junk); i++) {
        junk[i] = (uint8_t)(i * 17u);
    }
    RoboCheckEqInt(FontGenericFuzzParse(junk, sizeof(junk)), RoboOk);
    RoboCheckEqInt(FontGenericFuzzParse(NULL, 0u), RoboOk);

    /* Duplicate entries keep deterministic order. */
    {
        const char *dup[] = {"A", "B", "A"};
        RoboCheckEqInt(FontGenericConfigSetMapping(
                              cfg, RoboFontGenericMonospace, dup, 3u),
                          RoboOk);
        req.generic = RoboFontGenericMonospace;
        RoboCheckEqInt(FontGenericResolve(r, &req, &r1), RoboOk);
        RoboCheckEqInt((int)r1.familyCount, 3);
        RoboCheckStrEq(r1.families[0], "A");
        RoboCheckStrEq(r1.families[1], "B");
        RoboCheckStrEq(r1.families[2], "A");
        FontGenericResultClear(&r1);
    }

    FontGenericResolverDestroy(r);
    FontGenericConfigRelease(cfg);
    FontRegistryDestroy(reg);
}

void TestRegisterFontGeneric(void)
{
    TestRegister("font.generic.ids_names", TestIdsAndNames);
    TestRegister("font.generic.config_precedence", TestConfigPrecedenceAndEmpty);
    TestRegister("font.generic.related_specials", TestRelatedAndSpecials);
    TestRegister("font.generic.select_fallback",
                       TestSelectAndFallbackIntegration);
    TestRegister("font.generic.cache_lang_fuzz", TestCacheLanguageFuzz);
}
