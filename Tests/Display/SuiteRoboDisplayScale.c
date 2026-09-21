/*
 * RoboDisplay / RoboWindow DPI & scale suite (Goal 7.8).
 * Headless mock displays — configurable scale/DPI/bounds.
 */

#include "RoboDisplay/RoboDisplay.h"
#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"

#include <string.h>

#define MOCK_MAX 8

typedef struct scaleMock {
    DisplayInfo displays[MOCK_MAX];
    size_t count;
} ScaleMock;

static ScaleMock gSm;
static DisplayBackendCaps gSmCaps;
static DisplayBackendVtbl gSmVtbl;

static void FillDisp(DisplayInfo *d,
                      DisplayId id,
                      int32_t x,
                      int32_t y,
                      int32_t w,
                      int32_t h,
                      int32_t resW,
                      int32_t resH,
                      float scale,
                      float dpiX,
                      float dpiY,
                      int primary)
{
    DisplayInfoInit(d);
    d->id = id;
    d->bounds = MakeGfxRect(x, y, w, h);
    d->workArea = MakeGfxRect(x, y, w, h - 40);
    d->resolutionWidthPx = resW;
    d->resolutionHeightPx = resH;
    d->refreshHz = 60.0f;
    d->scale = scale;
    d->dpiX = dpiX;
    d->dpiY = dpiY;
    d->orientation = RoboDisplayOrientationLandscape;
    d->isPrimary = primary;
    d->connection = RoboDisplayConnectionConnected;
}

static Result SmInit(void *ud)
{
    (void)ud;
    return RoboOk;
}

static void SmShutdown(void *ud)
{
    (void)ud;
}

static Result SmEnumerate(void *ud,
                                  DisplayInfo *out,
                                  size_t capacity,
                                  size_t *outCount)
{
    ScaleMock *m = (ScaleMock *)ud;
    size_t n;
    size_t i;

    if (!m || !outCount) {
        return RoboInvalidArgument;
    }
    n = m->count;
    *outCount = n;
    if (!out || capacity == 0u) {
        return RoboOk;
    }
    if (n > capacity) {
        n = capacity;
    }
    for (i = 0u; i < n; i++) {
        out[i] = m->displays[i];
    }
    *outCount = n;
    return RoboOk;
}

static Result SmPrimary(void *ud, DisplayInfo *out)
{
    ScaleMock *m = (ScaleMock *)ud;
    size_t i;

    if (!m || !out) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < m->count; i++) {
        if (m->displays[i].isPrimary) {
            *out = m->displays[i];
            return RoboOk;
        }
    }
    return RoboNotFound;
}

static Result SmGetInfo(void *ud,
                                 DisplayId id,
                                 DisplayInfo *out)
{
    ScaleMock *m = (ScaleMock *)ud;
    size_t i;

    if (!m || !out) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < m->count; i++) {
        if (m->displays[i].id == id) {
            *out = m->displays[i];
            return RoboOk;
        }
    }
    return RoboNotFound;
}

static void ScaleFixture(void)
{
    memset(&gSm, 0, sizeof(gSm));
    memset(&gSmCaps, 0, sizeof(gSmCaps));
    gSmCaps.enumerate = 1;
    gSmCaps.primary = 1;
    gSmCaps.GetInfo = 1;
    gSmCaps.workArea = 1;
    gSmCaps.scaleDpi = 1;

    memset(&gSmVtbl, 0, sizeof(gSmVtbl));
    gSmVtbl.name = "scale_mock";
    gSmVtbl.capabilities = &gSmCaps;
    gSmVtbl.initialize = SmInit;
    gSmVtbl.shutdown = SmShutdown;
    gSmVtbl.enumerate = SmEnumerate;
    gSmVtbl.GetPrimary = SmPrimary;
    gSmVtbl.GetInfo = SmGetInfo;

    WindowShutdown();
    DisplayShutdown();
    RoboCheckEqInt(DisplayInit(), RoboOk);
    RoboCheckEqInt(DisplayBackendRegister(&gSmVtbl, &gSm), RoboOk);
}

static void ScaleFixtureEnd(void)
{
    WindowShutdown();
    DisplayShutdown();
}

static void TestScaleValidate(void)
{
    union {
        float f;
        uint32_t u;
    } nan;
    union {
        float f;
        uint32_t u;
    } inf;

    RoboCheckEqInt(DisplayScaleValidate(1.0f), RoboOk);
    RoboCheckEqInt(DisplayScaleValidate(1.25f), RoboOk);
    RoboCheckEqInt(DisplayScaleValidate(1.5f), RoboOk);
    RoboCheckEqInt(DisplayScaleValidate(1.75f), RoboOk);
    RoboCheckEqInt(DisplayScaleValidate(2.0f), RoboOk);
    RoboCheckEqInt(DisplayScaleValidate(2.5f), RoboOk);
    RoboCheckEqInt(DisplayScaleValidate(3.0f), RoboOk);
    RoboCheckEqInt(DisplayScaleValidate(0.25f), RoboOk);
    RoboCheckEqInt(DisplayScaleValidate(8.0f), RoboOk);

    RoboCheckEqInt(DisplayScaleValidate(0.0f), RoboInvalidArgument);
    RoboCheckEqInt(DisplayScaleValidate(-1.0f), RoboInvalidArgument);
    RoboCheckEqInt(DisplayScaleValidate(0.1f), RoboInvalidArgument);
    RoboCheckEqInt(DisplayScaleValidate(9.0f), RoboInvalidArgument);

    nan.u = 0x7FC00000u; /* quiet NaN */
    RoboCheckEqInt(DisplayScaleValidate(nan.f), RoboInvalidArgument);

    inf.u = 0x7F800000u; /* +Inf */
    RoboCheckEqInt(DisplayScaleValidate(inf.f), RoboInvalidArgument);
    RoboCheckEqInt(DisplayScaleIsKnown(0.0f), 0);
    RoboCheckEqInt(DisplayScaleIsKnown(1.5f), 1);
}

static void TestDpiValidate(void)
{
    union {
        float f;
        uint32_t u;
    } nan;
    DisplayDpi pair;

    RoboCheckEqInt(DisplayDpiValidate(0.0f), RoboOk);
    RoboCheckEqInt(DisplayDpiIsUnavailable(0.0f), 1);
    RoboCheckEqInt(DisplayDpiIsKnown(0.0f), 0);
    RoboCheckEqInt(DisplayDpiValidate(96.0f), RoboOk);
    RoboCheckEqInt(DisplayDpiValidate(120.0f), RoboOk);
    RoboCheckEqInt(DisplayDpiValidate(144.5f), RoboOk);
    RoboCheckEqInt(DisplayDpiIsKnown(96.0f), 1);

    RoboCheckEqInt(DisplayDpiValidate(-1.0f), RoboInvalidArgument);
    RoboCheckEqInt(DisplayDpiValidate(0.5f), RoboInvalidArgument);
    RoboCheckEqInt(DisplayDpiValidate(2000.0f), RoboInvalidArgument);
    nan.u = 0x7FC00000u;
    RoboCheckEqInt(DisplayDpiValidate(nan.f), RoboInvalidArgument);

    DisplayDpiInit(&pair);
    RoboCheck(pair.dpiX == 0.0f);
    RoboCheckEqInt(DisplayDpiPairValidate(&pair), RoboOk);
    pair.dpiX = 96.0f;
    pair.dpiY = -1.0f;
    RoboCheckEqInt(DisplayDpiPairValidate(&pair),
                      RoboInvalidArgument);
}

static void TestScaleFromDpi(void)
{
    float s = 0.0f;

    RoboCheckEqInt(DisplayScaleFromDpi(96.0f, &s), RoboOk);
    RoboCheck(s > 0.99f && s < 1.01f);
    RoboCheckEqInt(DisplayScaleFromDpi(144.0f, &s), RoboOk);
    RoboCheck(s > 1.49f && s < 1.51f);
    RoboCheckEqInt(DisplayScaleFromDpi(192.0f, &s), RoboOk);
    RoboCheck(s > 1.99f && s < 2.01f);
    RoboCheckEqInt(DisplayScaleFromDpi(0.0f, &s),
                      RoboInvalidArgument);
}

static void CheckRoundtrip(int32_t logical, float scale)
{
    int32_t phys = 0;
    int32_t back = 0;

    RoboCheckEqInt(DisplayLogicalToPhysical(logical, scale, &phys),
                      RoboOk);
    RoboCheckEqInt(DisplayPhysicalToLogical(phys, scale, &back),
                      RoboOk);
    /* Nearest rounding: round-trip may differ by at most 1 for odd products. */
    {
        int32_t delta = back - logical;
        if (delta < 0) {
            delta = -delta;
        }
        RoboCheck(delta <= 1);
    }
}

static void TestConvertScalars(void)
{
    int32_t phys = 0;
    int32_t logical = 0;
    float scales[] = {1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f};
    size_t i;

    RoboCheckEqInt(DisplayLogicalToPhysical(100, 1.25f, &phys),
                      RoboOk);
    RoboCheckEqInt(phys, 125);
    RoboCheckEqInt(DisplayLogicalToPhysical(100, 1.5f, &phys),
                      RoboOk);
    RoboCheckEqInt(phys, 150);
    RoboCheckEqInt(DisplayLogicalToPhysical(100, 1.75f, &phys),
                      RoboOk);
    RoboCheckEqInt(phys, 175);
    RoboCheckEqInt(DisplayLogicalToPhysical(10, 2.5f, &phys),
                      RoboOk);
    RoboCheckEqInt(phys, 25);

    /* Nearest; ties away from zero */
    RoboCheckEqInt(DisplayLogicalToPhysical(1, 1.5f, &phys), RoboOk);
    RoboCheckEqInt(phys, 2);
    RoboCheckEqInt(DisplayLogicalToPhysical(-1, 1.5f, &phys),
                      RoboOk);
    RoboCheckEqInt(phys, -2);

    RoboCheckEqInt(DisplayPhysicalToLogical(125, 1.25f, &logical),
                      RoboOk);
    RoboCheckEqInt(logical, 100);

    for (i = 0u; i < sizeof(scales) / sizeof(scales[0]); i++) {
        CheckRoundtrip(800, scales[i]);
        CheckRoundtrip(-200, scales[i]);
        CheckRoundtrip(0, scales[i]);
    }

    RoboCheckEqInt(DisplayLogicalToPhysical(100, 0.0f, &phys),
                      RoboInvalidArgument);
    RoboCheckEqInt(
        DisplayLogicalToPhysical(2000000000, 8.0f, &phys),
        RoboInvalidArgument);
}

static void TestConvertSizePointRect(void)
{
    int32_t pw = 0;
    int32_t ph = 0;
    int32_t px = 0;
    int32_t py = 0;
    GfxRect in;
    GfxRect out;

    RoboCheckEqInt(
        DisplaySizeLogicalToPhysical(640, 480, 1.5f, &pw, &ph),
        RoboOk);
    RoboCheckEqInt(pw, 960);
    RoboCheckEqInt(ph, 720);

    RoboCheckEqInt(
        DisplayPointLogicalToPhysical(-100, 50, 2.0f, &px, &py),
        RoboOk);
    RoboCheckEqInt(px, -200);
    RoboCheckEqInt(py, 100);

    in = MakeGfxRect(-80, 40, 200, 100);
    RoboCheckEqInt(DisplayRectLogicalToPhysical(&in, 1.25f, &out),
                      RoboOk);
    RoboCheckEqInt(out.x, -100);
    RoboCheckEqInt(out.y, 50);
    RoboCheckEqInt(out.width, 250);
    RoboCheckEqInt(out.height, 125);

    RoboCheckEqInt(DisplayRectPhysicalToLogical(&out, 1.25f, &in),
                      RoboOk);
    RoboCheckEqInt(in.x, -80);
    RoboCheckEqInt(in.y, 40);
    RoboCheckEqInt(in.width, 200);
    RoboCheckEqInt(in.height, 100);
}

static void TestMultiDisplayScale(void)
{
    float s = 0.0f;
    DisplayDpi dpi;
    GfxRect phys;

    ScaleFixture();
    FillDisp(&gSm.displays[0], (DisplayId)1u, 0, 0, 1280, 800, 1280,
              800, 1.0f, 96.0f, 96.0f, 1);
    FillDisp(&gSm.displays[1], (DisplayId)2u, 1280, 0, 1280, 800,
              1920, 1200, 1.5f, 144.0f, 144.0f, 0);
    FillDisp(&gSm.displays[2], (DisplayId)3u, -1920, 0, 1920, 1080,
              3840, 2160, 2.0f, 192.0f, 192.0f, 0);
    gSm.count = 3u;
    RoboCheckEqInt(DisplayRefresh(), RoboOk);
    RoboCheckEqInt(DisplayCount(), 3);

    RoboCheckEqInt(DisplayGetScale((DisplayId)1u, &s),
                      RoboOk);
    RoboCheck(s > 0.99f && s < 1.01f);
    RoboCheckEqInt(DisplayGetScale((DisplayId)2u, &s),
                      RoboOk);
    RoboCheck(s > 1.49f && s < 1.51f);
    RoboCheckEqInt(DisplayGetScale((DisplayId)3u, &s),
                      RoboOk);
    RoboCheck(s > 1.99f && s < 2.01f);

    RoboCheckEqInt(DisplayGetDpi((DisplayId)2u, &dpi),
                      RoboOk);
    RoboCheck(dpi.dpiX > 143.0f && dpi.dpiX < 145.0f);

    {
        DisplayInfo info;
        RoboCheckEqInt(DisplayGet((DisplayId)3u, &info),
                          RoboOk);
        RoboCheckEqInt(DisplayInfoPhysicalBounds(&info, &phys),
                          RoboOk);
        RoboCheckEqInt(phys.x, -3840);
        RoboCheckEqInt(phys.width, 3840);
        RoboCheckEqInt(phys.height, 2160);
    }

    RoboCheckEqInt(DisplayGetScale((DisplayId)99u, &s),
                      RoboNotFound);
    ScaleFixtureEnd();
}

static void TestDpiUnavailableVsInvalid(void)
{
    DisplayInfo info;
    float s = 0.0f;
    DisplayDpi dpi;

    ScaleFixture();
    FillDisp(&gSm.displays[0], (DisplayId)1u, 0, 0, 800, 600, 0, 0,
              1.5f, 0.0f, 0.0f, 1);
    gSm.count = 1u;
    RoboCheckEqInt(DisplayRefresh(), RoboOk);
    RoboCheckEqInt(DisplayGet((DisplayId)1u, &info), RoboOk);
    RoboCheckEqInt(DisplayInfoGetDpi(&info, &dpi), RoboOk);
    RoboCheckEqInt(DisplayDpiIsUnavailable(dpi.dpiX), 1);
    RoboCheckEqInt(DisplayInfoEffectiveScale(&info, &s), RoboOk);
    RoboCheck(s > 1.49f && s < 1.51f);

    /* Scale unavailable; derive from DPI */
    FillDisp(&gSm.displays[0], (DisplayId)1u, 0, 0, 800, 600, 1600,
              1200, 0.0f, 192.0f, 192.0f, 1);
    RoboCheckEqInt(DisplayRefresh(), RoboOk);
    RoboCheckEqInt(DisplayGet((DisplayId)1u, &info), RoboOk);
    RoboCheckEqInt(DisplayInfoEffectiveScale(&info, &s), RoboOk);
    RoboCheck(s > 1.99f && s < 2.01f);

    /* Both unavailable */
    FillDisp(&gSm.displays[0], (DisplayId)1u, 0, 0, 800, 600, 0, 0,
              0.0f, 0.0f, 0.0f, 1);
    RoboCheckEqInt(DisplayRefresh(), RoboOk);
    RoboCheckEqInt(DisplayGet((DisplayId)1u, &info), RoboOk);
    RoboCheckEqInt(DisplayInfoEffectiveScale(&info, &s),
                      RoboNotFound);
    ScaleFixtureEnd();
}

static void TestWindowScale(void)
{
    WindowConfig cfg;
    WindowId id;
    float s = 0.0f;
    DisplayDpi dpi;
    GfxSize phys;

    /* Window init installs placeholder display; replace with scale mock. */
    WindowShutdown();
    RoboCheckEqInt(WindowInit(), RoboOk);
    RoboCheckEqInt(WindowBackendUseNull(), RoboOk);

    memset(&gSm, 0, sizeof(gSm));
    memset(&gSmCaps, 0, sizeof(gSmCaps));
    gSmCaps.enumerate = 1;
    gSmCaps.primary = 1;
    gSmCaps.GetInfo = 1;
    gSmCaps.workArea = 1;
    gSmCaps.scaleDpi = 1;
    memset(&gSmVtbl, 0, sizeof(gSmVtbl));
    gSmVtbl.name = "scale_mock";
    gSmVtbl.capabilities = &gSmCaps;
    gSmVtbl.initialize = SmInit;
    gSmVtbl.shutdown = SmShutdown;
    gSmVtbl.enumerate = SmEnumerate;
    gSmVtbl.GetPrimary = SmPrimary;
    gSmVtbl.GetInfo = SmGetInfo;
    RoboCheckEqInt(DisplayBackendRegister(&gSmVtbl, &gSm), RoboOk);

    FillDisp(&gSm.displays[0], (DisplayId)10u, 0, 0, 1280, 800, 1280,
              800, 1.0f, 96.0f, 96.0f, 1);
    FillDisp(&gSm.displays[1], (DisplayId)20u, 1280, 0, 1280, 800,
              2560, 1600, 2.0f, 192.0f, 192.0f, 0);
    gSm.count = 2u;
    RoboCheckEqInt(DisplayRefresh(), RoboOk);

    WindowConfigInit(&cfg);
    cfg.visible = 0;
    cfg.width = 400;
    cfg.height = 300;
    cfg.displayId = (DisplayId)10u;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(WindowGetScale(id, &s), RoboOk);
    RoboCheck(s > 0.99f && s < 1.01f);
    RoboCheckEqInt(WindowGetDpi(id, &dpi), RoboOk);
    RoboCheck(dpi.dpiX > 95.0f && dpi.dpiX < 97.0f);

    RoboCheckEqInt(
        WindowSetAssociatedDisplay(id, (DisplayId)20u),
        RoboOk);
    RoboCheckEqInt(WindowGetScale(id, &s), RoboOk);
    RoboCheck(s > 1.99f && s < 2.01f);
    RoboCheckEqInt(WindowGetSizePhysical(id, &phys), RoboOk);
    RoboCheckEqInt(phys.width, 800);
    RoboCheckEqInt(phys.height, 600);

    RoboCheckEqInt(WindowSetAssociatedDisplay(id, RoboDisplayIdNone),
                      RoboOk);
    RoboCheckEqInt(WindowGetDpi(id, &dpi), RoboNotFound);

    RoboCheckEqInt(WindowSetAssociatedDisplay(id, (DisplayId)99u),
                      RoboNotFound);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);

    WindowConfigInit(&cfg);
    cfg.visible = 0;
    cfg.width = 400;
    cfg.height = 300;
    cfg.scalePreference = 1.25f;
    cfg.displayId = (DisplayId)10u;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(WindowGetScale(id, &s), RoboOk);
    RoboCheck(s > 1.24f && s < 1.26f);
    RoboCheckEqInt(
        WindowSetAssociatedDisplay(id, (DisplayId)20u),
        RoboOk);
    RoboCheckEqInt(WindowGetScale(id, &s), RoboOk);
    RoboCheck(s > 1.24f && s < 1.26f);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);

    cfg.scalePreference = -1.0f;
    RoboCheckEqInt(WindowConfigValidate(&cfg), RoboInvalidArgument);
    cfg.scalePreference = 99.0f;
    RoboCheckEqInt(WindowConfigValidate(&cfg), RoboInvalidArgument);

    ScaleFixtureEnd();
}

void TestRegisterDisplayScale(void)
{
    TestRegister("display.scale.validate", TestScaleValidate);
    TestRegister("display.scale.dpi_validate", TestDpiValidate);
    TestRegister("display.scale.from_dpi", TestScaleFromDpi);
    TestRegister("display.scale.convert", TestConvertScalars);
    TestRegister("display.scale.geom", TestConvertSizePointRect);
    TestRegister("display.scale.multi", TestMultiDisplayScale);
    TestRegister("display.scale.unavailable", TestDpiUnavailableVsInvalid);
    TestRegister("display.scale.window", TestWindowScale);
}
