/*
 * RoboFont loader / cache suite (Goal 5.13).
 * Synthetic fixtures only — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

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
static void PutU32(uint8_t **p, uint32_t v)
{
    *(*p)++ = (uint8_t)(v >> 24);
    *(*p)++ = (uint8_t)(v >> 16);
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static void PutI16(uint8_t **p, int16_t v) { PutU16(p, (uint16_t)v); }
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
                         uint32_t n, uint32_t baseOffset)
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
        PutU32(&p, (uint32_t)(baseOffset + cursor));
        PutU32(&p, tables[i].length);
        memcpy(out + cursor, tables[i].data, tables[i].length);
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

static size_t BuildHhea(uint8_t *b)
{
    uint8_t *p = b;
    PutU32(&p, 0x00010000u);
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
    PutU16(&p, 1);
    return (size_t)(p - b);
}

static size_t BuildMaxp(uint8_t *b)
{
    uint8_t *p = b;
    int i;
    PutU32(&p, 0x00010000u);
    PutU16(&p, 1);
    for (i = 0; i < 13; i++) {
        PutU16(&p, 0);
    }
    return (size_t)(p - b);
}

static size_t BuildHmtx(uint8_t *b)
{
    uint8_t *p = b;
    PutU16(&p, 500);
    PutI16(&p, 0);
    return (size_t)(p - b);
}

static size_t BuildLoca(uint8_t *b)
{
    uint8_t *p = b;
    PutU16(&p, 0);
    PutU16(&p, 0);
    return (size_t)(p - b);
}

static size_t BuildPost(uint8_t *b)
{
    uint8_t *p = b;
    PutU32(&p, 0x00030000u);
    PutU32(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    return (size_t)(p - b);
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

static size_t BuildNamedFontAt(uint8_t *out, size_t cap, uint32_t baseOffset,
                                  const char *family, const char *ps, uint16_t weight)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[8], loca[8], post[64], os2[128], name[512];
    SynthTable tabs[9];
    uint32_t nt = 0u;
    size_t nn = BuildNameTable(name, sizeof(name), family, ps);
    if (nn == 0u) {
        return 0u;
    }
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
    return BuildSfnt(out, cap, tabs, nt, baseOffset);
}

static size_t BuildNamedFont(uint8_t *out, size_t cap, const char *family,
                               const char *ps, uint16_t weight)
{
    return BuildNamedFontAt(out, cap, 0u, family, ps, weight);
}

static void TestLazyLoadAndCacheHit(void)
{
    uint8_t font[2048];
    size_t n;
    FontRegistry *reg = NULL;
    FontLoader *loader = NULL;
    FontDiscoverySourceId sid = 0u;
    FontDiscoveryResult scan;
    const FontRegistration *rec = NULL;
    FontLoadResult a, b;
    const FontCacheStats *st;

    n = BuildNamedFont(font, sizeof(font), "LoadFam", "LoadFam-Regular", 400);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    RoboCheckEqInt(FontLoaderCreate(reg, NULL, &loader), RoboOk);
    RoboCheckEqInt(
        FontRegistryAddMemorySource(reg, "mem:LoadFam", font, n, &sid), RoboOk);
    RoboCheckEqInt(FontRegistryScanSource(reg, sid, 0u, &scan), RoboOk);
    RoboCheckEqInt(FontRegistryRegistrationAt(reg, 0u, &rec), RoboOk);

    /* Discovery registered metadata; metrics not yet loaded. */
    RoboCheck(FontFaceMetricsInfo(rec->face) == NULL);
    RoboCheckEqInt(FontLoaderState(loader, rec->id), RoboFontLoadUnloaded);

    FontLoadResultInit(&a);
    RoboCheckEqInt(FontLoaderLoadRegistration(loader, rec->id, &a), RoboOk);
    RoboCheckEqInt(a.state, RoboFontLoadLoaded);
    RoboCheckEqInt((int)a.fromCache, 0);
    RoboCheck(a.face != NULL);
    RoboCheck(FontFaceMetricsInfo(a.face) != NULL);
    RoboCheckEqInt(FontLoaderState(loader, rec->id), RoboFontLoadLoaded);

    FontLoadResultInit(&b);
    RoboCheckEqInt(FontLoaderLoadRegistration(loader, rec->id, &b), RoboOk);
    RoboCheckEqInt((int)b.fromCache, 1);
    RoboCheck(b.face == a.face);

    st = GetFontCacheStats(FontLoaderCache(loader));
    RoboCheck(st->hits >= 1u);
    RoboCheck(st->misses >= 1u);
    RoboCheck(st->entries >= 1u);

    {
        FontFace **faces = NULL;
        uint32_t fn = 0u;
        FontSelectRequest req;
        FontSelection sel;
        faces = (FontFace **)RoboCalloc(1, sizeof(*faces));
        RoboCheck(faces != NULL);
        faces[0] = a.face;
        fn = 1u;
        FontSelectRequestInit(&req);
        FontSelectionInit(&sel);
        req.family = "loadfam";
        RoboCheckEqInt(FontSelect(&req, faces, fn, &sel), RoboOk);
        RoboCheckEqInt(sel.found, 1);
        FontSelectionClear(&sel);
        RoboFree(faces);
    }

    FontLoadResultClear(&a);
    FontLoadResultClear(&b);
    FontLoaderDestroy(loader);
    FontRegistryDestroy(reg);
}

static void TestCacheEvictAndInvalidate(void)
{
    uint8_t f0[2048], f1[2048], f2[2048];
    size_t n0, n1, n2;
    FontRegistry *reg = NULL;
    FontLoader *loader = NULL;
    FontLoaderConfig cfg;
    FontDiscoverySourceId s0 = 0u, s1 = 0u, s2 = 0u;
    FontDiscoveryResult scan;
    const FontRegistration *r0 = NULL, *r1 = NULL, *r2 = NULL;
    FontLoadResult lr;
    FontRegistrationId id0, id1;

    n0 = BuildNamedFont(f0, sizeof(f0), "E0", "E0", 400);
    n1 = BuildNamedFont(f1, sizeof(f1), "E1", "E1", 400);
    n2 = BuildNamedFont(f2, sizeof(f2), "E2", "E2", 400);
    RoboCheck(n0 > 0u && n1 > 0u && n2 > 0u);

    FontLoaderConfigInit(&cfg);
    cfg.maxFaces = 2u;
    cfg.maxBytes = RoboFontCacheDefaultMaxBytes;

    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    RoboCheckEqInt(FontLoaderCreate(reg, &cfg, &loader), RoboOk);
    RoboCheckEqInt(FontRegistryAddMemorySource(reg, "e0", f0, n0, &s0),
                      RoboOk);
    RoboCheckEqInt(FontRegistryAddMemorySource(reg, "e1", f1, n1, &s1),
                      RoboOk);
    RoboCheckEqInt(FontRegistryAddMemorySource(reg, "e2", f2, n2, &s2),
                      RoboOk);
    RoboCheckEqInt(FontRegistryScanSource(reg, s0, 0u, &scan), RoboOk);
    RoboCheckEqInt(FontRegistryScanSource(reg, s1, 0u, &scan), RoboOk);
    RoboCheckEqInt(FontRegistryScanSource(reg, s2, 0u, &scan), RoboOk);
    RoboCheckEqInt(FontRegistryRegistrationAt(reg, 0u, &r0), RoboOk);
    RoboCheckEqInt(FontRegistryRegistrationAt(reg, 1u, &r1), RoboOk);
    RoboCheckEqInt(FontRegistryRegistrationAt(reg, 2u, &r2), RoboOk);
    id0 = r0->id;
    id1 = r1->id;

    FontLoadResultInit(&lr);
    RoboCheckEqInt(FontLoaderLoadRegistration(loader, id0, &lr), RoboOk);
    FontLoadResultClear(&lr);
    RoboCheckEqInt(FontLoaderLoadRegistration(loader, id1, &lr), RoboOk);
    FontLoadResultClear(&lr);
    RoboCheckEqInt(FontLoaderLoadRegistration(loader, r2->id, &lr), RoboOk);
    FontLoadResultClear(&lr);
    RoboCheck(FontCacheEntryCount(FontLoaderCache(loader)) <= 2u);

    RoboCheckEqInt(
        FontCacheInvalidateRegistration(FontLoaderCache(loader), r2->id),
        RoboOk);
    RoboCheckEqInt(FontLoaderState(loader, r2->id), RoboFontLoadUnloaded);

    RoboCheckEqInt(FontRegistryRemoveRegistration(reg, id0), RoboOk);
    RoboCheckEqInt(FontLoaderState(loader, id0), RoboFontLoadUnloaded);

    FontCacheClear(FontLoaderCache(loader));
    RoboCheckEqInt((int)FontCacheEntryCount(FontLoaderCache(loader)),
                      0);

    FontLoaderDestroy(loader);
    FontRegistryDestroy(reg);
}

static void TestTtcLoadAndFailures(void)
{
    uint8_t ttc[4096];
    size_t hdr = 12u + 8u;
    size_t na, nb;
    uint8_t *p;
    FontRegistry *reg = NULL;
    FontLoader *loader = NULL;
    FontDiscoverySourceId sid = 0u;
    FontDiscoveryResult scan;
    const FontRegistration *r0 = NULL, *r1 = NULL;
    FontLoadResult lr;
    uint8_t bad[32];

    memset(ttc, 0, sizeof(ttc));
    p = ttc;
    PutU32(&p, RoboFontSfntVersionTtcf);
    PutU32(&p, 0x00010000u);
    PutU32(&p, 2u);
    PutU32(&p, (uint32_t)hdr);
    PutU32(&p, 0u);
    na = BuildNamedFontAt(ttc + hdr, sizeof(ttc) - hdr, (uint32_t)hdr, "LCollA",
                             "LCollA", 400);
    RoboCheck(na > 0u);
    p = ttc + 12;
    PutU32(&p, (uint32_t)hdr);
    PutU32(&p, (uint32_t)(hdr + na));
    nb = BuildNamedFontAt(ttc + hdr + na, sizeof(ttc) - (hdr + na),
                             (uint32_t)(hdr + na), "LCollB", "LCollB", 700);
    RoboCheck(nb > 0u);

    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    RoboCheckEqInt(FontLoaderCreate(reg, NULL, &loader), RoboOk);
    RoboCheckEqInt(
        FontRegistryAddMemorySource(reg, "c.ttc", ttc, hdr + na + nb, &sid),
        RoboOk);
    RoboCheckEqInt(FontRegistryScanSource(reg, sid, 0u, &scan), RoboOk);
    RoboCheckEqInt((int)scan.registered, 2);
    RoboCheckEqInt(FontRegistryRegistrationAt(reg, 0u, &r0), RoboOk);
    RoboCheckEqInt(FontRegistryRegistrationAt(reg, 1u, &r1), RoboOk);

    FontLoadResultInit(&lr);
    RoboCheckEqInt(FontLoaderLoadRegistration(loader, r0->id, &lr), RoboOk);
    RoboCheck(lr.face != NULL);
    RoboCheckEqInt((int)FontFaceIndex(lr.face), (int)r0->faceIndex);
    FontLoadResultClear(&lr);
    RoboCheckEqInt(FontLoaderLoadRegistration(loader, r1->id, &lr), RoboOk);
    RoboCheck(lr.face != NULL);
    RoboCheck(FontFaceSource(lr.face) == FontFaceSource(r0->face) ||
               FontFaceSource(lr.face) != NULL);
    FontLoadResultClear(&lr);

    /* Missing registration */
    RoboCheckEqInt(FontLoaderLoadRegistration(loader, 99999u, &lr),
                      RoboNotFound);
    RoboCheckEqInt(lr.state, RoboFontLoadFailed);

    /* Unsupported / malformed memory scan + load path via fuzz */
    memset(bad, 0xA5, sizeof(bad));
    RoboCheckEqInt(FontLoaderFuzzParse(bad, sizeof(bad)), RoboOk);
    RoboCheckEqInt(FontLoaderFuzzParse(NULL, 0u), RoboOk);

    FontLoaderDestroy(loader);
    FontRegistryDestroy(reg);
}

static void TestVirtualFileLoadAndBench(void)
{
    uint8_t font[2048];
    size_t n;
    FontDiscoveryVirtualIo *vio = NULL;
    FontRegistry *reg = NULL;
    FontLoader *loader = NULL;
    FontDiscoverySourceId sid = 0u;
    FontDiscoveryResult scan;
    const FontRegistration *rec = NULL;
    FontLoadResult lr;
    uint32_t i;
    uint32_t coldHits = 0u;
    const FontCacheStats *st;

    n = BuildNamedFont(font, sizeof(font), "VirtLoad", "VirtLoad", 400);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontDiscoveryVirtualIoCreate(&vio), RoboOk);
    RoboCheckEqInt(
        FontDiscoveryVirtualAddFile(vio, "/Fonts/VirtLoad.ttf", font, n),
        RoboOk);
    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    FontRegistrySetIo(reg, FontDiscoveryVirtualIoOps(vio));
    RoboCheckEqInt(FontLoaderCreate(reg, NULL, &loader), RoboOk);
    RoboCheckEqInt(FontRegistryAddFileSource(reg, "/Fonts/VirtLoad.ttf", &sid),
                      RoboOk);
    RoboCheckEqInt(FontRegistryScanSource(reg, sid, 0u, &scan), RoboOk);
    RoboCheckEqInt(FontRegistryRegistrationAt(reg, 0u, &rec), RoboOk);

    FontLoadResultInit(&lr);
    RoboCheckEqInt(FontLoaderLoadRegistration(loader, rec->id, &lr), RoboOk);
    RoboCheckEqInt((int)lr.fromCache, 0);
    FontLoadResultClear(&lr);

    for (i = 0u; i < 64u; i++) {
        RoboCheckEqInt(FontLoaderLoadRegistration(loader, rec->id, &lr),
                          RoboOk);
        coldHits += lr.fromCache;
        FontLoadResultClear(&lr);
    }
    RoboCheck(coldHits == 64u);
    st = GetFontCacheStats(FontLoaderCache(loader));
    RoboCheck(st->hits >= 64u);

    /* Bypass: load without requiring a new cache insert (hit OK if already cached). */
    {
        FontLoadRequest req;
        uint32_t before = FontCacheEntryCount(FontLoaderCache(loader));
        FontLoadRequestInit(&req);
        req.registrationId = rec->id;
        req.cachePolicy = RoboFontCachePolicyBypass;
        RoboCheckEqInt(FontLoaderLoad(loader, &req, &lr), RoboOk);
        RoboCheck(lr.face != NULL);
        RoboCheckEqInt((int)FontCacheEntryCount(FontLoaderCache(loader)),
                          (int)before);
        FontLoadResultClear(&lr);
    }

    FontLoaderDestroy(loader);
    FontRegistryDestroy(reg);
    FontDiscoveryVirtualIoDestroy(vio);
}

static void TestRefreshInvalidatesCache(void)
{
    uint8_t font[2048];
    size_t n;
    FontRegistry *reg = NULL;
    FontLoader *loader = NULL;
    FontDiscoverySourceId sid = 0u;
    FontDiscoveryResult scan;
    const FontRegistration *rec = NULL;
    FontLoadResult lr;
    FontRegistrationId oldId;

    n = BuildNamedFont(font, sizeof(font), "RefFam", "RefFam", 400);
    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    RoboCheckEqInt(FontLoaderCreate(reg, NULL, &loader), RoboOk);
    RoboCheckEqInt(
        FontRegistryAddMemorySource(reg, "ref", font, n, &sid), RoboOk);
    RoboCheckEqInt(FontRegistryScanSource(reg, sid, 0u, &scan), RoboOk);
    RoboCheckEqInt(FontRegistryRegistrationAt(reg, 0u, &rec), RoboOk);
    oldId = rec->id;
    RoboCheckEqInt(FontLoaderLoadRegistration(loader, oldId, &lr), RoboOk);
    FontLoadResultClear(&lr);
    RoboCheck(FontCacheEntryCount(FontLoaderCache(loader)) >= 1u);

    RoboCheckEqInt(FontRegistryRefreshSource(reg, sid, 0u, &scan), RoboOk);
    /* Refresh removes old registrations → invalidate hook clears cache entry. */
    RoboCheckEqInt(FontLoaderState(loader, oldId), RoboFontLoadUnloaded);

    FontLoaderDestroy(loader);
    FontRegistryDestroy(reg);
}

void TestRegisterFontLoader(void)
{
    TestRegister("font.loader.lazy_cache", TestLazyLoadAndCacheHit);
    TestRegister("font.loader.evict_invalidate", TestCacheEvictAndInvalidate);
    TestRegister("font.loader.ttc_fail_fuzz", TestTtcLoadAndFailures);
    TestRegister("font.loader.virtual_bench", TestVirtualFileLoadAndBench);
    TestRegister("font.loader.refresh", TestRefreshInvalidatesCache);
}
