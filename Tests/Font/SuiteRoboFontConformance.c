/*
 * RoboFont conformance suite (Goal 5.21).
 *
 * Cross-cutting regression boundary for Goals 5.1–5.20.
 * Synthetic fixtures + documented corpus under tests/fonts/ — no Chromium /
 * system fonts. Filter: -Filter font.conform  (or font.conform.sfnt, …).
 */

#include "RoboFont/RoboFont.h"
#include "RoboFont/RoboFontConformance.h"
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

/* ---- Corpus polarity blobs (mirrored under tests/fonts/) ---- */

static const uint8_t kCorpusTruncatedSfnt[] = {
    0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x10};
static const uint8_t kCorpusHugeTables[] = {
    0x00, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

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

static size_t BuildSfnt(uint8_t *out, size_t outCap, uint32_t version,
                         const SynthTable *tables, uint32_t n)
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

static size_t BuildOs2(uint8_t *buf, uint16_t weight, uint16_t width)
{
    uint8_t *p = buf;
    int i;
    PutU16(&p, 4);
    PutI16(&p, 500);
    PutU16(&p, weight);
    PutU16(&p, width);
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

/* Minimal CFF INDEX/DICT probe bytes (not a full OTTO font). */
static size_t BuildCffIndexProbe(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU16(&p, 2u);
    PutU8(&p, 1u);
    PutU8(&p, 1u);
    PutU8(&p, 2u);
    PutU8(&p, 4u);
    PutU8(&p, 'A');
    PutU8(&p, 'B');
    PutU8(&p, 'C');
    return (size_t)(p - buf);
}

typedef struct {
    const char *family;
    const char *ps;
    uint16_t numGlyphs;
    uint16_t weight;
    uint16_t width;
    int withColor;
    int withCmap;
    uint16_t cmapCp;  /* 0 → U+0041 */
    uint16_t cmapGid; /* 0 → 1 */
} ConformFontSpec;

static size_t BuildConformFont(uint8_t *out, size_t cap, const ConformFontSpec *spec)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[256], loca[256], post[64], os2[128];
    uint8_t name[512], cmap[256], cpal[64], colr[64];
    SynthTable tabs[16];
    uint32_t nt = 0u;
    size_t nn, nc = 0u;
    uint16_t glyphs;
    uint16_t mapCp;
    uint16_t mapGid;

    if (!spec || spec->numGlyphs < 1u) {
        return 0u;
    }
    glyphs = spec->numGlyphs;
    mapCp = spec->cmapCp ? spec->cmapCp : 0x41u;
    mapGid = spec->cmapGid ? spec->cmapGid : 1u;
    nn = BuildNameTable(name, sizeof(name), spec->family, spec->ps);
    if (nn == 0u) {
        return 0u;
    }
    if (spec->withCmap) {
        nc = BuildCmapFmt4Ascii(cmap, mapCp, mapGid);
        if (nc == 0u) {
            return 0u;
        }
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
    tabs[nt++] = (SynthTable){RoboFontTagOs2, os2,
                                  (uint32_t)BuildOs2(os2, spec->weight, spec->width)};
    tabs[nt++] = (SynthTable){RoboFontTagName, name, (uint32_t)nn};
    if (spec->withCmap) {
        tabs[nt++] = (SynthTable){RoboFontTagCmap, cmap, (uint32_t)nc};
    }
    if (spec->withColor) {
        tabs[nt++] =
            (SynthTable){RoboFontTagCpal, cpal, (uint32_t)BuildCpal2x2(cpal)};
        tabs[nt++] = (SynthTable){RoboFontTagColr, colr,
                                      (uint32_t)BuildColrV0Layers(colr)};
    }
    return BuildSfnt(out, cap, RoboFontSfntVersionTrue, tabs, nt);
}

static ConformFontSpec MakeSpec(const char *family, const char *ps, uint16_t glyphs,
                                     uint16_t weight, uint16_t width, int color, int cmap)
{
    ConformFontSpec s;
    memset(&s, 0, sizeof(s));
    s.family = family;
    s.ps = ps;
    s.numGlyphs = glyphs;
    s.weight = weight;
    s.width = width;
    s.withColor = color;
    s.withCmap = cmap;
    return s;
}

static ConformFontSpec MakeSpecCmap(const char *family, const char *ps,
                                          uint16_t glyphs, uint16_t cp, uint16_t gid)
{
    ConformFontSpec s = MakeSpec(family, ps, glyphs, 400u, 5u, 0, 1);
    s.cmapCp = cp;
    s.cmapGid = gid;
    return s;
}

static void LoadFace(const uint8_t *font, size_t n, FontSource **src,
                      FontFace **face)
{
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(*src, 0u, face), RoboOk);
}

/* ---- Tests ---- */

static void TestConformArchitecture(void)
{
    FontConformResult r;
    FontGoldenData g;
    FontConformCase c;

    RoboCheckStrEq(FontConformCategoryString(RoboFontConformSfnt), "sfnt");
    RoboCheckStrEq(FontConformKindString(RoboFontConformKindGolden),
                      "golden");
    RoboCheckStrEq(FontVersionString(), "0.22.0");

    c.id = "font.conform.architecture";
    c.category = RoboFontConformGolden;
    c.kind = RoboFontConformKindUnit;
    c.fixture = "synthetic:none";
    c.purpose = "verify conformance helper API";
    RoboCheck(c.id != NULL);

    FontConformResultInit(&r);
    RoboCheck(r.ok);
    RoboCheckEqInt(FontConformResultAdd(&r, "fx", "head", 1u, 0x41u, 0u, "1",
                                                   "2", "mismatch"),
                      RoboOk);
    RoboCheck(!r.ok);
    RoboCheckEqInt((int)r.diagnosticCount, 1);
    RoboCheckStrEq(r.diagnostics[0].expected, "1");
    RoboCheckStrEq(r.diagnostics[0].actual, "2");

    FontGoldenInit(&g);
    RoboCheckEqInt(FontGoldenAppendU32(&g, "n", 3u), RoboOk);
    RoboCheckEqInt(FontGoldenAppendI32(&g, "d", -2), RoboOk);
    RoboCheckEqInt(FontGoldenAppendStr(&g, "s", "ok"), RoboOk);
    RoboCheck(FontGoldenEqualsCstr(&g, "n=3\nd=-2\ns=ok\n"));
}

static void TestConformSfnt(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    FontSfntTableRecord rec;
    FontSpan span;
    ConformFontSpec spec = MakeSpec("ConformSFNT", "ConformSFNT", 2u, 400u, 5u, 0, 1);

    n = BuildConformFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontSfntParse(src, 0u, RoboFontSfntFlagsNone, &sfnt),
                      RoboOk);
    RoboCheck(FontSfntTableCount(sfnt) >= 10u);
    RoboCheckEqInt(FontSfntFindTable(sfnt, RoboFontTagHead, &rec), RoboOk);
    RoboCheckEqInt(FontSfntTableBytes(sfnt, RoboFontTagHead, &span), RoboOk);
    RoboCheck(span.size >= 54u);
    RoboCheckEqInt(FontSfntFindTable(sfnt, RoboFontTagMaxp, &rec), RoboOk);
    RoboCheck(FontSfntHasTable(sfnt, RoboFontTagMaxp));
    FontSfntRelease(sfnt);
    FontSourceRelease(src);

    /* Malformed truncated directory must fail safely. */
    RoboCheckEqInt(
        FontSourceCreateMemoryCopy(kCorpusTruncatedSfnt,
                                            sizeof(kCorpusTruncatedSfnt), &src),
        RoboOk);
    RoboCheck(FontSfntParse(src, 0u, RoboFontSfntFlagsNone, &sfnt) != RoboOk);
    FontSourceRelease(src);
}

static void TestConformTruetype(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    uint32_t glyphs = 0u;
    FontMetrics m;
    ConformFontSpec spec = MakeSpec("ConformTT", "ConformTT", 3u, 400u, 5u, 0, 1);

    n = BuildConformFont(font, sizeof(font), &spec);
    LoadFace(font, n, &src, &face);
    RoboCheckEqInt(FontFaceParseTruetype(face, 0u), RoboOk);
    RoboCheck(FontFaceTruetype(face) != NULL);
    RoboCheckEqInt(FontFaceGlyphCount(face, &glyphs), RoboOk);
    RoboCheckEqInt((int)glyphs, 3);
    RoboCheckEqInt(FontFaceParseMetrics(face), RoboOk);
    RoboCheckEqInt(FontFaceLayoutMetrics(face, &m), RoboOk);
    RoboCheckEqInt((int)m.unitsPerEm, 1000);
    RoboCheckEqInt((int)m.ascent, 800);
    RoboCheckEqInt((int)m.descent, 200);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestConformOpentype(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    ConformFontSpec spec = MakeSpec("ConformOT", "ConformOT", 2u, 400u, 5u, 0, 1);

    n = BuildConformFont(font, sizeof(font), &spec);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontSfntParse(src, 0u, RoboFontSfntFlagsNone, &sfnt),
                      RoboOk);
    RoboCheckEqInt((int)FontOtDetectOutline(sfnt),
                      (int)RoboFontOutlineTruetype);
    FontSfntRelease(sfnt);
    FontSourceRelease(src);
}

static void TestConformCff(void)
{
    uint8_t buf[64];
    size_t n;
    FontCffIndex idx;
    FontSpan span;
    FontSpan obj;
    size_t end = 0u;

    n = BuildCffIndexProbe(buf);
    span.data = buf;
    span.size = n;
    memset(&idx, 0, sizeof(idx));
    RoboCheckEqInt(FontCffIndexParse(span, 0, &idx, &end), RoboOk);
    RoboCheckEqInt((int)FontCffIndexCount(&idx), 2);
    RoboCheckEqInt(FontCffIndexGet(&idx, 0, &obj), RoboOk);
    RoboCheckEqInt((int)obj.size, 1);
    RoboCheckEqInt(obj.data[0], 'A');
    FontCffIndexClear(&idx);
}

static void TestConformCmap(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontCmapLookupResult lr;
    FontGoldenData g;
    ConformFontSpec spec = MakeSpec("ConformCmap", "ConformCmap", 2u, 400u, 5u, 0, 1);

    n = BuildConformFont(font, sizeof(font), &spec);
    LoadFace(font, n, &src, &face);
    RoboCheckEqInt(FontFaceParseTruetype(face, 0u), RoboOk);
    RoboCheckEqInt(FontFaceParseCmap(face), RoboOk);
    RoboCheckEqInt(FontFaceLookupGlyph(face, 0x41u, &lr), RoboOk);
    RoboCheckEqInt((int)lr.status, (int)RoboFontCmapMapped);
    RoboCheckEqInt((int)lr.glyphId, 1);
    RoboCheckEqInt(FontFaceLookupGlyph(face, 0x42u, &lr), RoboOk);
    RoboCheckEqInt((int)lr.status, (int)RoboFontCmapUnmapped);

    /* Surrogate / out-of-range must not map. */
    RoboCheckEqInt(FontFaceLookupGlyph(face, 0xD800u, &lr), RoboOk);
    RoboCheck((int)lr.status != (int)RoboFontCmapMapped);
    RoboCheckEqInt(FontFaceLookupGlyph(face, 0x110000u, &lr), RoboOk);
    RoboCheck((int)lr.status != (int)RoboFontCmapMapped);

    FontGoldenInit(&g);
    RoboCheckEqInt(FontGoldenAppendU32(&g, "U+0041", 1u), RoboOk);
    RoboCheck(FontGoldenEqualsCstr(&g, "U+0041=1\n"));

    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestConformMetrics(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontMetrics m;
    FontGlyphMetrics gm;
    FontGoldenData g;
    ConformFontSpec spec = MakeSpec("ConformMetrics", "ConformMetrics", 2u, 400u, 5u, 0, 1);

    n = BuildConformFont(font, sizeof(font), &spec);
    LoadFace(font, n, &src, &face);
    RoboCheckEqInt(FontFaceParseTruetype(face, 0u), RoboOk);
    RoboCheckEqInt(FontFaceParseMetrics(face), RoboOk);
    RoboCheckEqInt(FontFaceLayoutMetrics(face, &m), RoboOk);
    RoboCheckEqInt(FontFaceGlyphMetrics(face, 1u, &gm), RoboOk);
    RoboCheckEqInt((int)gm.status, (int)RoboFontGlyphMetricsOk);
    RoboCheckEqInt((int)gm.advanceWidth, 500);
    RoboCheckEqInt((int)gm.leftSideBearing, 10);

    FontGoldenInit(&g);
    RoboCheckEqInt(FontGoldenAppendU32(&g, "upem", m.unitsPerEm), RoboOk);
    RoboCheckEqInt(FontGoldenAppendI32(&g, "ascent", (int32_t)m.ascent),
                      RoboOk);
    RoboCheckEqInt(FontGoldenAppendI32(&g, "descent", (int32_t)m.descent),
                      RoboOk);
    RoboCheckEqInt(FontGoldenAppendU32(&g, "adv1", gm.advanceWidth), RoboOk);
    RoboCheck(FontGoldenEqualsCstr(
        &g, "upem=1000\nascent=800\ndescent=200\nadv1=500\n"));

    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestConformNames(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    const FontMetadata *meta;
    ConformFontSpec spec = MakeSpec("ConformName", "ConformNamePS", 2u, 400u, 5u, 0, 1);

    n = BuildConformFont(font, sizeof(font), &spec);
    LoadFace(font, n, &src, &face);
    RoboCheckEqInt(FontFaceParseTruetype(face, 0u), RoboOk);
    RoboCheckEqInt(FontFaceParseMetadata(face), RoboOk);
    meta = FontFaceMetadata(face);
    RoboCheck(meta != NULL);
    RoboCheckStrEq(meta->familyName.utf8, "ConformName");
    RoboCheckStrEq(meta->postscriptName.utf8, "ConformNamePS");
    RoboCheckStrEq(meta->subfamilyName.utf8, "Regular");
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestConformStyle(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    const FontClassification *cls;
    ConformFontSpec spec = MakeSpec("ConformStyle", "ConformStyle", 2u, 700u, 5u, 0, 1);

    n = BuildConformFont(font, sizeof(font), &spec);
    LoadFace(font, n, &src, &face);
    RoboCheckEqInt(FontFaceParseTruetype(face, 0u), RoboOk);
    RoboCheckEqInt(FontFaceParseClassification(face), RoboOk);
    cls = FontFaceClassification(face);
    RoboCheck(cls != NULL);
    RoboCheckEqInt((int)cls->weight, 700);
    RoboCheckEqInt((int)cls->style, (int)RoboFontStyleNormal);
    RoboCheckEqInt((int)cls->stretch, 100);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestConformSelection(void)
{
    uint8_t fa[4096], fb[4096];
    size_t na, nb;
    FontSource *sa = NULL, *sb = NULL;
    FontFace *a = NULL, *b = NULL;
    FontFace *cands[2];
    FontSelectRequest req;
    FontSelection sel;
    ConformFontSpec saSpec = MakeSpec("SelProbe", "SelProbe", 2u, 400u, 5u, 0, 1);
    ConformFontSpec sbSpec = MakeSpec("SelProbe", "SelProbeBold", 2u, 700u, 5u, 0, 1);

    na = BuildConformFont(fa, sizeof(fa), &saSpec);
    nb = BuildConformFont(fb, sizeof(fb), &sbSpec);
    LoadFace(fa, na, &sa, &a);
    LoadFace(fb, nb, &sb, &b);
    RoboCheckEqInt(FontFaceParseTruetype(a, 0u), RoboOk);
    RoboCheckEqInt(FontFaceParseTruetype(b, 0u), RoboOk);
    RoboCheckEqInt(FontFaceParseMetadata(a), RoboOk);
    RoboCheckEqInt(FontFaceParseMetadata(b), RoboOk);
    RoboCheckEqInt(FontFaceParseClassification(a), RoboOk);
    RoboCheckEqInt(FontFaceParseClassification(b), RoboOk);

    cands[0] = a;
    cands[1] = b;
    FontSelectRequestInit(&req);
    req.family = "selprobe";
    req.weight = 700;
    req.stretch = 100;
    req.style = RoboFontStyleNormal;
    RoboCheckEqInt(FontSelect(&req, cands, 2u, &sel), RoboOk);
    RoboCheckEqInt(sel.found, 1);
    RoboCheck(sel.face == b);

    /* Determinism: same request twice → same face. */
    {
        FontSelection sel2;
        RoboCheckEqInt(FontSelect(&req, cands, 2u, &sel2), RoboOk);
        RoboCheck(sel2.face == sel.face);
        FontSelectionClear(&sel2);
    }
    FontSelectionClear(&sel);
    FontFaceRelease(a);
    FontFaceRelease(b);
    FontSourceRelease(sa);
    FontSourceRelease(sb);
}

static void TestConformDiscovery(void)
{
    uint8_t ttf[4] = {0x00, 0x01, 0x00, 0x00};
    uint8_t otto[4] = {'O', 'T', 'T', 'O'};
    uint8_t ttc[4] = {'t', 't', 'c', 'f'};

    RoboCheckEqInt(FontDiscoveryClassifyBytes(ttf, 4), RoboFontCandidateTtf);
    RoboCheckEqInt(FontDiscoveryClassifyBytes(otto, 4), RoboFontCandidateOtf);
    RoboCheckEqInt(FontDiscoveryClassifyBytes(ttc, 4),
                      RoboFontCandidateCollection);
    RoboCheck(FontDiscoveryExtensionIsFont("Fonts/Probe.ttf"));
    RoboCheck(FontDiscoveryExtensionIsFont("X.otc"));
    RoboCheck(!FontDiscoveryExtensionIsFont("readme.txt"));
}

static void TestConformLoading(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontValidationResult *vres = NULL;
    FontFace *face = NULL;
    ConformFontSpec spec = MakeSpec("ConformLoad", "ConformLoad", 2u, 400u, 5u, 0, 1);

    n = BuildConformFont(font, sizeof(font), &spec);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0u, &face), RoboOk);
    RoboCheckEqInt(
        FontValidateForLoad(face, RoboFontValidateNormal, &vres), RoboOk);
    if (vres) {
        RoboCheck(FontValidationResultOk(vres));
        FontValidationResultRelease(vres);
        vres = NULL;
    }
    RoboCheckEqInt(FontFaceParseTruetype(face, 0u), RoboOk);
    FontFaceRelease(face);
    FontSourceRelease(src);

    /* Malformed source must fail structural validate before trusted use. */
    RoboCheckEqInt(
        FontSourceCreateMemoryCopy(kCorpusTruncatedSfnt,
                                            sizeof(kCorpusTruncatedSfnt), &src),
        RoboOk);
    {
        FontValidationPolicy pol;
        FontValidationPolicyInit(&pol);
        pol.level = RoboFontValidationStrict;
        RoboCheck(FontValidateSource(src, 0u, &pol, &vres, NULL) != RoboOk ||
                   (vres && !FontValidationResultOk(vres)));
        if (vres) {
            FontValidationResultRelease(vres);
        }
    }
    FontSourceRelease(src);
}

static void TestConformCoverage(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    const FontCoverage *cov;
    ConformFontSpec spec = MakeSpec("ConformCov", "ConformCov", 2u, 400u, 5u, 0, 1);

    n = BuildConformFont(font, sizeof(font), &spec);
    LoadFace(font, n, &src, &face);
    RoboCheckEqInt(FontFaceParseTruetype(face, 0u), RoboOk);
    RoboCheckEqInt(FontFaceParseCmap(face), RoboOk);
    RoboCheckEqInt(FontFaceParseCoverage(face), RoboOk);
    cov = FontFaceCoverage(face);
    RoboCheck(cov != NULL);
    RoboCheck(FontCoverageContains(cov, 0x41u));
    RoboCheck(!FontCoverageContains(cov, 0x42u));
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestConformFallback(void)
{
    uint8_t latin[4096], cover[4096];
    size_t nl, nc;
    FontRegistry *reg = NULL;
    FontDiscoverySourceId s1 = 0u, s2 = 0u;
    FontDiscoveryResult dr;
    FontFallbackResolver *resolver = NULL;
    FontFallbackRequest req;
    FontFallbackResult res;
    uint32_t cp = 0x41u;
    FontFace *primary = NULL;
    FontFace *covered = NULL;
    /* Primary covers 'B' only; alt covers 'A' — request U+0041 must fall back. */
    ConformFontSpec miss = MakeSpecCmap("FbMiss", "FbMiss", 2u, 0x42u, 1u);
    ConformFontSpec hit = MakeSpecCmap("FbHit", "FbHit", 2u, 0x41u, 1u);
    uint32_t i;

    nl = BuildConformFont(latin, sizeof(latin), &miss);
    nc = BuildConformFont(cover, sizeof(cover), &hit);
    RoboCheck(nl > 0u && nc > 0u);

    RoboCheckEqInt(FontRegistryCreate(&reg), RoboOk);
    RoboCheckEqInt(
        FontRegistryAddMemorySource(reg, "mem:miss", latin, nl, &s1), RoboOk);
    RoboCheckEqInt(
        FontRegistryAddMemorySource(reg, "mem:hit", cover, nc, &s2), RoboOk);
    RoboCheckEqInt(FontRegistryScanAll(reg, 0u, &dr), RoboOk);
    RoboCheck((int)FontRegistryRegistrationCount(reg) >= 2);

    for (i = 0u; i < FontRegistryRegistrationCount(reg); i++) {
        const FontRegistration *rec = NULL;
        RoboCheckEqInt(FontRegistryRegistrationAt(reg, i, &rec), RoboOk);
        if (rec && strcmp(rec->family, "FbMiss") == 0) {
            primary = rec->face;
        }
        if (rec && strcmp(rec->family, "FbHit") == 0) {
            covered = rec->face;
        }
    }
    RoboCheck(primary != NULL && covered != NULL);

    RoboCheckEqInt(FontFallbackResolverCreate(reg, NULL, &resolver), RoboOk);
    FontFallbackRequestInit(&req);
    req.primary = primary;
    req.codepoints = &cp;
    req.codepointCount = 1u;
    FontFallbackResultInit(&res);
    RoboCheckEqInt(FontFallbackResolve(resolver, &req, &res), RoboOk);
    RoboCheckEqInt(res.found, 1);
    RoboCheck(res.face == covered || res.usedPrimary);
    if (!res.usedPrimary) {
        RoboCheck(res.face == covered);
    }
    FontFallbackResultClear(&res);
    FontFallbackResolverDestroy(resolver);
    FontRegistryDestroy(reg);
}

static void TestConformGeneric(void)
{
    FontGenericFamily g = RoboFontGenericUnknown;
    uint32_t i;

    RoboCheck(!FontGenericFamilyIsValid(RoboFontGenericUnknown));
    for (i = 1u; i < (uint32_t)RoboFontGenericCount; i++) {
        RoboCheck(FontGenericFamilyIsValid((FontGenericFamily)i));
        RoboCheck(FontGenericFamilyName((FontGenericFamily)i) != NULL);
    }
    RoboCheckEqInt(FontGenericFamilyFromName("serif", 0u, &g), RoboOk);
    RoboCheckEqInt((int)g, (int)RoboFontGenericSerif);
    RoboCheckEqInt(FontGenericFamilyFromName("sans-serif", 0u, &g), RoboOk);
    RoboCheckEqInt((int)g, (int)RoboFontGenericSansSerif);
    RoboCheckEqInt(FontGenericFamilyFromName("monospace", 0u, &g), RoboOk);
    RoboCheckEqInt(FontGenericFamilyFromName("system-ui", 0u, &g), RoboOk);
    RoboCheckEqInt(FontGenericFamilyFromName("emoji", 0u, &g), RoboOk);
    RoboCheckEqInt(FontGenericFamilyFromName("math", 0u, &g), RoboOk);
    RoboCheckEqInt(FontGenericFamilyFromName("ui-serif", 0u, &g), RoboOk);
    RoboCheckEqInt(FontGenericFamilyFromName("ui-sans-serif", 0u, &g), RoboOk);
    RoboCheckEqInt(FontGenericFamilyFromName("ui-monospace", 0u, &g), RoboOk);
    RoboCheckEqInt(FontGenericFamilyFromName("ui-rounded", 0u, &g), RoboOk);
    RoboCheckEqInt(FontGenericFamilyFromName("cursive", 0u, &g), RoboOk);
    RoboCheckEqInt(FontGenericFamilyFromName("fantasy", 0u, &g), RoboOk);
    RoboCheckEqInt(FontGenericFamilyFromName("not-a-generic", 0u, &g),
                      RoboNotFound);
}

static void TestConformEmoji(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    ConformFontSpec spec = MakeSpec("ConformEmoji", "ConformEmoji", 2u, 400u, 5u, 1, 1);

    n = BuildConformFont(font, sizeof(font), &spec);
    LoadFace(font, n, &src, &face);
    RoboCheckEqInt(FontFaceParseTruetype(face, 0u), RoboOk);
    RoboCheckEqInt(FontFaceParseColor(face), RoboOk);
    RoboCheckEqInt(FontFaceParseEmoji(face), RoboOk);
    RoboCheck(FontFaceEmoji(face) != NULL);
    /* VS15/VS16 scalars are valid Unicode; classification must not crash. */
    {
        FontCmapLookupResult lr;
        (void)FontFaceParseCmap(face);
        (void)FontFaceLookupGlyph(face, 0xFE0Eu, &lr);
        (void)FontFaceLookupGlyph(face, 0xFE0Fu, &lr);
    }
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestConformVariation(void)
{
    float out = 0.0f;
    FontGoldenData g;

    RoboCheckEqInt(
        FontVariationNormalize(100.0f, 400.0f, 900.0f, 100.0f, &out), RoboOk);
    RoboCheck(out == -1.0f);
    RoboCheckEqInt(
        FontVariationNormalize(100.0f, 400.0f, 900.0f, 400.0f, &out), RoboOk);
    RoboCheck(out == 0.0f);
    RoboCheckEqInt(
        FontVariationNormalize(100.0f, 400.0f, 900.0f, 900.0f, &out), RoboOk);
    RoboCheck(out == 1.0f);

    /* Property: clamp then normalize is monotonic for midpoints. */
    {
        float a = 0.0f, b = 0.0f;
        float ca, cb;
        RoboCheckEqInt(
            FontVariationClampUser(100.0f, 400.0f, 900.0f, 50.0f, &ca), RoboOk);
        RoboCheckEqInt(
            FontVariationClampUser(100.0f, 400.0f, 900.0f, 950.0f, &cb), RoboOk);
        RoboCheck(ca == 100.0f);
        RoboCheck(cb == 900.0f);
        RoboCheckEqInt(
            FontVariationNormalize(100.0f, 400.0f, 900.0f, 250.0f, &a), RoboOk);
        RoboCheckEqInt(
            FontVariationNormalize(100.0f, 400.0f, 900.0f, 650.0f, &b), RoboOk);
        RoboCheck(a < 0.0f && b > 0.0f);
    }

    FontGoldenInit(&g);
    RoboCheckEqInt(
        FontVariationNormalize(100.0f, 400.0f, 900.0f, 400.0f, &out), RoboOk);
    RoboCheckEqInt(FontGoldenAppendF32(&g, "wght_def", out), RoboOk);
    RoboCheck(FontGoldenEqualsCstr(&g, "wght_def=0.000000\n"));
}

static void TestConformColor(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    const FontColorInfo *ci;
    ConformFontSpec spec = MakeSpec("ConformColor", "ConformColor", 4u, 400u, 5u, 1, 1);

    n = BuildConformFont(font, sizeof(font), &spec);
    LoadFace(font, n, &src, &face);
    RoboCheckEqInt(FontFaceParseTruetype(face, 0u), RoboOk);
    RoboCheckEqInt(FontFaceParseColor(face), RoboOk);
    ci = FontFaceColor(face);
    RoboCheck(ci != NULL);
    RoboCheck(FontColorInfoIsColorCapable(ci));
    RoboCheck(FontColorInfoCapabilities(ci) != 0u);
    /* SVG remains inert — no execute path exists; color info is data only. */
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestConformSecurity(void)
{
    FontSource *src = NULL;
    FontValidationPolicy pol;
    FontValidationResult *vres = NULL;

    FontValidationPolicyInit(&pol);
    pol.level = RoboFontValidationStrict;

    RoboCheckEqInt(
        FontSourceCreateMemoryCopy(kCorpusTruncatedSfnt,
                                            sizeof(kCorpusTruncatedSfnt), &src),
        RoboOk);
    RoboCheck(FontValidateSource(src, 0u, &pol, &vres, NULL) != RoboOk ||
               (vres && !FontValidationResultOk(vres)));
    if (vres) {
        FontValidationResultRelease(vres);
        vres = NULL;
    }
    FontSourceRelease(src);

    RoboCheckEqInt(
        FontSourceCreateMemoryCopy(kCorpusHugeTables,
                                            sizeof(kCorpusHugeTables), &src),
        RoboOk);
    RoboCheck(FontValidateSource(src, 0u, &pol, &vres, NULL) != RoboOk ||
               (vres && !FontValidationResultOk(vres)));
    if (vres) {
        FontValidationResultRelease(vres);
    }
    FontSourceRelease(src);

    RoboCheckEqInt(FontValidateFuzzParse(kCorpusTruncatedSfnt,
                                                    sizeof(kCorpusTruncatedSfnt)),
                      RoboOk);
}

static void TestConformLimits(void)
{
    FontValidationLimits lim;
    FontValidationPolicy pol;
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontValidationResult *vres = NULL;
    ConformFontSpec spec = MakeSpec("ConformLim", "ConformLim", 2u, 400u, 5u, 0, 1);

    FontValidationLimitsInit(&lim);
    /* Below hard max: clamp leaves value. */
    lim.maxGlyphs = 100u;
    FontValidationLimitsClamp(&lim);
    RoboCheckEqInt((int)lim.maxGlyphs, 100);

    /* At hard max. */
    lim.maxGlyphs = RoboFontValidationHardMaxGlyphs;
    FontValidationLimitsClamp(&lim);
    RoboCheckEqInt((int)lim.maxGlyphs, (int)RoboFontValidationHardMaxGlyphs);

    /* Above hard max → clamped down. */
    lim.maxGlyphs = RoboFontValidationHardMaxGlyphs + 1u;
    FontValidationLimitsClamp(&lim);
    RoboCheckEqInt((int)lim.maxGlyphs, (int)RoboFontValidationHardMaxGlyphs);

    /* Tiny glyph budget rejects our 2-glyph font under STRICT. */
    n = BuildConformFont(font, sizeof(font), &spec);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    FontValidationPolicyInit(&pol);
    pol.level = RoboFontValidationStrict;
    pol.rejectOversized = 1u;
    pol.limits = lim;
    pol.limits.maxGlyphs = 1u;
    FontValidationLimitsClamp(&pol.limits);
    RoboCheck(FontValidateSource(src, 0u, &pol, &vres, NULL) != RoboOk ||
               (vres && !FontValidationResultOk(vres)));
    if (vres) {
        FontValidationResultRelease(vres);
    }
    FontSourceRelease(src);
}

static void TestConformCrossTable(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    uint32_t glyphs = 0u;
    FontGlyphMetrics gm;
    ConformFontSpec spec = MakeSpec("ConformX", "ConformX", 3u, 400u, 5u, 0, 1);

    n = BuildConformFont(font, sizeof(font), &spec);
    LoadFace(font, n, &src, &face);
    RoboCheckEqInt(FontFaceParseTruetype(face, 0u), RoboOk);
    RoboCheckEqInt(FontFaceGlyphCount(face, &glyphs), RoboOk);
    RoboCheckEqInt((int)glyphs, 3);
    RoboCheckEqInt(FontFaceParseMetrics(face), RoboOk);
    /* maxp ↔ hmtx: every glyph id in range has metrics. */
    RoboCheckEqInt(FontFaceGlyphMetrics(face, 0u, &gm), RoboOk);
    RoboCheckEqInt((int)gm.status, (int)RoboFontGlyphMetricsOk);
    RoboCheckEqInt(FontFaceGlyphMetrics(face, 2u, &gm), RoboOk);
    RoboCheckEqInt((int)gm.status, (int)RoboFontGlyphMetricsOk);
    RoboCheckEqInt(FontFaceGlyphMetrics(face, 3u, &gm), RoboOk);
    RoboCheck((int)gm.status != (int)RoboFontGlyphMetricsOk);

    /* cmap ↔ maxp: mapped gid must be < numGlyphs. */
    RoboCheckEqInt(FontFaceParseCmap(face), RoboOk);
    {
        FontCmapLookupResult lr;
        RoboCheckEqInt(FontFaceLookupGlyph(face, 0x41u, &lr), RoboOk);
        RoboCheckEqInt((int)lr.status, (int)RoboFontCmapMapped);
        RoboCheck(lr.glyphId < glyphs);
    }
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestConformGolden(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    const FontMetadata *meta;
    FontMetrics m;
    FontGoldenData g;
    ConformFontSpec spec = MakeSpec("GoldenFace", "GoldenFacePS", 2u, 400u, 5u, 0, 1);

    n = BuildConformFont(font, sizeof(font), &spec);
    LoadFace(font, n, &src, &face);
    RoboCheckEqInt(FontFaceParseTruetype(face, 0u), RoboOk);
    RoboCheckEqInt(FontFaceParseMetadata(face), RoboOk);
    RoboCheckEqInt(FontFaceParseMetrics(face), RoboOk);
    meta = FontFaceMetadata(face);
    RoboCheckEqInt(FontFaceLayoutMetrics(face, &m), RoboOk);

    FontGoldenInit(&g);
    RoboCheckEqInt(FontGoldenAppendStr(&g, "family", meta->familyName.utf8),
                      RoboOk);
    RoboCheckEqInt(
        FontGoldenAppendStr(&g, "ps", meta->postscriptName.utf8), RoboOk);
    RoboCheckEqInt(FontGoldenAppendU32(&g, "upem", m.unitsPerEm), RoboOk);
    RoboCheck(FontGoldenEqualsCstr(
        &g, "family=GoldenFace\nps=GoldenFacePS\nupem=1000\n"));

    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestConformProperty(void)
{
    uint32_t i;
    float prev = -2.0f;

    /* Property: normalize(min..max) is non-decreasing for increasing user values. */
    for (i = 0u; i <= 10u; i++) {
        float user = 100.0f + (float)i * 80.0f;
        float nrm = 0.0f;
        RoboCheckEqInt(
            FontVariationNormalize(100.0f, 400.0f, 900.0f, user, &nrm), RoboOk);
        RoboCheck(nrm >= prev);
        prev = nrm;
    }

    /* Property: coverage of unmapped codepoint is false when cmap present. */
    {
        uint8_t font[4096];
        size_t n;
        FontSource *src = NULL;
        FontFace *face = NULL;
        const FontCoverage *cov;
        ConformFontSpec spec = MakeSpec("PropCov", "PropCov", 2u, 400u, 5u, 0, 1);
        n = BuildConformFont(font, sizeof(font), &spec);
        LoadFace(font, n, &src, &face);
        RoboCheckEqInt(FontFaceParseTruetype(face, 0u), RoboOk);
        RoboCheckEqInt(FontFaceParseCmap(face), RoboOk);
        RoboCheckEqInt(FontFaceParseCoverage(face), RoboOk);
        cov = FontFaceCoverage(face);
        for (i = 0u; i < 26u; i++) {
            uint32_t cp = 0x61u + i; /* a-z unmapped */
            RoboCheck(!FontCoverageContains(cov, cp));
        }
        RoboCheck(FontCoverageContains(cov, 0x41u));
        FontFaceRelease(face);
        FontSourceRelease(src);
    }
}

static void TestConformFuzz(void)
{
    uint8_t junk[64];
    uint32_t i;
    uint8_t font[4096];
    size_t n;
    ConformFontSpec spec = MakeSpec("FuzzFace", "FuzzFace", 2u, 400u, 5u, 1, 1);

    for (i = 0u; i < sizeof(junk); i++) {
        junk[i] = (uint8_t)(i * 37u);
    }
    RoboCheckEqInt(FontConformFuzzSmoke(NULL, 0u), RoboOk);
    RoboCheckEqInt(FontConformFuzzSmoke(junk, sizeof(junk)), RoboOk);
    RoboCheckEqInt(
        FontConformFuzzSmoke(kCorpusTruncatedSfnt, sizeof(kCorpusTruncatedSfnt)),
        RoboOk);

    n = BuildConformFont(font, sizeof(font), &spec);
    RoboCheckEqInt(FontConformFuzzSmoke(font, n), RoboOk);
    RoboCheckEqInt(FontColorFuzzParse(font, n), RoboOk);
    RoboCheckEqInt(FontVariationFuzzParse(font, n), RoboOk);
    RoboCheckEqInt(FontSelectionFuzzParse(font, n), RoboOk);
}

static void TestConformBench(void)
{
    uint8_t font[4096];
    size_t n;
    uint32_t i;
    uint32_t hits = 0u;
    ConformFontSpec spec = MakeSpec("BenchFace", "BenchFace", 2u, 400u, 5u, 0, 1);

    n = BuildConformFont(font, sizeof(font), &spec);
    RoboCheck(n > 0u);

    for (i = 0u; i < 50u; i++) {
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
    printf("font.conform.bench hits=%u\n", (unsigned)hits);
    RoboCheckEqInt((int)hits, 50);
}

void TestRegisterFontConformance(void)
{
    TestRegister("font.conform.architecture", TestConformArchitecture);
    TestRegister("font.conform.sfnt", TestConformSfnt);
    TestRegister("font.conform.truetype", TestConformTruetype);
    TestRegister("font.conform.opentype", TestConformOpentype);
    TestRegister("font.conform.cff", TestConformCff);
    TestRegister("font.conform.cmap", TestConformCmap);
    TestRegister("font.conform.metrics", TestConformMetrics);
    TestRegister("font.conform.names", TestConformNames);
    TestRegister("font.conform.style", TestConformStyle);
    TestRegister("font.conform.selection", TestConformSelection);
    TestRegister("font.conform.discovery", TestConformDiscovery);
    TestRegister("font.conform.loading", TestConformLoading);
    TestRegister("font.conform.coverage", TestConformCoverage);
    TestRegister("font.conform.fallback", TestConformFallback);
    TestRegister("font.conform.generic", TestConformGeneric);
    TestRegister("font.conform.emoji", TestConformEmoji);
    TestRegister("font.conform.variation", TestConformVariation);
    TestRegister("font.conform.color", TestConformColor);
    TestRegister("font.conform.security", TestConformSecurity);
    TestRegister("font.conform.limits", TestConformLimits);
    TestRegister("font.conform.cross_table", TestConformCrossTable);
    TestRegister("font.conform.golden", TestConformGolden);
    TestRegister("font.conform.property", TestConformProperty);
    TestRegister("font.conform.fuzz", TestConformFuzz);
    TestRegister("font.conform.bench", TestConformBench);
}
