/*
 * RoboFont foundation suite (Goal 5.1).
 */

#include "RoboFont/RoboFont.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

#include <string.h>

/* Minimal SFNT-like magic (TrueType scaler type) — not a valid font. */
static const uint8_t kSfntMagic[12] = {
    0x00, 0x01, 0x00, 0x00, 'h', 'e', 'a', 'd', 0, 0, 0, 0
};

static void TestFontVersion(void)
{
    RoboCheckStrEq(FontVersionString(), "0.22.0");
    RoboCheckEqInt(RoboFontApiFrozen, 1);
    RoboCheckEqInt(FontInit(), RoboOk);
    FontShutdown();
}

static void TestFontStyleValidate(void)
{
    FontStyleDesc d;
    FontStyleDescInit(&d);
    RoboCheckEqInt(FontStyleDescValidate(&d), RoboOk);
    RoboCheck(FontWeightIsValid(RoboFontWeightBold));
    RoboCheck(!FontWeightIsValid(0));
    RoboCheck(!FontWeightIsValid(1001));
    RoboCheck(FontStretchIsValid(RoboFontStretchNormal));
    RoboCheck(!FontStretchIsValid(10));
    d.axisCount = 1;
    d.axes[0].tag = 0;
    d.axes[0].value = 400.0f;
    RoboCheckEqInt(FontStyleDescValidate(&d), RoboInvalidArgument);
    d.axes[0].tag = RoboFontTagWght;
    d.axes[0].value = 600.0f;
    RoboCheckEqInt(FontStyleDescValidate(&d), RoboOk);
    RoboCheckEqInt(FontStyleDescValidate(NULL), RoboInvalidArgument);
}

static void TestFontMetrics(void)
{
    FontMetrics m;
    FontBbox bb;
    float px = 0.0f;
    FontUnit u = 0;

    RoboCheckEqInt(FontMetricsInit(NULL, 1000, 800, 200, 0, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(FontMetricsInit(&m, 0, 800, 200, 0, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(FontMetricsInit(&m, 1000, 800, -1, 0, NULL),
                      RoboInvalidArgument);

    bb.xMin = -100;
    bb.yMin = -200;
    bb.xMax = 1000;
    bb.yMax = 800;
    RoboCheckEqInt(FontMetricsInit(&m, 1000, 800, 200, 90, &bb), RoboOk);
    RoboCheckEqInt(m.valid, 1);
    RoboCheckEqInt(m.hasBbox, 1);
    RoboCheckEqInt(FontMetricsLineHeight(&m), 1090);

    RoboCheckEqInt(FontUnitsToPixels(500, 1000, 16.0f, &px), RoboOk);
    RoboCheck(px > 7.9f && px < 8.1f);
    RoboCheckEqInt(FontPixelsToUnits(8.0f, 1000, 16.0f, &u), RoboOk);
    RoboCheckEqInt(u, 500);
    RoboCheck(FontPointsToPixels(12.0f) > 15.0f);
}

static void TestFontSourceCopy(void)
{
    FontSource *src = NULL;
    uint8_t buf[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    RoboCheckEqInt(FontSourceCreateMemoryCopy(NULL, 8, &src),
                      RoboInvalidArgument);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(buf, 0, &src),
                      RoboInvalidArgument);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(buf, sizeof(buf), &src), RoboOk);
    RoboCheck(src != NULL);
    RoboCheckEqInt((int)MakeFontSourceKind(src), (int)RoboFontSourceMemoryCopy);
    RoboCheckEqInt((int)FontSourceSize(src), 8);
    RoboCheck(memcmp(FontSourceBytes(src), buf, 8) == 0);

    /* Mutating original must not affect copy. */
    buf[0] = 99;
    RoboCheckEqInt(FontSourceBytes(src)[0], 1);

    FontSourceRetain(src);
    FontSourceRelease(src);
    FontSourceRelease(src);
    FontSourceRelease(NULL);
}

static void TestFontSourceBorrow(void)
{
    FontSource *src = NULL;
    uint8_t buf[4] = {0x00, 0x01, 0x00, 0x00};

    RoboCheckEqInt(FontSourceCreateMemoryBorrow(buf, sizeof(buf), &src), RoboOk);
    RoboCheckEqInt((int)MakeFontSourceKind(src), (int)RoboFontSourceMemoryBorrow);
    RoboCheck(FontSourceBytes(src) == buf);
    FontSourceRelease(src);
}

static void TestFontFaceInstance(void)
{
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontInstance *inst = NULL;
    FontInstance *inst2 = NULL;
    FontStyleDesc style;
    FontMetrics metrics;
    FontMetrics got;
    float px = 0.0f;
    uint32_t glyphs = 99u;
    FontFaceId fid;
    FontInstanceId iid;

    RoboCheckEqInt(
        FontSourceCreateMemoryCopy(kSfntMagic, sizeof(kSfntMagic), &src),
        RoboOk);
    RoboCheckEqInt(FontFaceCreate(NULL, 0, &face), RoboInvalidArgument);
    RoboCheckEqInt(FontFaceCreate(src, 0, &face), RoboOk);
    RoboCheckEqInt((int)FontFaceFormat(face), (int)RoboFontFormatSfnt);
    RoboCheckEqInt((int)FontFaceIndex(face), 0);
    fid = GetFontFaceId(face);
    RoboCheck(fid != 0u);
    RoboCheck(FontFaceSource(face) == src);

    /* Source still alive after releasing our local retain (face holds one). */
    FontSourceRelease(src);
    RoboCheck(FontSourceSize(FontFaceSource(face)) == sizeof(kSfntMagic));

    RoboCheckEqInt(FontFaceStyle(face, &style), RoboOk);
    RoboCheckEqInt(style.weight, RoboFontWeightNormal);
    RoboCheckEqInt(FontFaceGlyphCount(face, &glyphs), RoboNotSupported);
    RoboCheckEqInt((int)glyphs, 0);

    FontStyleDescInit(&style);
    style.weight = RoboFontWeightBold;
    RoboCheckEqInt(FontInstanceCreate(face, 0.0f, &style, &inst),
                      RoboInvalidArgument);
    RoboCheckEqInt(FontInstanceCreate(face, 16.0f, &style, &inst), RoboOk);
    iid = GetFontInstanceId(inst);
    RoboCheck(iid != 0u);
    RoboCheck(FontInstanceFace(inst) == face);
    RoboCheck(FontInstancePixelSize(inst) == 16.0f);
    RoboCheckEqInt(FontInstanceStyle(inst, &style), RoboOk);
    RoboCheckEqInt(style.weight, RoboFontWeightBold);

    RoboCheckEqInt(FontInstanceMetrics(inst, &got), RoboNotFound);
    RoboCheckEqInt(FontMetricsInit(&metrics, 1000, 800, 200, 0, NULL), RoboOk);
    RoboCheckEqInt(FontInstanceSetMetrics(inst, &metrics), RoboOk);
    RoboCheckEqInt(FontInstanceMetrics(inst, &got), RoboOk);
    RoboCheckEqInt(got.unitsPerEm, 1000);
    RoboCheckEqInt(FontInstanceUnitsToPixels(inst, 1000, &px), RoboOk);
    RoboCheck(px > 15.9f && px < 16.1f);

    /* Face/instance separation: second instance, different size. */
    RoboCheckEqInt(FontInstanceCreate(face, 24.0f, NULL, &inst2), RoboOk);
    RoboCheck(GetFontInstanceId(inst2) != iid);
    RoboCheck(FontInstancePixelSize(inst2) == 24.0f);

    /* Axis settings on a non-variable face: validate OK; coords skipped (NOT_FOUND). */
    FontStyleDescInit(&style);
    style.axisCount = 1;
    style.axes[0].tag = RoboFontTagWght;
    style.axes[0].value = 600.0f;
    {
        FontInstance *withAxis = NULL;
        RoboCheckEqInt(FontInstanceCreate(face, 12.0f, &style, &withAxis),
                          RoboOk);
        RoboCheck(withAxis != NULL);
        RoboCheck(FontInstanceVariationCoords(withAxis) == NULL);
        RoboCheckEqInt((int)FontInstanceMetricsVariationState(withAxis),
                          (int)RoboFontMetricsVarStatic);
        FontInstanceRelease(withAxis);
    }

    FontInstanceRelease(inst2);
    FontInstanceRelease(inst);
    FontFaceRetain(face);
    FontFaceRelease(face);
    FontFaceRelease(face);
}

static void TestFontFormatDetect(void)
{
    static const uint8_t woff[4] = {'w', 'O', 'F', 'F'};
    static const uint8_t junk[4] = {0xFF, 0xFE, 0xFD, 0xFC};
    FontSource *src = NULL;
    FontFace *face = NULL;

    RoboCheckEqInt(FontSourceCreateMemoryBorrow(woff, 4, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0, &face), RoboOk);
    RoboCheckEqInt((int)FontFaceFormat(face), (int)RoboFontFormatWoff);
    FontFaceRelease(face);
    FontSourceRelease(src);

    RoboCheckEqInt(FontSourceCreateMemoryBorrow(junk, 4, &src), RoboOk);
    RoboCheckEqInt(FontFaceCreate(src, 0, &face), RoboOk);
    RoboCheckEqInt((int)FontFaceFormat(face), (int)RoboFontFormatUnknown);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestFontGlyphId(void)
{
    GlyphId g = RoboGlyphIdNone;
    RoboCheckEqInt((int)g, 0);
    g = 42u;
    RoboCheck(g != RoboGlyphIdNone);
}

static void TestFontLifetimeNullSafe(void)
{
    FontFaceRetain(NULL);
    FontFaceRelease(NULL);
    FontInstanceRetain(NULL);
    FontInstanceRelease(NULL);
    RoboCheck(GetFontFaceId(NULL) == 0u);
    RoboCheck(FontInstanceFace(NULL) == NULL);
}

void TestRegisterFont(void)
{
    TestRegister("font.version", TestFontVersion);
    TestRegister("font.style", TestFontStyleValidate);
    TestRegister("font.metrics", TestFontMetrics);
    TestRegister("font.source_copy", TestFontSourceCopy);
    TestRegister("font.source_borrow", TestFontSourceBorrow);
    TestRegister("font.face_instance", TestFontFaceInstance);
    TestRegister("font.format_detect", TestFontFormatDetect);
    TestRegister("font.glyph_id", TestFontGlyphId);
    TestRegister("font.lifetime_null", TestFontLifetimeNullSafe);
}
