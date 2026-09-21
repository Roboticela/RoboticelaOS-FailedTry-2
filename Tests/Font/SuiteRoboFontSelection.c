/*
 * RoboFont selection suite (Goal 5.11).
 * Synthetic fixtures only — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

#include <string.h>

#ifndef RoboFontNameIdTypoFamily
#define RoboFontNameIdTypoFamily 16u
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

static size_t BuildPost(uint8_t *b, int32_t angle)
{
    uint8_t *p = b;
    PutU32(&p, 0x00030000u);
    PutU32(&p, (uint32_t)angle);
    PutI16(&p, -100);
    PutI16(&p, 50);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    return (size_t)(p - b);
}

static size_t BuildOs2(uint8_t *buf, uint16_t weight, uint16_t width, uint16_t fs)
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
    PutU16(&p, fs);
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

/**
 * name table with Windows Unicode records.
 * nameIds[] / strings[] parallel arrays; count records.
 */
static size_t BuildNameTable(uint8_t *buf, size_t cap, const uint16_t *nameIds,
                               const char *const *strings, uint32_t count)
{
    uint8_t *p = buf;
    uint32_t i;
    size_t storage;
    size_t header = 6u + (size_t)count * 12u;

    if (cap < header) {
        return 0u;
    }
    PutU16(&p, 0); /* format */
    PutU16(&p, (uint16_t)count);
    PutU16(&p, (uint16_t)header);
    storage = header;
    for (i = 0u; i < count; i++) {
        size_t slen = strlen(strings[i]) * 2u;
        uint8_t *rec = buf + 6u + (size_t)i * 12u;
        uint8_t *rp = rec;
        if (storage + slen > cap) {
            return 0u;
        }
        PutU16(&rp, 3); /* Windows */
        PutU16(&rp, 1); /* Unicode BMP */
        PutU16(&rp, 0x0409);
        PutU16(&rp, nameIds[i]);
        PutU16(&rp, (uint16_t)slen);
        PutU16(&rp, (uint16_t)(storage - header));
        {
            uint8_t *sp = buf + storage;
            PutUtf16beAscii(&sp, strings[i]);
        }
        storage += slen;
    }
    return storage;
}

#define SEL_FACE_FONT_CAP 2048u

typedef struct {
    uint8_t *font;
    size_t fontN;
    FontSource *src;
    FontFace *face;
} SelFace;

static void SelFaceRelease(SelFace *f)
{
    if (!f) {
        return;
    }
    if (f->face) {
        FontFaceRelease(f->face);
        f->face = NULL;
    }
    if (f->src) {
        FontSourceRelease(f->src);
        f->src = NULL;
    }
    if (f->font) {
        RoboFree(f->font);
        f->font = NULL;
    }
    f->fontN = 0u;
}

static int SelFaceBuild(SelFace *f, const char *legacyFamily, const char *typoOpt,
                          const char *wwsOpt, uint16_t weight, uint16_t width,
                          uint16_t fs, int32_t postAngle)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[8], loca[8], post[64], os2[128], name[512];
    uint16_t ids[8];
    const char *strs[8];
    uint32_t nc = 0u;
    size_t nn;
    /* head/hhea/maxp/hmtx/loca/glyf/post/os2/name — nine tables */
    SynthTable tabs[9];
    uint32_t nt = 0u;

    memset(f, 0, sizeof(*f));
    f->font = (uint8_t *)RoboCalloc(SEL_FACE_FONT_CAP, 1u);
    if (!f->font) {
        return 0;
    }

    ids[nc] = RoboFontNameIdFamily;
    strs[nc++] = legacyFamily;
    ids[nc] = RoboFontNameIdSubfamily;
    strs[nc++] = "Regular";
    ids[nc] = RoboFontNameIdFull;
    strs[nc++] = legacyFamily;
    ids[nc] = RoboFontNameIdPostscript;
    strs[nc++] = legacyFamily;
    if (typoOpt) {
        ids[nc] = RoboFontNameIdTypoFamily;
        strs[nc++] = typoOpt;
    }
    if (wwsOpt) {
        ids[nc] = RoboFontNameIdWwsFamily;
        strs[nc++] = wwsOpt;
    }
    nn = BuildNameTable(name, sizeof(name), ids, strs, nc);
    if (nn == 0u) {
        SelFaceRelease(f);
        return 0;
    }

    tabs[nt++] = (SynthTable){RoboFontTagHead, head, (uint32_t)BuildHead(head)};
    tabs[nt++] = (SynthTable){RoboFontTagHhea, hhea, (uint32_t)BuildHhea(hhea)};
    tabs[nt++] = (SynthTable){RoboFontTagMaxp, maxp, (uint32_t)BuildMaxp(maxp)};
    tabs[nt++] = (SynthTable){RoboFontTagHmtx, hmtx, (uint32_t)BuildHmtx(hmtx)};
    tabs[nt++] = (SynthTable){RoboFontTagLoca, loca, (uint32_t)BuildLoca(loca)};
    tabs[nt++] = (SynthTable){RoboFontTagGlyf, head, 0u};
    tabs[nt++] =
        (SynthTable){RoboFontTagPost, post, (uint32_t)BuildPost(post, postAngle)};
    tabs[nt++] =
        (SynthTable){RoboFontTagOs2, os2, (uint32_t)BuildOs2(os2, weight, width, fs)};
    tabs[nt++] = (SynthTable){RoboFontTagName, name, (uint32_t)nn};

    f->fontN = BuildSfnt(f->font, SEL_FACE_FONT_CAP, tabs, nt);
    if (f->fontN == 0u) {
        SelFaceRelease(f);
        return 0;
    }
    if (FontSourceCreateMemoryCopy(f->font, f->fontN, &f->src) != RoboOk) {
        SelFaceRelease(f);
        return 0;
    }
    if (FontFaceCreate(f->src, 0u, &f->face) != RoboOk) {
        SelFaceRelease(f);
        return 0;
    }
    return 1;
}

static void TestDistanceHelpers(void)
{
    RoboCheckEqInt(FontSelectStretchDist(100, 100), 0);
    RoboCheck(FontSelectStretchDist(75, 50) <
               FontSelectStretchDist(75, 100));
    RoboCheckEqInt(FontSelectWeightDist(400, 400), 0);
    RoboCheck(FontSelectWeightDist(300, 200) <
               FontSelectWeightDist(300, 400));
    RoboCheckEqInt(
        FontSelectStyleDist(RoboFontStyleItalic, RoboFontStyleItalic, 0, 0,
                                    0, 0),
        0);
    RoboCheck(FontSelectStyleDist(RoboFontStyleItalic, RoboFontStyleOblique,
                                           0, 0, 0, 0) <
               FontSelectStyleDist(RoboFontStyleItalic, RoboFontStyleNormal,
                                           0, 0, 0, 0));
}

static void TestFamilyAndWeightSelect(void)
{
    SelFace a, b, c;
    FontFace *cands[3];
    FontSelectRequest req;
    FontSelection sel;

    RoboCheck(SelFaceBuild(&a, "Probe", NULL, NULL, 400, 5, 0, 0));
    RoboCheck(SelFaceBuild(&b, "Probe", NULL, NULL, 700, 5, RoboFontFsBold, 0));
    RoboCheck(SelFaceBuild(&c, "Other", NULL, NULL, 400, 5, 0, 0));

    cands[0] = a.face;
    cands[1] = b.face;
    cands[2] = c.face;

    FontSelectRequestInit(&req);
    req.family = "probe"; /* casefold */
    req.weight = 700;
    req.stretch = 100;
    req.style = RoboFontStyleNormal;

    RoboCheckEqInt(FontSelect(&req, cands, 3u, &sel), RoboOk);
    RoboCheckEqInt(sel.found, 1);
    RoboCheck(sel.face == b.face);
    RoboCheckEqInt(sel.selected.weight, 700);
    FontSelectionClear(&sel);

    req.family = "Missing";
    RoboCheckEqInt(FontSelect(&req, cands, 3u, &sel), RoboOk);
    RoboCheckEqInt(sel.found, 0);
    FontSelectionClear(&sel);

    SelFaceRelease(&a);
    SelFaceRelease(&b);
    SelFaceRelease(&c);
}

static void TestTypoVsLegacyAndWws(void)
{
    SelFace typo, wws;
    FontFace *cands[2];
    FontSelectRequest req;
    FontSelection sel;

    RoboCheck(SelFaceBuild(&typo, "LegacyFam", "TypoFam", NULL, 400, 5, 0, 0));
    RoboCheck(SelFaceBuild(&wws, "LegacyFam2", NULL, "WwsFam", 400, 5,
                              RoboFontFsWws, 0));

    cands[0] = typo.face;
    cands[1] = wws.face;

    FontSelectRequestInit(&req);
    req.family = "TypoFam";
    RoboCheckEqInt(FontSelect(&req, cands, 2u, &sel), RoboOk);
    RoboCheckEqInt(sel.found, 1);
    RoboCheck(sel.face == typo.face);
    RoboCheckEqInt(sel.familyKind, RoboFontSelectFamilyTypographic);
    FontSelectionClear(&sel);

    req.family = "WwsFam";
    req.flags |= RoboFontSelectFlagPreferWws;
    RoboCheckEqInt(FontSelect(&req, cands, 2u, &sel), RoboOk);
    RoboCheckEqInt(sel.found, 1);
    RoboCheck(sel.face == wws.face);
    RoboCheckEqInt(sel.familyKind, RoboFontSelectFamilyWws);
    FontSelectionClear(&sel);

    SelFaceRelease(&typo);
    SelFaceRelease(&wws);
}

static void TestStyleItalicOblique(void)
{
    SelFace ital, obl, norm;
    FontFace *cands[3];
    FontSelectRequest req;
    FontSelection sel;

    RoboCheck(SelFaceBuild(&ital, "Slope", NULL, NULL, 400, 5, RoboFontFsItalic, 0));
    RoboCheck(SelFaceBuild(&obl, "Slope", NULL, NULL, 400, 5, RoboFontFsOblique,
                              -786432));
    RoboCheck(SelFaceBuild(&norm, "Slope", NULL, NULL, 400, 5, 0, 0));

    cands[0] = norm.face;
    cands[1] = obl.face;
    cands[2] = ital.face;

    FontSelectRequestInit(&req);
    req.family = "Slope";
    req.style = RoboFontStyleItalic;
    RoboCheckEqInt(FontSelect(&req, cands, 3u, &sel), RoboOk);
    RoboCheckEqInt(sel.found, 1);
    RoboCheck(sel.face == ital.face);
    FontSelectionClear(&sel);

    req.style = RoboFontStyleOblique;
    RoboCheckEqInt(FontSelect(&req, cands, 3u, &sel), RoboOk);
    RoboCheckEqInt(sel.found, 1);
    RoboCheck(sel.face == obl.face);
    FontSelectionClear(&sel);

    SelFaceRelease(&ital);
    SelFaceRelease(&obl);
    SelFaceRelease(&norm);
}

static void TestStretchNearest(void)
{
    SelFace condensed, normal, expanded;
    FontFace *cands[3];
    FontSelectRequest req;
    FontSelection sel;

    RoboCheck(SelFaceBuild(&condensed, "Wide", NULL, NULL, 400, 3, 0, 0));
    RoboCheck(SelFaceBuild(&normal, "Wide", NULL, NULL, 400, 5, 0, 0));
    RoboCheck(SelFaceBuild(&expanded, "Wide", NULL, NULL, 400, 7, 0, 0));

    cands[0] = expanded.face;
    cands[1] = normal.face;
    cands[2] = condensed.face;

    FontSelectRequestInit(&req);
    req.family = "Wide";
    req.stretch = RoboFontStretchCondensed; /* 75 */
    RoboCheckEqInt(FontSelect(&req, cands, 3u, &sel), RoboOk);
    RoboCheckEqInt(sel.found, 1);
    RoboCheck(sel.face == condensed.face);
    FontSelectionClear(&sel);

    SelFaceRelease(&condensed);
    SelFaceRelease(&normal);
    SelFaceRelease(&expanded);
}

static void TestDeterminismOrder(void)
{
    SelFace a, b;
    FontFace *order1[2];
    FontFace *order2[2];
    FontSelectRequest req;
    FontSelection s1, s2;

    RoboCheck(SelFaceBuild(&a, "Same", NULL, NULL, 500, 5, 0, 0));
    RoboCheck(SelFaceBuild(&b, "Same", NULL, NULL, 500, 5, 0, 0));

    order1[0] = a.face;
    order1[1] = b.face;
    order2[0] = b.face;
    order2[1] = a.face;

    FontSelectRequestInit(&req);
    req.family = "Same";
    req.weight = 500;

    RoboCheckEqInt(FontSelect(&req, order1, 2u, &s1), RoboOk);
    RoboCheckEqInt(FontSelect(&req, order2, 2u, &s2), RoboOk);
    RoboCheckEqInt(s1.found, 1);
    RoboCheckEqInt(s2.found, 1);
    RoboCheckEqInt((int)(s1.faceId == s2.faceId), 1);
    /* Lower faceId wins on perfect tie. */
    RoboCheck(s1.faceId == GetFontFaceId(a.face) ||
               s1.faceId == GetFontFaceId(b.face));
    if (GetFontFaceId(a.face) < GetFontFaceId(b.face)) {
        RoboCheck(s1.face == a.face);
    } else {
        RoboCheck(s1.face == b.face);
    }

    FontSelectionClear(&s1);
    FontSelectionClear(&s2);
    SelFaceRelease(&a);
    SelFaceRelease(&b);
}

static void TestAliasAndEmpty(void)
{
    SelFace f;
    FontFace *cands[1];
    FontSelectRequest req;
    FontSelection sel;
    const char *aliases[] = {"AliasName"};

    RoboCheck(SelFaceBuild(&f, "AliasName", NULL, NULL, 400, 5, 0, 0));
    cands[0] = f.face;

    FontSelectRequestInit(&req);
    req.family = "PrimaryMissing";
    req.aliases = aliases;
    req.aliasCount = 1u;
    RoboCheckEqInt(FontSelect(&req, cands, 1u, &sel), RoboOk);
    RoboCheckEqInt(sel.found, 1);
    FontSelectionClear(&sel);

    RoboCheckEqInt(FontSelect(&req, NULL, 0u, &sel), RoboOk);
    RoboCheckEqInt(sel.found, 0);
    FontSelectionClear(&sel);

    req.family = NULL;
    RoboCheckEqInt(FontSelectRequestValidate(&req), RoboInvalidArgument);

    SelFaceRelease(&f);
}

static void TestFuzzSelection(void)
{
    uint8_t junk[128];
    size_t i;
    for (i = 0u; i < sizeof(junk); i++) {
        junk[i] = (uint8_t)(i * 41u + 7u);
    }
    RoboCheckEqInt(FontSelectionFuzzParse(junk, sizeof(junk)), RoboOk);
    RoboCheckEqInt(FontSelectionFuzzParse(junk, 0u), RoboOk);
}

void TestRegisterFontSelection(void)
{
    TestRegister("font.select.distances", TestDistanceHelpers);
    TestRegister("font.select.family_weight", TestFamilyAndWeightSelect);
    TestRegister("font.select.typo_wws", TestTypoVsLegacyAndWws);
    TestRegister("font.select.style", TestStyleItalicOblique);
    TestRegister("font.select.stretch", TestStretchNearest);
    TestRegister("font.select.determinism", TestDeterminismOrder);
    TestRegister("font.select.alias_empty", TestAliasAndEmpty);
    TestRegister("font.select.fuzz", TestFuzzSelection);
}
