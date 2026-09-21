/*
 * RoboFont variable-font suite (Goal 5.18).
 * Synthetic SFNT fixtures — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

#include <math.h>
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

#define VAR_AXIS_NAME_ID 256u
#define VAR_BOLD_NAME_ID 257u

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
static void PutI32(uint8_t **p, int32_t v) { PutU32(p, (uint32_t)v); }
static void PutI64(uint8_t **p, int64_t v)
{
    PutU32(p, (uint32_t)((uint64_t)v >> 32));
    PutU32(p, (uint32_t)((uint64_t)v));
}

static int32_t FloatToFixed(float v)
{
    return (int32_t)((double)v * 65536.0);
}

static int16_t FloatToF2dot14(float v)
{
    return (int16_t)lround((double)v * 16384.0);
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

static size_t BuildNameTable(uint8_t *buf, size_t cap, const uint16_t *ids,
                               const char *const *strs, uint32_t count)
{
    uint8_t *p = buf;
    uint32_t i;
    size_t header = 6u + (size_t)count * 12u;
    size_t storage = header;

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

static size_t BuildVarName(uint8_t *buf, size_t cap, const char *family)
{
    uint16_t ids[6];
    const char *strs[6];
    ids[0] = RoboFontNameIdFamily;
    strs[0] = family;
    ids[1] = RoboFontNameIdSubfamily;
    strs[1] = "Regular";
    ids[2] = RoboFontNameIdFull;
    strs[2] = family;
    ids[3] = RoboFontNameIdPostscript;
    strs[3] = "VarSans-Regular";
    ids[4] = (uint16_t)VAR_AXIS_NAME_ID;
    strs[4] = "Weight";
    ids[5] = (uint16_t)VAR_BOLD_NAME_ID;
    strs[5] = "Bold";
    return BuildNameTable(buf, cap, ids, strs, 6u);
}

/* fvar: one wght axis 100..400..900; Regular@400 + Bold@700. */
static size_t BuildFvarWght(uint8_t *buf, int duplicateAxis)
{
    uint8_t *p = buf;
    uint16_t axisCount = duplicateAxis ? 2u : 1u;
    uint16_t axisSize = 20u;
    uint16_t instanceCount = 2u;
    uint16_t instanceSize = (uint16_t)(4u + 4u * axisCount);
    uint16_t a;

    PutU16(&p, 1);
    PutU16(&p, 0);
    PutU16(&p, 16);
    PutU16(&p, 0);
    PutU16(&p, axisCount);
    PutU16(&p, axisSize);
    PutU16(&p, instanceCount);
    PutU16(&p, instanceSize);
    for (a = 0u; a < axisCount; a++) {
        PutU32(&p, RoboFontTagWght);
        PutI32(&p, FloatToFixed(100.0f));
        PutI32(&p, FloatToFixed(400.0f));
        PutI32(&p, FloatToFixed(900.0f));
        PutU16(&p, 0);
        PutU16(&p, (uint16_t)VAR_AXIS_NAME_ID);
    }
    /* Regular */
    PutU16(&p, RoboFontNameIdSubfamily);
    PutU16(&p, 0);
    for (a = 0u; a < axisCount; a++) {
        PutI32(&p, FloatToFixed(400.0f));
    }
    /* Bold */
    PutU16(&p, (uint16_t)VAR_BOLD_NAME_ID);
    PutU16(&p, 0);
    for (a = 0u; a < axisCount; a++) {
        PutI32(&p, FloatToFixed(700.0f));
    }
    return (size_t)(p - buf);
}

/* Truncated fvar: claims huge axisCount but little payload. */
static size_t BuildFvarHugeTruncated(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutU16(&p, 16);
    PutU16(&p, 0);
    PutU16(&p, 500);
    PutU16(&p, 20);
    PutU16(&p, 0);
    PutU16(&p, 8);
    PutU32(&p, RoboFontTagWght);
    PutI32(&p, FloatToFixed(100.0f));
    PutI32(&p, FloatToFixed(400.0f));
    PutI32(&p, FloatToFixed(900.0f));
    PutU16(&p, 0);
    PutU16(&p, (uint16_t)VAR_AXIS_NAME_ID);
    return (size_t)(p - buf);
}

/* avar: (-1,-1),(0,0),(0.5,0.75),(1,1) */
static size_t BuildAvarNonlinear(uint8_t *buf)
{
    uint8_t *p = buf;
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 1);
    PutU16(&p, 4);
    PutI16(&p, FloatToF2dot14(-1.0f));
    PutI16(&p, FloatToF2dot14(-1.0f));
    PutI16(&p, FloatToF2dot14(0.0f));
    PutI16(&p, FloatToF2dot14(0.0f));
    PutI16(&p, FloatToF2dot14(0.5f));
    PutI16(&p, FloatToF2dot14(0.75f));
    PutI16(&p, FloatToF2dot14(1.0f));
    PutI16(&p, FloatToF2dot14(1.0f));
    return (size_t)(p - buf);
}

enum {
    VAR_FONT_FVAR = 1,
    VAR_FONT_AVAR = 2,
    VAR_FONT_DUP_AXIS = 4,
    VAR_FONT_HUGE_FVAR = 8,
    VAR_FONT_NO_FVAR = 16
};

static size_t BuildVarFont(uint8_t *out, size_t cap, const char *family,
                             uint16_t os2Weight, int flags)
{
    uint8_t head[64], hhea[40], maxp[40], hmtx[16], loca[16], post[40], os2[96];
    uint8_t name[512], fvar[128], avar[64];
    SynthTable tabs[16];
    uint32_t nt = 0u;
    size_t nn;
    size_t fn = 0u;
    size_t an = 0u;

    nn = BuildVarName(name, sizeof(name), family);
    if (nn == 0u) {
        return 0u;
    }
    if (!(flags & VAR_FONT_NO_FVAR)) {
        if (flags & VAR_FONT_HUGE_FVAR) {
            fn = BuildFvarHugeTruncated(fvar);
        } else {
            fn = BuildFvarWght(fvar, (flags & VAR_FONT_DUP_AXIS) ? 1 : 0);
        }
    }
    if (flags & VAR_FONT_AVAR) {
        an = BuildAvarNonlinear(avar);
    }

    tabs[nt++] = (SynthTable){RoboFontTagHead, head, (uint32_t)BuildHead(head)};
    tabs[nt++] = (SynthTable){RoboFontTagHhea, hhea, (uint32_t)BuildHhea(hhea)};
    tabs[nt++] = (SynthTable){RoboFontTagMaxp, maxp, (uint32_t)BuildMaxp(maxp)};
    tabs[nt++] = (SynthTable){RoboFontTagHmtx, hmtx, (uint32_t)BuildHmtx(hmtx)};
    tabs[nt++] = (SynthTable){RoboFontTagLoca, loca, (uint32_t)BuildLoca(loca)};
    tabs[nt++] = (SynthTable){RoboFontTagGlyf, head, 0u};
    tabs[nt++] = (SynthTable){RoboFontTagPost, post, (uint32_t)BuildPost(post)};
    tabs[nt++] =
        (SynthTable){RoboFontTagOs2, os2, (uint32_t)BuildOs2(os2, os2Weight)};
    tabs[nt++] = (SynthTable){RoboFontTagName, name, (uint32_t)nn};
    if (fn) {
        tabs[nt++] = (SynthTable){RoboFontTagFvar, fvar, (uint32_t)fn};
    }
    if (an) {
        tabs[nt++] = (SynthTable){RoboFontTagAvar, avar, (uint32_t)an};
    }
    return BuildSfnt(out, cap, tabs, nt);
}

static int NearF(float a, float b, float eps)
{
    float d = a - b;
    if (d < 0.0f) {
        d = -d;
    }
    return d <= eps;
}

static void TestNormalize(void)
{
    float out = 99.0f;
    float nanv;

    RoboCheckEqInt(
        FontVariationNormalize(100.0f, 400.0f, 900.0f, 100.0f, &out), RoboOk);
    RoboCheck(NearF(out, -1.0f, 0.001f));
    RoboCheckEqInt(
        FontVariationNormalize(100.0f, 400.0f, 900.0f, 400.0f, &out), RoboOk);
    RoboCheck(NearF(out, 0.0f, 0.001f));
    RoboCheckEqInt(
        FontVariationNormalize(100.0f, 400.0f, 900.0f, 900.0f, &out), RoboOk);
    RoboCheck(NearF(out, 1.0f, 0.001f));
    /* Midpoints below / above default. */
    RoboCheckEqInt(
        FontVariationNormalize(100.0f, 400.0f, 900.0f, 250.0f, &out), RoboOk);
    RoboCheck(NearF(out, -0.5f, 0.001f));
    RoboCheckEqInt(
        FontVariationNormalize(100.0f, 400.0f, 900.0f, 650.0f, &out), RoboOk);
    RoboCheck(NearF(out, 0.5f, 0.001f));

    RoboCheckEqInt(
        FontVariationClampUser(100.0f, 400.0f, 900.0f, 50.0f, &out), RoboOk);
    RoboCheck(NearF(out, 100.0f, 0.001f));
    RoboCheckEqInt(
        FontVariationClampUser(100.0f, 400.0f, 900.0f, 950.0f, &out), RoboOk);
    RoboCheck(NearF(out, 900.0f, 0.001f));

    nanv = nanf("");
    RoboCheckEqInt(
        FontVariationClampUser(100.0f, 400.0f, 900.0f, nanv, &out),
        RoboInvalidArgument);
    RoboCheckEqInt(
        FontVariationNormalize(100.0f, 400.0f, 900.0f, nanv, &out),
        RoboInvalidArgument);
}

static void TestFvarParse(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    const FontVariationModel *m;
    const FontVariationAxis *ax;
    const FontVariationInstance *inst;
    FontVariationModel *dupModel = NULL;
    uint8_t dupFont[4096];
    size_t dn;
    FontSource *dsrc = NULL;
    FontSfnt *dsfnt = NULL;

    n = BuildVarFont(font, sizeof(font), "VarSans", 400, VAR_FONT_FVAR);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0u, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseVariation(face), RoboOk);
    m = FontFaceVariation(face);
    RoboCheck(m != NULL);
    RoboCheck(FontVariationModelIsVariable(m));
    RoboCheckEqInt((int)FontVariationModelAxisCount(m), 1);
    ax = FontVariationModelAxis(m, 0u);
    RoboCheck(ax != NULL);
    RoboCheckEqInt((int)ax->tag, (int)RoboFontTagWght);
    RoboCheck(NearF(ax->minValue, 100.0f, 0.01f));
    RoboCheck(NearF(ax->defaultValue, 400.0f, 0.01f));
    RoboCheck(NearF(ax->maxValue, 900.0f, 0.01f));
    RoboCheck(ax->nameLen > 0u);
    RoboCheck(strcmp(ax->nameUtf8, "Weight") == 0);
    RoboCheckEqInt((int)FontVariationModelInstanceCount(m), 2);
    inst = FontVariationModelInstance(m, 0u);
    RoboCheck(inst != NULL);
    RoboCheck(inst->nameLen > 0u);
    RoboCheck(strcmp(inst->nameUtf8, "Regular") == 0);
    RoboCheck(NearF(inst->userCoords[0], 400.0f, 0.01f));
    inst = FontVariationModelInstance(m, 1u);
    RoboCheck(inst != NULL);
    RoboCheck(strcmp(inst->nameUtf8, "Bold") == 0);
    RoboCheck(NearF(inst->userCoords[0], 700.0f, 0.01f));

    /* Duplicate axis tags rejected. */
    dn = BuildVarFont(dupFont, sizeof(dupFont), "DupAxis", 400,
                        VAR_FONT_FVAR | VAR_FONT_DUP_AXIS);
    RoboCheck(dn > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(dupFont, dn, &dsrc), RoboOk);
    RoboCheckEqInt(FontSfntParse(dsrc, 0u, 0u, &dsfnt), RoboOk);
    RoboCheckEqInt(FontVariationModelCreate(dsfnt, NULL, NULL, &dupModel),
                      RoboParseError);
    RoboCheck(dupModel == NULL);
    FontSfntRelease(dsfnt);
    FontSourceRelease(dsrc);

    /* Missing fvar → NOT_FOUND. */
    {
        uint8_t staticFont[4096];
        size_t sn;
        FontSource *ssrc = NULL;
        FontFace *sface = NULL;
        sn = BuildVarFont(staticFont, sizeof(staticFont), "Static", 400,
                            VAR_FONT_NO_FVAR);
        RoboCheck(sn > 0u);
        RoboCheckEqInt(FontSourceCreateMemoryCopy(staticFont, sn, &ssrc),
                          RoboOk);
        RoboCheckEqInt(FontFaceCreate(ssrc, 0u, &sface), RoboOk);
        RoboCheckEqInt(FontFaceParseVariation(sface), RoboNotFound);
        RoboCheck(FontFaceVariation(sface) == NULL);
        FontFaceRelease(sface);
        FontSourceRelease(ssrc);
    }

    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestAvar(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    const FontVariationModel *m;
    FontVariationRequest req;
    FontVariationCoords *coords = NULL;
    float norm;
    float adj;

    n = BuildVarFont(font, sizeof(font), "AvarSans", 400, VAR_FONT_FVAR | VAR_FONT_AVAR);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0u, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseVariation(face), RoboOk);
    m = FontFaceVariation(face);
    RoboCheck(m != NULL);
    RoboCheck(FontVariationModelHasAvar(m));

    /* User mid-high 650 → normalized 0.5 → avar maps to 0.75. */
    RoboCheckEqInt(
        FontVariationNormalize(100.0f, 400.0f, 900.0f, 650.0f, &norm), RoboOk);
    RoboCheck(NearF(norm, 0.5f, 0.001f));
    RoboCheckEqInt(FontVariationAvarMap(m, 0u, norm, &adj), RoboOk);
    RoboCheck(NearF(adj, 0.75f, 0.02f));
    RoboCheck(!NearF(adj, norm, 0.01f));

    FontVariationRequestInit(&req);
    req.applyStyleAxes = 1u;
    req.weight = 650;
    RoboCheckEqInt(FontVariationCoordsCreate(m, &req, &coords), RoboOk);
    RoboCheck(NearF(FontVariationCoordsNormalized(coords, 0u), 0.5f, 0.02f));
    RoboCheck(NearF(FontVariationCoordsAdjusted(coords, 0u), 0.75f, 0.02f));
    FontVariationCoordsRelease(coords);

    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestCoordsInstance(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontVariationModel *model = NULL;
    FontVariationRequest req;
    FontVariationCoords *coords = NULL;
    FontInstance *a = NULL;
    FontInstance *b = NULL;
    FontStyleDesc style;
    const FontVariationCoords *ca;
    const FontVariationCoords *cb;
    float user = 0.0f;

    n = BuildVarFont(font, sizeof(font), "CoordSans", 400, VAR_FONT_FVAR);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0u, &face), RoboOk);
    RoboCheckEqInt(FontVariationModelCreateFromFace(face, &model), RoboOk);
    RoboCheck(model != NULL);

    FontVariationRequestInit(&req);
    req.applyStyleAxes = 1u;
    req.weight = 600;
    RoboCheckEqInt(FontVariationCoordsCreate(model, &req, &coords), RoboOk);
    RoboCheck(NearF(FontVariationCoordsUser(coords, 0u), 600.0f, 0.01f));
    /* (600-400)/(900-400) = 0.4 */
    RoboCheck(NearF(FontVariationCoordsNormalized(coords, 0u), 0.4f, 0.02f));
    RoboCheckEqInt(
        FontVariationCoordsUserForTag(coords, RoboFontTagWght, &user),
        RoboOk);
    RoboCheck(NearF(user, 600.0f, 0.01f));
    FontVariationCoordsRelease(coords);
    FontVariationModelRelease(model);

    FontStyleDescInit(&style);
    style.weight = 300;
    RoboCheckEqInt(FontInstanceCreate(face, 16.0f, &style, &a), RoboOk);
    style.weight = 800;
    RoboCheckEqInt(FontInstanceCreate(face, 16.0f, &style, &b), RoboOk);
    ca = FontInstanceVariationCoords(a);
    cb = FontInstanceVariationCoords(b);
    RoboCheck(ca != NULL && cb != NULL);
    RoboCheck(ca != cb);
    RoboCheck(NearF(FontVariationCoordsUser(ca, 0u), 300.0f, 0.01f));
    RoboCheck(NearF(FontVariationCoordsUser(cb, 0u), 800.0f, 0.01f));
    RoboCheckEqInt((int)FontInstanceMetricsVariationState(a),
                      (int)RoboFontMetricsVarPending);
    RoboCheckEqInt((int)FontInstanceMetricsVariationState(b),
                      (int)RoboFontMetricsVarPending);

    FontInstanceRelease(a);
    FontInstanceRelease(b);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestSelect(void)
{
    uint8_t staticBuf[4096];
    uint8_t varBuf[4096];
    size_t sn;
    size_t vn;
    FontSource *ssrc = NULL;
    FontSource *vsrc = NULL;
    FontFace *sface = NULL;
    FontFace *vface = NULL;
    FontFace *cands[2];
    FontSelectRequest req;
    FontSelection sel;
    FontMatchScore score;

    sn = BuildVarFont(staticBuf, sizeof(staticBuf), "SelectFam", 400, VAR_FONT_NO_FVAR);
    vn = BuildVarFont(varBuf, sizeof(varBuf), "SelectFam", 400, VAR_FONT_FVAR);
    RoboCheck(sn > 0u && vn > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(staticBuf, sn, &ssrc), RoboOk);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(varBuf, vn, &vsrc), RoboOk);
    RoboCheckEqInt(FontFaceCreate(ssrc, 0u, &sface), RoboOk);
    RoboCheckEqInt(FontFaceCreate(vsrc, 0u, &vface), RoboOk);

    cands[0] = sface;
    cands[1] = vface;
    FontSelectRequestInit(&req);
    req.family = "SelectFam";
    req.weight = 700;
    req.stretch = 100;
    req.style = RoboFontStyleNormal;

    RoboCheckEqInt(FontSelectScoreFace(&req, vface, &score), RoboOk);
    RoboCheckEqInt((int)score.weightDist, 0);

    RoboCheckEqInt(FontSelect(&req, cands, 2u, &sel), RoboOk);
    RoboCheckEqInt(sel.found, 1);
    RoboCheck(sel.face == vface);
    RoboCheckEqInt((int)sel.score.weightDist, 0);
    RoboCheckEqInt(sel.selected.weight, 700);
    FontSelectionClear(&sel);

    FontFaceRelease(sface);
    FontFaceRelease(vface);
    FontSourceRelease(ssrc);
    FontSourceRelease(vsrc);
}

static void TestFuzz(void)
{
    uint8_t junk[] = {0x00, 0x01, 0x00, 0x00, 0xFF, 0xFE, 0xFD, 0xFC};
    uint8_t empty[1] = {0};
    uint8_t font[4096];
    size_t n;
    uint8_t huge[4096];
    size_t hn;

    RoboCheckEqInt(FontVariationFuzzParse(NULL, 0u), RoboOk);
    (void)FontVariationFuzzParse(junk, sizeof(junk));
    (void)FontVariationFuzzParse(empty, 0u);

    n = BuildVarFont(font, sizeof(font), "FuzzOk", 400, VAR_FONT_FVAR | VAR_FONT_AVAR);
    RoboCheck(n > 0u);
    (void)FontVariationFuzzParse(font, n);

    hn = BuildVarFont(huge, sizeof(huge), "FuzzHuge", 400,
                        VAR_FONT_FVAR | VAR_FONT_HUGE_FVAR);
    RoboCheck(hn > 0u);
    (void)FontVariationFuzzParse(huge, hn);
    {
        FontSource *src = NULL;
        FontSfnt *sfnt = NULL;
        FontVariationModel *model = NULL;
        RoboCheckEqInt(FontSourceCreateMemoryCopy(huge, hn, &src), RoboOk);
        RoboCheckEqInt(FontSfntParse(src, 0u, 0u, &sfnt), RoboOk);
        RoboCheck(FontVariationModelCreate(sfnt, NULL, NULL, &model) != RoboOk);
        RoboCheck(model == NULL);
        FontSfntRelease(sfnt);
        FontSourceRelease(src);
    }
}

static void TestBench(void)
{
    uint8_t font[4096];
    size_t n;
    FontSource *src = NULL;
    FontFace *face = NULL;
    const FontVariationModel *m;
    int hits = 0;
    int i;

    n = BuildVarFont(font, sizeof(font), "BenchSans", 400, VAR_FONT_FVAR);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0u, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseVariation(face), RoboOk);
    m = FontFaceVariation(face);
    RoboCheck(m != NULL);

    for (i = 0; i < 200; i++) {
        float norm = 0.0f;
        float w = 100.0f + (float)(i % 801);
        FontVariationRequest req;
        FontVariationCoords *coords = NULL;
        if (FontVariationNormalize(100.0f, 400.0f, 900.0f, w, &norm) == RoboOk) {
            hits++;
        }
        FontVariationRequestInit(&req);
        req.applyStyleAxes = 1u;
        req.weight = (FontWeight)(100 + (i % 900));
        if (FontVariationCoordsCreate(m, &req, &coords) == RoboOk && coords) {
            hits++;
            (void)FontVariationCoordsAdjusted(coords, 0u);
            FontVariationCoordsRelease(coords);
        }
    }
    printf("font.variation.bench hits=%d\n", hits);
    RoboCheck(hits >= 200);

    FontFaceRelease(face);
    FontSourceRelease(src);
}

void TestRegisterFontVariation(void)
{
    TestRegister("font.variation.normalize", TestNormalize);
    TestRegister("font.variation.fvar_parse", TestFvarParse);
    TestRegister("font.variation.avar", TestAvar);
    TestRegister("font.variation.coords_instance", TestCoordsInstance);
    TestRegister("font.variation.select", TestSelect);
    TestRegister("font.variation.fuzz", TestFuzz);
    TestRegister("font.variation.bench", TestBench);
}
