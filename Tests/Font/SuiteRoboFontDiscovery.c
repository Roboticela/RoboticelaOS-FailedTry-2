/*
 * RoboFont discovery / registry suite (Goal 5.12).
 * Synthetic + virtual-IO fixtures only — no Chromium / system fonts.
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
    PutI16(&p, -100);
    PutI16(&p, 50);
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

static void TestClassifyAndExt(void)
{
    uint8_t ttf[4] = {0x00, 0x01, 0x00, 0x00};
    uint8_t otto[4] = {'O', 'T', 'T', 'O'};
    uint8_t ttc[4] = {'t', 't', 'c', 'f'};
    uint8_t junk[4] = {1, 2, 3, 4};
    RoboCheckEqInt(FontDiscoveryClassifyBytes(ttf, 4), RoboFontCandidateTtf);
    RoboCheckEqInt(FontDiscoveryClassifyBytes(otto, 4), RoboFontCandidateOtf);
    RoboCheckEqInt(FontDiscoveryClassifyBytes(ttc, 4),
                      RoboFontCandidateCollection);
    RoboCheckEqInt(FontDiscoveryClassifyBytes(junk, 4),
                      RoboFontCandidateUnsupported);
    RoboCheck(FontDiscoveryExtensionIsFont("A.TTF"));
    RoboCheck(FontDiscoveryExtensionIsFont("/x/y.otc"));
    RoboCheck(!FontDiscoveryExtensionIsFont("readme.txt"));
}

static void TestMemoryRegisterAndSelect(void)
{
    uint8_t font[2048];
    size_t n;
    FontRegistry *reg = NULL;
    FontDiscoverySourceId sid = 0u;
    FontDiscoveryResult result;
    FontFace **faces = NULL;
    uint32_t faceN = 0u;
    FontSelectRequest req;
    FontSelection sel;
    const FontRegistration *rec = NULL;

    n = BuildNamedFont(font, sizeof(font), "Discover", "Discover-Regular", 400);
    RoboCheck(n > 0u);

    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    RoboCheckEqInt(
        FontRegistryAddMemorySource(reg, "mem:Discover", font, n, &sid), RoboOk);
    RoboCheckEqInt(FontRegistryScanSource(reg, sid, 0u, &result), RoboOk);
    RoboCheckEqInt((int)result.registered, 1);
    RoboCheckEqInt((int)FontRegistryRegistrationCount(reg), 1);

    RoboCheckEqInt(FontRegistryRegistrationAt(reg, 0u, &rec), RoboOk);
    RoboCheck(rec->face != NULL);
    RoboCheck(rec->faceId != 0u);
    RoboCheck(rec->id != 0u);

    RoboCheckEqInt(FontRegistryCollectFaces(reg, &faces, &faceN), RoboOk);
    RoboCheckEqInt((int)faceN, 1);

    FontSelectRequestInit(&req);
    FontSelectionInit(&sel);
    req.family = "discover";
    req.weight = 400;
    RoboCheckEqInt(FontSelect(&req, faces, faceN, &sel), RoboOk);
    RoboCheckEqInt(sel.found, 1);
    RoboCheck(sel.face == faces[0]);
    FontSelectionClear(&sel);
    RoboFree(faces);

    FontRegistryDestroy(reg);
}

static void TestVirtualDirectory(void)
{
    uint8_t a[2048], b[2048], junk[8] = {0};
    size_t na, nb;
    FontDiscoveryVirtualIo *vio = NULL;
    FontRegistry *reg = NULL;
    FontDiscoverySourceId sid = 0u;
    FontDiscoveryResult result;

    na = BuildNamedFont(a, sizeof(a), "Alpha", "Alpha-Regular", 400);
    nb = BuildNamedFont(b, sizeof(b), "Beta", "Beta-Bold", 700);
    RoboCheck(na > 0u && nb > 0u);

    RoboCheckEqInt(FontDiscoveryVirtualIoCreate(&vio), RoboOk);
    RoboCheckEqInt(FontDiscoveryVirtualAddDir(vio, "/Fonts"), RoboOk);
    RoboCheckEqInt(FontDiscoveryVirtualAddFile(vio, "/Fonts/Alpha.ttf", a, na),
                      RoboOk);
    RoboCheckEqInt(FontDiscoveryVirtualAddFile(vio, "/Fonts/Beta.otf", b, nb),
                      RoboOk);
    RoboCheckEqInt(
        FontDiscoveryVirtualAddFile(vio, "/Fonts/readme.txt", junk, sizeof(junk)),
        RoboOk);

    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    FontRegistrySetIo(reg, FontDiscoveryVirtualIoOps(vio));
    RoboCheckEqInt(
        FontRegistryAddDirectorySource(reg, "/Fonts", 0, &sid), RoboOk);
    RoboCheckEqInt(FontRegistryScanSource(reg, sid, 0u, &result), RoboOk);
    RoboCheckEqInt((int)result.registered, 2);
    RoboCheck(result.skippedFilter >= 1u);
    RoboCheckEqInt((int)FontRegistryRegistrationCount(reg), 2);

    FontRegistryDestroy(reg);
    FontDiscoveryVirtualIoDestroy(vio);
}

static void TestTtcCollection(void)
{
    uint8_t ttc[4096];
    size_t hdr = 12u + 8u; /* ttcf + ver + numFonts + 2 offsets */
    size_t na, nb;
    uint32_t count = 0u;
    FontRegistry *reg = NULL;
    FontDiscoverySourceId sid = 0u;
    FontDiscoveryResult result;
    uint8_t *p;
    const FontRegistration *r0 = NULL;
    const FontRegistration *r1 = NULL;

    memset(ttc, 0, sizeof(ttc));
    p = ttc;
    PutU32(&p, RoboFontSfntVersionTtcf);
    PutU32(&p, 0x00010000u);
    PutU32(&p, 2u);
    PutU32(&p, (uint32_t)hdr); /* face 0 offset placeholder */
    PutU32(&p, 0u);            /* face 1 offset placeholder */

    na = BuildNamedFontAt(ttc + hdr, sizeof(ttc) - hdr, (uint32_t)hdr, "CollA", "CollA",
                             400);
    RoboCheck(na > 0u);
    p = ttc + 12;
    PutU32(&p, (uint32_t)hdr);
    PutU32(&p, (uint32_t)(hdr + na));
    nb = BuildNamedFontAt(ttc + hdr + na, sizeof(ttc) - (hdr + na),
                             (uint32_t)(hdr + na), "CollB", "CollB", 700);
    RoboCheck(nb > 0u);

    RoboCheckEqInt(FontSfntProbeFaceCount(ttc, hdr + na + nb, &count),
                      RoboOk);
    RoboCheckEqInt((int)count, 2);

    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    RoboCheckEqInt(
        FontRegistryAddMemorySource(reg, "coll.ttc", ttc, hdr + na + nb, &sid),
        RoboOk);
    RoboCheckEqInt(FontRegistryScanSource(reg, sid, 0u, &result), RoboOk);
    RoboCheckEqInt((int)result.registered, 2);
    RoboCheckEqInt(FontRegistryRegistrationAt(reg, 0u, &r0), RoboOk);
    RoboCheckEqInt(FontRegistryRegistrationAt(reg, 1u, &r1), RoboOk);
    RoboCheckEqInt((int)r0->isCollection, 1);
    RoboCheckEqInt((int)r1->isCollection, 1);
    RoboCheck(r0->faceId != r1->faceId);
    RoboCheck(r0->faceIndex != r1->faceIndex);
    FontRegistryDestroy(reg);
}

static void TestDuplicateAndRefresh(void)
{
    uint8_t font[2048];
    size_t n;
    FontRegistry *reg = NULL;
    FontDiscoverySourceId s1 = 0u, s2 = 0u;
    FontDiscoveryResult result;

    n = BuildNamedFont(font, sizeof(font), "DupFam", "DupFam-Regular", 400);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    RoboCheckEqInt(
        FontRegistryAddMemorySource(reg, "a", font, n, &s1), RoboOk);
    RoboCheckEqInt(
        FontRegistryAddMemorySource(reg, "b", font, n, &s2), RoboOk);
    RoboCheckEqInt(FontRegistryScanSource(reg, s1, 0u, &result), RoboOk);
    RoboCheckEqInt((int)result.registered, 1);
    RoboCheckEqInt(FontRegistryScanSource(reg, s2, 0u, &result), RoboOk);
    RoboCheckEqInt((int)result.registered, 0);
    RoboCheck(result.skippedDuplicate >= 1u);
    RoboCheckEqInt((int)FontRegistryRegistrationCount(reg), 1);

    RoboCheckEqInt(FontRegistryRefreshSource(reg, s1, 0u, &result), RoboOk);
    RoboCheckEqInt((int)result.registered, 1);
    RoboCheckEqInt((int)FontRegistryRegistrationCount(reg), 1);

    FontRegistryDestroy(reg);
}

static void TestRemoveAndUnsupported(void)
{
    uint8_t font[2048];
    uint8_t bad[16] = {0};
    size_t n;
    FontRegistry *reg = NULL;
    FontDiscoverySourceId sid = 0u;
    FontDiscoveryResult result;
    const FontRegistration *rec = NULL;
    FontRegistrationId rid;

    n = BuildNamedFont(font, sizeof(font), "Gone", "Gone", 400);
    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    RoboCheckEqInt(
        FontRegistryAddMemorySource(reg, "gone", font, n, &sid), RoboOk);
    RoboCheckEqInt(FontRegistryScanSource(reg, sid, 0u, &result), RoboOk);
    RoboCheckEqInt(FontRegistryRegistrationAt(reg, 0u, &rec), RoboOk);
    rid = rec->id;
    RoboCheckEqInt(FontRegistryRemoveRegistration(reg, rid), RoboOk);
    RoboCheckEqInt((int)FontRegistryRegistrationCount(reg), 0);

    RoboCheckEqInt(
        FontRegistryAddMemorySource(reg, "bad", bad, sizeof(bad), &sid), RoboOk);
    RoboCheckEqInt(FontRegistryScanSource(reg, sid, 0u, &result), RoboOk);
    RoboCheck(result.skippedUnsupported >= 1u);

    FontRegistryDestroy(reg);
}

static void TestFuzzDiscovery(void)
{
    uint8_t junk[64];
    size_t i;
    for (i = 0u; i < sizeof(junk); i++) {
        junk[i] = (uint8_t)(i * 19u + 3u);
    }
    RoboCheckEqInt(FontDiscoveryFuzzParse(junk, sizeof(junk)), RoboOk);
    RoboCheckEqInt(FontDiscoveryFuzzParse(NULL, 0u), RoboOk);
}

void TestRegisterFontDiscovery(void)
{
    TestRegister("font.discover.classify", TestClassifyAndExt);
    TestRegister("font.discover.memory_select", TestMemoryRegisterAndSelect);
    TestRegister("font.discover.virtual_dir", TestVirtualDirectory);
    TestRegister("font.discover.ttc", TestTtcCollection);
    TestRegister("font.discover.duplicate_refresh", TestDuplicateAndRefresh);
    TestRegister("font.discover.remove_unsupported", TestRemoveAndUnsupported);
    TestRegister("font.discover.fuzz", TestFuzzDiscovery);
}
