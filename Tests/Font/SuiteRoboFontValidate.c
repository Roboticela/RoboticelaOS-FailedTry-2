/*
 * RoboFont validation & security suite (Goal 5.20).
 * Synthetic SFNT fixtures only — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
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

/* ---- Security regression corpus (permanent regressions) ---- */

static const uint8_t kRegressEmpty[] = {0u};

static const uint8_t kRegressFourZeros[4] = {0u, 0u, 0u, 0u};

static const uint8_t kRegressTruncatedSfnt[8] = {
    0x00, 0x01, 0x00, 0x00, /* TrueType signature */
    0x00, 0x01,              /* numTables = 1 */
    0x00, 0x10               /* truncated directory */
};

/* Two directory entries claim the same payload offset (overlap). */
static const uint8_t kRegressOverlapDir[] = {
    0x00, 0x01, 0x00, 0x00,
    0x00, 0x02,
    0x00, 0x20, 0x00, 0x00,
    0x00, 0x01,
    0x00, 0x00,
    'A', 'A', 'A', 'A', 0, 0, 0, 0, 0, 0, 0, 28, 0, 0, 0, 8,
    'B', 'B', 'B', 'B', 0, 0, 0, 0, 0, 0, 0, 28, 0, 0, 0, 8,
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE
};

/* numTables claims 255 but directory is truncated. */
static const uint8_t kRegressHugeTableCount[16] = {
    0x00, 0x01, 0x00, 0x00,
    0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00
};

/* ---- Binary helpers ---- */

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

/* Synthetic builder — directory checksums left zero (typical for tests). */
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
        if (tables[i].length) {
            memcpy(out + cursor, tables[i].data, tables[i].length);
        }
        cursor += pad;
    }
    return cursor;
}

/* Builder with correct per-table checksums (for checksum policy tests). */
static size_t BuildSfntChecked(uint8_t *out, size_t outCap, const SynthTable *tables,
                                 uint32_t n)
{
    size_t dirEnd;
    size_t cursor;
    size_t need;
    uint32_t i;

    dirEnd = 12u + (size_t)n * 16u;
    need = dirEnd;
    for (i = 0u; i < n; i++) {
        need += ((size_t)tables[i].length + 3u) & ~(size_t)3u;
    }
    if (need > outCap) {
        return 0u;
    }
    memset(out, 0, need);
    {
        uint8_t *hp = out;
        PutU32(&hp, RoboFontSfntVersionTrue);
    }
    WriteSearchFields(out, (uint16_t)n);

    cursor = dirEnd;
    for (i = 0u; i < n; i++) {
        uint32_t checksum;
        uint32_t pad;
        uint8_t *rec = out + 12u + (size_t)i * 16u;
        uint8_t *p = rec;

        if (tables[i].length > 0u && tables[i].data) {
            memcpy(out + cursor, tables[i].data, tables[i].length);
        }
        checksum = FontSfntChecksum(
            tables[i].length ? (out + cursor) : NULL, tables[i].length);
        PutU32(&p, tables[i].tag);
        PutU32(&p, checksum);
        PutU32(&p, (uint32_t)cursor);
        PutU32(&p, tables[i].length);
        pad = (uint32_t)(((size_t)tables[i].length + 3u) & ~(size_t)3u);
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
        PutI16(&p, 0);
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

static size_t BuildCmapFmt4Ascii(uint8_t *buf, uint16_t cp, uint16_t gid)
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

static size_t BuildCpal2x2(uint8_t *buf)
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

static size_t BuildColrV0Layers(uint8_t *buf)
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

typedef struct {
    const char *family;
    const char *ps;
    uint16_t numGlyphs;
    int withColor;
} MinimalFontSpec;

static size_t BuildMinimalFont(uint8_t *out, size_t cap, const MinimalFontSpec *spec)
{
    uint8_t head[64];
    uint8_t hhea[64];
    uint8_t maxp[64];
    uint8_t hmtx[256];
    uint8_t loca[256];
    uint8_t post[64];
    uint8_t os2[128];
    uint8_t name[512];
    uint8_t cmap[256];
    uint8_t cpal[64];
    uint8_t colr[64];
    SynthTable tabs[16];
    uint32_t nt = 0u;
    size_t nn;
    size_t nc;
    uint16_t glyphs;

    if (!spec || spec->numGlyphs < 1u) {
        return 0u;
    }
    glyphs = spec->numGlyphs;
    nn = BuildNameTable(name, sizeof(name), spec->family, spec->ps);
    nc = BuildCmapFmt4Ascii(cmap, 0x41u, 1u);
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

    if (spec->withColor) {
        tabs[nt++] = (SynthTable){RoboFontTagCpal, cpal,
                                      (uint32_t)BuildCpal2x2(cpal)};
        tabs[nt++] = (SynthTable){RoboFontTagColr, colr,
                                      (uint32_t)BuildColrV0Layers(colr)};
    }
    return BuildSfnt(out, cap, tabs, nt);
}

static void LoadFace(const uint8_t *font, size_t n, FontSource **src,
                      FontFace **face)
{
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(*src, 0u, face), RoboOk);
}

static int FindIssueCode(const FontValidationResult *result,
                           FontValidationIssueCode code,
                           FontValidationIssue *out)
{
    uint32_t i;
    FontValidationIssue issue;

    if (!result) {
        return 0;
    }
    for (i = 0u; i < FontValidationResultIssueCount(result); i++) {
        if (FontValidationResultIssue(result, i, &issue) != RoboOk) {
            continue;
        }
        if (issue.code == code) {
            if (out) {
                *out = issue;
            }
            return 1;
        }
    }
    return 0;
}

static int IssueHasFatalCode(const FontValidationResult *result,
                                FontValidationIssueCode code)
{
    uint32_t i;
    FontValidationIssue issue;

    if (!result) {
        return 0;
    }
    for (i = 0u; i < FontValidationResultIssueCount(result); i++) {
        if (FontValidationResultIssue(result, i, &issue) != RoboOk) {
            continue;
        }
        if (issue.code == code &&
            issue.severity == RoboFontValidationSeverityFatal) {
            return 1;
        }
    }
    return 0;
}

/* ---- Tests ---- */

static void TestPolicyLimits(void)
{
    FontValidationPolicy pol;
    FontValidationLimits lim;
    const char *s;

    FontValidationPolicyInit(&pol);
    RoboCheckEqInt((int)pol.level, (int)RoboFontValidationBasic);
    RoboCheckEqInt((int)pol.checksumPolicy, (int)RoboFontChecksumWarn);
    RoboCheck(pol.rejectOversized != 0u);
    RoboCheck(pol.stopOnFatal != 0u);

    memset(&lim, 0, sizeof(lim));
    lim.maxSourceSize = UINT64_MAX;
    lim.maxTables = UINT32_MAX;
    lim.maxGlyphs = UINT32_MAX;
    lim.maxAllocation = UINT32_MAX;
    lim.maxWorkBudget = UINT32_MAX;
    FontValidationLimitsClamp(&lim);
    RoboCheck(lim.maxSourceSize <= RoboFontValidationHardMaxSourceSize);
    RoboCheck(lim.maxTables <= RoboFontValidationHardMaxTables);
    RoboCheck(lim.maxGlyphs <= RoboFontValidationHardMaxGlyphs);
    RoboCheck(lim.maxAllocation <= RoboFontValidationHardMaxAlloc);
    RoboCheck(lim.maxWorkBudget <= RoboFontValidationHardMaxWork);
    RoboCheck(lim.maxTables >= 1u);
    RoboCheck(lim.maxGlyphs >= 1u);

    RoboCheckEqInt((int)FontValidationLevelFromLoader(RoboFontValidateTrusted),
                      (int)RoboFontValidationNone);
    RoboCheckEqInt((int)FontValidationLevelFromLoader(RoboFontValidateNormal),
                      (int)RoboFontValidationBasic);
    RoboCheckEqInt((int)FontValidationLevelFromLoader(RoboFontValidateStrict),
                      (int)RoboFontValidationStrict);
    RoboCheckEqInt((int)FontValidationLevelFromLoader(RoboFontValidateParanoid),
                      (int)RoboFontValidationParanoid);

    FontValidationPolicyFromLoader(RoboFontValidateParanoid, &pol);
    RoboCheckEqInt((int)pol.level, (int)RoboFontValidationParanoid);
    RoboCheckEqInt((int)pol.checksumPolicy, (int)RoboFontChecksumReject);

    RoboCheckEqInt((int)FontValidationLevelToLoader(RoboFontValidationParanoid),
                      (int)RoboFontValidateParanoid);

    s = FontValidationIssueCodeString(RoboFontValidationChecksumMismatch);
    RoboCheck(s != NULL);
    RoboCheck(strcmp(s, "checksum_mismatch") == 0);
    s = FontValidationSeverityString(RoboFontValidationSeverityFatal);
    RoboCheck(s != NULL);
    RoboCheck(strcmp(s, "fatal") == 0);
    s = FontValidationStageString(RoboFontValidationStageContainer);
    RoboCheck(s != NULL);
    RoboCheck(strcmp(s, "container") == 0);
}

static void TestValidSfnt(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontValidationPolicy pol;
    FontValidationResult *result = NULL;
    MinimalFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "ValidateSans";
    spec.ps = "ValidateSans";
    spec.numGlyphs = 2u;
    n = BuildMinimalFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);

    FontValidationPolicyInit(&pol);
    pol.level = RoboFontValidationBasic;
    pol.checksumPolicy = RoboFontChecksumAccept;

    RoboCheckEqInt(FontValidateSource(src, 0u, &pol, &result, NULL), RoboOk);
    RoboCheck(result != NULL);
    RoboCheck(FontValidationResultOk(result));
    RoboCheck(!FontValidationResultHasFatal(result));
    FontValidationResultRelease(result);
    result = NULL;

    LoadFace(font, n, &src, &face);
    RoboCheckEqInt(FontValidateFace(face, &pol, &result), RoboOk);
    RoboCheck(result != NULL);
    RoboCheck(FontValidationResultOk(result));
    RoboCheck(!FontValidationResultHasFatal(result));

    FontValidationResultRelease(result);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestBadSignature(void)
{
    static const uint8_t garbage[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33};
    FontSource *src = NULL;
    FontValidationPolicy pol;
    FontValidationResult *result = NULL;

    RoboCheckEqInt(FontSourceCreateMemoryCopy(garbage, sizeof(garbage), &src),
                      RoboOk);
    FontValidationPolicyInit(&pol);
    pol.level = RoboFontValidationBasic;

    RoboCheckEqInt(FontValidateSource(src, 0u, &pol, &result, NULL),
                      RoboParseError);
    RoboCheck(result != NULL);
    RoboCheck(FontValidationResultHasFatal(result));
    RoboCheck(!FontValidationResultOk(result));
    RoboCheck(FindIssueCode(result, RoboFontValidationInvalidSignature, NULL) ||
               FindIssueCode(result, RoboFontValidationParseFailed, NULL));

    FontValidationResultRelease(result);
    FontSourceRelease(src);
}

static void TestTableCount(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontValidationPolicy pol;
    FontValidationResult *result = NULL;
    MinimalFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "TableLimit";
    spec.ps = "TableLimit";
    spec.numGlyphs = 2u;
    n = BuildMinimalFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);

    FontValidationPolicyInit(&pol);
    pol.level = RoboFontValidationBasic;
    pol.checksumPolicy = RoboFontChecksumAccept;
    pol.limits.maxTables = 1u;
    FontValidationLimitsClamp(&pol.limits);

    RoboCheckEqInt(FontValidateSource(src, 0u, &pol, &result, NULL),
                      RoboParseError);
    RoboCheck(result != NULL);
    RoboCheck(FontValidationResultHasFatal(result));
    RoboCheck(IssueHasFatalCode(result, RoboFontValidationTableCount) ||
               IssueHasFatalCode(result, RoboFontValidationSourceTooLarge));

    FontValidationResultRelease(result);
    FontSourceRelease(src);
}

static void TestChecksum(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontValidationPolicy warnPol;
    FontValidationPolicy rejectPol;
    FontValidationResult *warnResult = NULL;
    FontValidationResult *rejectResult = NULL;
    FontValidationIssue issue;
    MinimalFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "ChecksumTest";
    spec.ps = "ChecksumTest";
    spec.numGlyphs = 2u;
    n = BuildMinimalFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);

    FontValidationPolicyInit(&warnPol);
    warnPol.level = RoboFontValidationBasic;
    warnPol.checksumPolicy = RoboFontChecksumWarn;

    RoboCheckEqInt(FontValidateSource(src, 0u, &warnPol, &warnResult, NULL),
                      RoboOk);
    RoboCheck(warnResult != NULL);
    RoboCheck(FontValidationResultOk(warnResult));
    RoboCheck(FindIssueCode(warnResult, RoboFontValidationChecksumMismatch, &issue));
    RoboCheckEqInt((int)issue.severity, (int)RoboFontValidationSeverityWarning);
    RoboCheck(strcmp(FontValidationIssueCodeString(issue.code),
                      "checksum_mismatch") == 0);
    FontValidationResultRelease(warnResult);
    warnResult = NULL;

    FontValidationPolicyInit(&rejectPol);
    rejectPol.level = RoboFontValidationBasic;
    rejectPol.checksumPolicy = RoboFontChecksumReject;

    RoboCheckEqInt(
        FontValidateSource(src, 0u, &rejectPol, &rejectResult, NULL),
        RoboParseError);
    RoboCheck(rejectResult != NULL);
    RoboCheck(FontValidationResultHasFatal(rejectResult));
    RoboCheck(FindIssueCode(rejectResult, RoboFontValidationChecksumMismatch,
                               &issue));
    RoboCheckEqInt((int)issue.severity, (int)RoboFontValidationSeverityFatal);

    FontValidationResultRelease(rejectResult);
    FontSourceRelease(src);

    /* Deliberately wrong directory checksum on otherwise valid table bytes. */
    {
        static const uint8_t payload[] = {0x11, 0x22, 0x33, 0x44};
        SynthTable tabs[1];
        uint8_t checked[128];
        size_t cn;

        tabs[0].tag = RoboFontTagCmap;
        tabs[0].data = payload;
        tabs[0].length = 4u;
        cn = BuildSfntChecked(checked, sizeof(checked), tabs, 1u);
        RoboCheck(cn > 0u);
        checked[16] ^= 0xFFu; /* corrupt directory checksum field */

        RoboCheckEqInt(
            FontSourceCreateMemoryCopy(checked, cn, &src), RoboOk);
        FontValidationPolicyInit(&rejectPol);
        rejectPol.level = RoboFontValidationBasic;
        rejectPol.checksumPolicy = RoboFontChecksumReject;
        RoboCheckEqInt(
            FontValidateSource(src, 0u, &rejectPol, &rejectResult, NULL),
            RoboParseError);
        RoboCheck(FindIssueCode(rejectResult, RoboFontValidationChecksumMismatch,
                                   NULL));
        FontValidationResultRelease(rejectResult);
        FontSourceRelease(src);
        src = NULL;
    }
}

static void TestGlyphLimit(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontValidationPolicy pol;
    FontValidationResult *result = NULL;
    MinimalFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "GlyphLimit";
    spec.ps = "GlyphLimit";
    spec.numGlyphs = 2u;
    n = BuildMinimalFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);

    FontValidationPolicyInit(&pol);
    pol.level = RoboFontValidationBasic;
    pol.checksumPolicy = RoboFontChecksumAccept;
    pol.limits.maxGlyphs = 1u;
    FontValidationLimitsClamp(&pol.limits);

    RoboCheckEqInt(FontValidateSource(src, 0u, &pol, &result, NULL),
                      RoboParseError);
    RoboCheck(result != NULL);
    RoboCheck(FontValidationResultHasFatal(result));
    RoboCheck(IssueHasFatalCode(result, RoboFontValidationGlyphCount));

    FontValidationResultRelease(result);
    FontSourceRelease(src);
}

static void TestLoader(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontValidationResult *result = NULL;
    MinimalFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "LoaderValidate";
    spec.ps = "LoaderValidate";
    spec.numGlyphs = 2u;
    n = BuildMinimalFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);
    LoadFace(font, n, &src, &face);

    RoboCheckEqInt(FontValidateForLoad(face, RoboFontValidateNormal, &result),
                      RoboOk);
    RoboCheck(result != NULL);
    RoboCheck(FontValidationResultOk(result));
    FontValidationResultRelease(result);
    result = NULL;

    /* PARANOID may warn on synthetic checksums — must not crash. */
    (void)FontValidateForLoad(face, RoboFontValidateParanoid, &result);
    if (result) {
        FontValidationResultRelease(result);
        result = NULL;
    }

    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestColorParanoid(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontValidationPolicy pol;
    FontValidationResult *result = NULL;
    MinimalFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "ColorParanoid";
    spec.ps = "ColorParanoid";
    spec.numGlyphs = 2u;
    spec.withColor = 1;
    n = BuildMinimalFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);
    LoadFace(font, n, &src, &face);

    FontValidationPolicyInit(&pol);
    pol.level = RoboFontValidationParanoid;
    pol.checksumPolicy = RoboFontChecksumWarn;

    RoboCheckEqInt(FontValidateFace(face, &pol, &result), RoboOk);
    RoboCheck(result != NULL);
    RoboCheck(!FontValidationResultHasFatal(result));

    FontValidationResultRelease(result);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestCorpus(void)
{
    static const struct {
        const uint8_t *data;
        size_t size;
    } blobs[] = {
        {kRegressEmpty, sizeof(kRegressEmpty)},
        {kRegressFourZeros, sizeof(kRegressFourZeros)},
        {kRegressTruncatedSfnt, sizeof(kRegressTruncatedSfnt)},
        {kRegressOverlapDir, sizeof(kRegressOverlapDir)},
        {kRegressHugeTableCount, sizeof(kRegressHugeTableCount)},
    };
    FontValidationPolicy pol;
    uint32_t i;
    int anyNotOk = 0;

    FontValidationPolicyInit(&pol);
    pol.level = RoboFontValidationBasic;
    pol.checksumPolicy = RoboFontChecksumAccept;

    for (i = 0u; i < (uint32_t)(sizeof(blobs) / sizeof(blobs[0])); i++) {
        FontSource *src = NULL;
        FontValidationResult *result = NULL;

        RoboCheckEqInt(FontValidateFuzzParse(blobs[i].data, blobs[i].size),
                          RoboOk);

        if (FontSourceCreateMemoryCopy(blobs[i].data, blobs[i].size, &src) ==
            RoboOk) {
            (void)FontValidateSource(src, 0u, &pol, &result, NULL);
            if (result) {
                if (!FontValidationResultOk(result)) {
                    anyNotOk = 1;
                }
                FontValidationResultRelease(result);
            }
            FontSourceRelease(src);
        }
    }
    RoboCheck(anyNotOk);
}

static void TestDiagnostics(void)
{
    static const uint8_t garbage[] = {0xCA, 0xFE, 0xBA, 0xBE, 0x01, 0x02, 0x03, 0x04};
    FontSource *src = NULL;
    FontValidationPolicy pol;
    FontValidationResult *resultA = NULL;
    FontValidationResult *resultB = NULL;
    FontValidationIssue issueA;
    FontValidationIssue issueB;
    const char *codeS;
    const char *sevS;
    const char *stageS;

    RoboCheckEqInt(FontSourceCreateMemoryCopy(garbage, sizeof(garbage), &src),
                      RoboOk);
    FontValidationPolicyInit(&pol);
    pol.level = RoboFontValidationBasic;

    RoboCheckEqInt(FontValidateSource(src, 0u, &pol, &resultA, NULL),
                      RoboParseError);
    RoboCheck(resultA != NULL);
    RoboCheck(FontValidationResultIssueCount(resultA) >= 1u);
    RoboCheckEqInt(FontValidationResultIssue(resultA, 0u, &issueA), RoboOk);

    codeS = FontValidationIssueCodeString(issueA.code);
    sevS = FontValidationSeverityString(issueA.severity);
    stageS = FontValidationStageString(issueA.stage);
    RoboCheck(codeS != NULL && codeS[0] != '\0');
    RoboCheck(sevS != NULL && sevS[0] != '\0');
    RoboCheck(stageS != NULL && stageS[0] != '\0');
    RoboCheckEqInt((int)issueA.severity, (int)RoboFontValidationSeverityFatal);

    RoboCheckEqInt(FontValidateSource(src, 0u, &pol, &resultB, NULL),
                      RoboParseError);
    RoboCheckEqInt(FontValidationResultIssue(resultB, 0u, &issueB), RoboOk);
    RoboCheck(strcmp(issueA.message, issueB.message) == 0);

    FontValidationResultRelease(resultA);
    FontValidationResultRelease(resultB);
    FontSourceRelease(src);
}

static void TestFuzz(void)
{
    uint8_t font[4096];
    uint8_t patterned[512];
    size_t n;
    uint32_t i;
    MinimalFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "FuzzValidate";
    spec.ps = "FuzzValidate";
    spec.numGlyphs = 2u;
    n = BuildMinimalFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);

    RoboCheckEqInt(FontValidateFuzzParse(NULL, 0u), RoboOk);
    RoboCheckEqInt(FontValidateFuzzParse(font, n), RoboOk);

    for (i = 0u; i < sizeof(patterned); i++) {
        patterned[i] = (uint8_t)(0xA5u ^ (uint8_t)(i * 17u + 3u));
    }
    memcpy(patterned, font, n < sizeof(patterned) ? n : sizeof(patterned));
    RoboCheckEqInt(FontValidateFuzzParse(patterned, sizeof(patterned)),
                      RoboOk);

    for (i = 0u; i < 32u; i++) {
        font[i % n] ^= (uint8_t)(i * 13u + 7u);
        RoboCheckEqInt(FontValidateFuzzParse(font, n), RoboOk);
    }
}

static void TestBench(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontValidationPolicy pol;
    int hits = 0;
    int i;
    MinimalFontSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.family = "BenchValidate";
    spec.ps = "BenchValidate";
    spec.numGlyphs = 2u;
    n = BuildMinimalFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);

    FontValidationPolicyInit(&pol);
    pol.level = RoboFontValidationBasic;
    pol.checksumPolicy = RoboFontChecksumAccept;

    for (i = 0; i < 100; i++) {
        FontValidationResult *result = NULL;
        if (FontValidateSource(src, 0u, &pol, &result, NULL) == RoboOk &&
            result && FontValidationResultOk(result)) {
            hits++;
        }
        if (result) {
            FontValidationResultRelease(result);
        }
    }
    printf("font.validate.bench hits=%d\n", hits);
    RoboCheck(hits >= 100);

    FontSourceRelease(src);
}

void TestRegisterFontValidate(void)
{
    TestRegister("font.validate.policy_limits", TestPolicyLimits);
    TestRegister("font.validate.valid_sfnt", TestValidSfnt);
    TestRegister("font.validate.bad_signature", TestBadSignature);
    TestRegister("font.validate.table_count", TestTableCount);
    TestRegister("font.validate.checksum", TestChecksum);
    TestRegister("font.validate.glyph_limit", TestGlyphLimit);
    TestRegister("font.validate.loader", TestLoader);
    TestRegister("font.validate.color_paranoid", TestColorParanoid);
    TestRegister("font.validate.corpus", TestCorpus);
    TestRegister("font.validate.diagnostics", TestDiagnostics);
    TestRegister("font.validate.fuzz", TestFuzz);
    TestRegister("font.validate.bench", TestBench);
}
