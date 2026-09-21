/*
 * SuiteRoboWindowGraphics — Goal 7.14 Window + RoboGraphics integration.
 */

#include "RoboDisplay/RoboDisplay.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboGraphics/RoboGraphicsScreenshot.h"
#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowGraphics.h"

#include <stdio.h>
#include <string.h>

typedef struct gfxMock {
    int presentCount;
    uint32_t lastW;
    uint32_t lastH;
    GfxPixelFormat lastFmt;
    uint32_t cornerTl;
    uint32_t cornerBr;
} GfxMock;

static GfxMock gGm;
static WindowBackendCaps gGmCaps;
static WindowBackendVtbl gGmVtbl;
static int gGmNextToken = 1;

static uint32_t PackRgbaSample(const GfxSurface *s, uint32_t x,
                                 uint32_t y)
{
    GfxColorRgba8 c;
    if (GfxSurfaceGetPixel(s, x, y, &c) != RoboOk) {
        return 0u;
    }
    return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
}

static Result GmInit(void *ud)
{
    (void)ud;
    return RoboOk;
}

static void GmShutdown(void *ud)
{
    (void)ud;
}

static Result GmCreate(void *ud, WindowId id,
                               const WindowConfig *cfg,
                               WindowNativeHandle *out)
{
    (void)ud;
    (void)id;
    (void)cfg;
    if (!out) {
        return RoboInvalidArgument;
    }
    WindowNativeHandleClear(out);
    out->token = (uint64_t)gGmNextToken++;
    out->generation = 1u;
    return RoboOk;
}

static Result GmDestroy(void *ud, WindowId id,
                                WindowNativeHandle *native)
{
    (void)ud;
    (void)id;
    WindowNativeHandleInvalidate(native);
    return RoboOk;
}

static Result GmShow(void *ud, WindowId id,
                             WindowNativeHandle *native)
{
    (void)ud;
    (void)id;
    (void)native;
    return RoboOk;
}

static Result GmHide(void *ud, WindowId id,
                             WindowNativeHandle *native)
{
    (void)ud;
    (void)id;
    (void)native;
    return RoboOk;
}

static Result GmTitle(void *ud, WindowId id,
                              WindowNativeHandle *native,
                              const char *title)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)title;
    return RoboOk;
}

static Result GmSize(void *ud, WindowId id,
                             WindowNativeHandle *native, int32_t w,
                             int32_t h)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)w;
    (void)h;
    return RoboOk;
}

static Result GmConstraints(void *ud, WindowId id,
                                    WindowNativeHandle *native,
                                    int32_t minW, int32_t minH, int32_t maxW,
                                    int32_t maxH)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)minW;
    (void)minH;
    (void)maxW;
    (void)maxH;
    return RoboOk;
}

static Result GmPos(void *ud, WindowId id,
                            WindowNativeHandle *native, int32_t x,
                            int32_t y)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)x;
    (void)y;
    return RoboOk;
}

static Result GmFocus(void *ud, WindowId id,
                              WindowNativeHandle *native)
{
    (void)ud;
    (void)id;
    (void)native;
    return RoboOk;
}

static Result GmQuery(void *ud, WindowId id,
                              const WindowNativeHandle *native,
                              WindowState *inout)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)inout;
    return RoboOk;
}

static Result GmPresent(void *ud, WindowId id,
                                WindowNativeHandle *native,
                                GfxSurface *surface)
{
    GfxMock *m = (GfxMock *)ud;
    (void)id;
    (void)native;
    if (!m || !surface) {
        return RoboInvalidArgument;
    }
    m->presentCount++;
    m->lastW = GfxSurfaceWidth(surface);
    m->lastH = GfxSurfaceHeight(surface);
    m->lastFmt = GfxSurfaceFormat(surface);
    m->cornerTl = PackRgbaSample(surface, 0, 0);
    if (m->lastW > 0u && m->lastH > 0u) {
        m->cornerBr = PackRgbaSample(surface, m->lastW - 1u, m->lastH - 1u);
    }
    return RoboOk;
}

static Result GmEvents(void *ud)
{
    (void)ud;
    return RoboOk;
}

static void GmBind(void)
{
    memset(&gGm, 0, sizeof(gGm));
    memset(&gGmCaps, 0, sizeof(gGmCaps));
    gGmCaps.RoboCreateWindow = 1;
    gGmCaps.RoboDestroyWindow = 1;
    gGmCaps.showHide = 1;
    gGmCaps.resize = 1;
    gGmCaps.move = 1;
    gGmCaps.SetTitle = 1;
    gGmCaps.focus = 1;
    gGmCaps.QueryState = 1;
    gGmCaps.present = 1;
    gGmCaps.ProcessEvents = 1;
    gGmCaps.sizeConstraints = 1;
    gGmCaps.presentFormatMask = RoboWindowPresentFmtCpuDefault;
    gGmCaps.presentMaxWidth = 8192u;
    gGmCaps.presentMaxHeight = 8192u;

    memset(&gGmVtbl, 0, sizeof(gGmVtbl));
    gGmVtbl.name = "gfx_mock";
    gGmVtbl.capabilities = &gGmCaps;
    gGmVtbl.initialize = GmInit;
    gGmVtbl.shutdown = GmShutdown;
    gGmVtbl.RoboCreateWindow = GmCreate;
    gGmVtbl.RoboDestroyWindow = GmDestroy;
    gGmVtbl.show = GmShow;
    gGmVtbl.hide = GmHide;
    gGmVtbl.SetTitle = GmTitle;
    gGmVtbl.SetSize = GmSize;
    gGmVtbl.SetSizeConstraints = GmConstraints;
    gGmVtbl.SetPosition = GmPos;
    gGmVtbl.focus = GmFocus;
    gGmVtbl.QueryState = GmQuery;
    gGmVtbl.PresentSurface = GmPresent;
    gGmVtbl.ProcessEvents = GmEvents;
}

static int BeginGfx(WindowId *outId, int32_t w, int32_t h)
{
    WindowConfig cfg;

    GmBind();
    RoboCheckEqInt(WindowInit(), RoboOk);
    RoboCheckEqInt(WindowBackendRegister(&gGmVtbl, &gGm), RoboOk);
    WindowConfigInit(&cfg);
    cfg.width = w;
    cfg.height = h;
    cfg.visible = 0;
    cfg.title = "gfx-int";
    RoboCheckEqInt(WindowCreate(&cfg, outId), RoboOk);
    return 1;
}

static void TestGfxFrameLifecycle(void)
{
    WindowId id = RoboWindowIdNone;
    GfxSurface *surf = NULL;
    GfxRaster *raster = NULL;
    GfxSize rsz;
    GfxColorRgba8 px;

    BeginGfx(&id, 200, 100);
    RoboCheck(WindowGetRenderSurface(id) == NULL);
    RoboCheck(!WindowIsDirty(id));
    RoboCheck(!WindowFrameIsOpen(id));

    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    RoboCheck(surf != NULL);
    RoboCheck(WindowFrameIsOpen(id));
    {
        GfxSurface *nested = surf;
        RoboCheckEqInt(WindowBeginFrame(id, &nested), RoboBusy);
        RoboCheck(nested == surf); /* out-param unchanged on BUSY */
    }

    RoboCheckEqInt(WindowGetRenderSize(id, &rsz), RoboOk);
    RoboCheckEqInt(rsz.width, 200);
    RoboCheckEqInt(rsz.height, 100);
    RoboCheck(GfxSurfaceFormat(surf) == RoboGfxFormatBgra8);

    RoboCheckEqInt(GfxRasterCreate(surf, &raster), RoboOk);
    (void)GfxRasterClear(raster,
                                (GfxColorRgba8){245, 245, 247, 255});
    (void)GfxRasterFillRect(
        raster, MakeGfxRect(10, 10, 40, 30),
        (GfxColorRgba8){14, 122, 255, 255});
    (void)GfxRasterDrawLine(
        raster, MakeGfxPoint(0, 0), MakeGfxPoint(199, 99),
        (GfxColorRgba8){255, 59, 48, 255});
    (void)GfxRasterSetOpacity(raster, 128);
    (void)GfxRasterFillRect(
        raster, MakeGfxRect(80, 40, 50, 40),
        (GfxColorRgba8){48, 209, 88, 255});
    GfxRasterSetOpacity(raster, 255);
    (void)GfxSurfaceSetPixel(surf, 199, 99,
                                     (GfxColorRgba8){0, 0, 255, 255});
    GfxRasterDestroy(raster);

    RoboCheckEqInt(WindowEndFrame(id), RoboOk);
    RoboCheck(!WindowFrameIsOpen(id));
    RoboCheck(WindowIsDirty(id));
    RoboCheckEqInt(WindowEndFrame(id), RoboInvalidArgument);

    RoboCheckEqInt(WindowPresentFrame(id), RoboOk);
    RoboCheckEqInt(gGm.presentCount, 1);
    RoboCheckEqInt((int)gGm.lastW, 200);
    RoboCheckEqInt((int)gGm.lastH, 100);
    RoboCheck(!WindowIsDirty(id));
    /* Second present with no dirty → no backend call. */
    RoboCheckEqInt(WindowPresentFrame(id), RoboOk);
    RoboCheckEqInt(gGm.presentCount, 1);

    RoboCheckEqInt(GfxSurfaceGetPixel(surf, 10, 10, &px), RoboOk);
    RoboCheckEqInt((int)px.r, 14);
    RoboCheckEqInt((int)px.g, 122);
    RoboCheckEqInt((int)px.b, 255);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheck(WindowGetRenderSurface(id) == NULL);
    WindowShutdown();
}

static void TestGfxResizeRecreate(void)
{
    WindowId id = RoboWindowIdNone;
    GfxSurface *surf = NULL;
    GfxSize rsz;
    uint32_t firstW = 0u;
    uint32_t firstH = 0u;

    BeginGfx(&id, 160, 120);
    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    firstW = GfxSurfaceWidth(surf);
    firstH = GfxSurfaceHeight(surf);
    RoboCheckEqInt((int)firstW, 160);
    RoboCheckEqInt((int)firstH, 120);
    (void)GfxSurfaceClear(surf, (GfxColorRgba8){0, 0, 0, 255});
    RoboCheckEqInt(WindowEndFrame(id), RoboOk);
    RoboCheckEqInt(WindowPresentFrame(id), RoboOk);

    RoboCheckEqInt(WindowSetSize(id, 320, 240), RoboOk);
    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    RoboCheck(surf != NULL);
    RoboCheckEqInt(WindowGetRenderSize(id, &rsz), RoboOk);
    RoboCheckEqInt(rsz.width, 320);
    RoboCheckEqInt(rsz.height, 240);
    RoboCheck(GfxSurfaceWidth(surf) == 320u);
    RoboCheck(GfxSurfaceHeight(surf) == 240u);
    (void)GfxSurfaceClear(surf,
                                 (GfxColorRgba8){255, 255, 255, 255});
    RoboCheckEqInt(WindowEndFrame(id), RoboOk);
    RoboCheckEqInt(WindowPresentFrame(id), RoboOk);
    RoboCheckEqInt((int)gGm.lastW, 320);
    RoboCheckEqInt((int)gGm.lastH, 240);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

static void TestGfxScalePhysical(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    GfxSurface *surf = NULL;
    GfxSize rsz;
    float scale = 0.0f;

    GmBind();
    RoboCheckEqInt(WindowInit(), RoboOk);
    RoboCheckEqInt(WindowBackendRegister(&gGmVtbl, &gGm), RoboOk);
    WindowConfigInit(&cfg);
    cfg.width = 100;
    cfg.height = 50;
    cfg.scalePreference = 2.0f;
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(WindowGetScale(id, &scale), RoboOk);
    RoboCheck(scale == 2.0f);

    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    RoboCheckEqInt(WindowGetRenderSize(id, &rsz), RoboOk);
    RoboCheckEqInt(rsz.width, 200);
    RoboCheckEqInt(rsz.height, 100);
    RoboCheckEqInt(WindowEndFrame(id), RoboOk);
    RoboCheckEqInt(WindowPresentFrame(id), RoboOk);
    RoboCheckEqInt((int)gGm.lastW, 200);
    RoboCheckEqInt((int)gGm.lastH, 100);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

static void TestGfxInvalidateAndErrors(void)
{
    WindowId id = RoboWindowIdNone;
    GfxSurface *surf = NULL;

    BeginGfx(&id, 64, 64);
    RoboCheckEqInt(WindowInvalidate(id), RoboOk);
    RoboCheck(WindowIsDirty(id));
    RoboCheckEqInt(WindowPresentFrame(id), RoboOk);
    RoboCheckEqInt(gGm.presentCount, 1);
    RoboCheck(!WindowIsDirty(id));

    RoboCheckEqInt(WindowInvalidateRect(id, MakeGfxRect(0, 0, 0, 0)),
                      RoboInvalidArgument);
    RoboCheckEqInt(WindowInvalidateRect(id, MakeGfxRect(1, 1, 8, 8)),
                      RoboOk);
    RoboCheck(WindowIsDirty(id));
    RoboCheckEqInt(WindowClearDirty(id), RoboOk);
    RoboCheck(!WindowIsDirty(id));

    RoboCheckEqInt(WindowBeginFrame(id, NULL), RoboInvalidArgument);
    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    RoboCheckEqInt(WindowPresentFrame(id), RoboBusy);
    RoboCheckEqInt(WindowReleaseRenderSurface(id), RoboBusy);
    RoboCheckEqInt(WindowEndFrame(id), RoboOk);

    RoboCheckEqInt(WindowSetRenderFormat(id, RoboGfxFormatAlpha8),
                      RoboNotSupported);
    RoboCheckEqInt(WindowSetRenderFormat(id, RoboGfxFormatRgba8),
                      RoboOk);
    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    RoboCheck(GfxSurfaceFormat(surf) == RoboGfxFormatRgba8);
    RoboCheckEqInt(WindowEndFrame(id), RoboOk);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboNotFound);
    RoboCheckEqInt(WindowPresentFrame(id), RoboNotFound);
    WindowShutdown();
}

static void TestGfxPresentNullUsesOwned(void)
{
    WindowId id = RoboWindowIdNone;
    GfxSurface *surf = NULL;

    BeginGfx(&id, 32, 32);
    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    (void)GfxSurfaceClear(surf, (GfxColorRgba8){1, 2, 3, 255});
    RoboCheckEqInt(WindowEndFrame(id), RoboOk);
    /* Explicit present(NULL) falls back to owned render surface. */
    RoboCheckEqInt(WindowPresent(id, NULL), RoboOk);
    RoboCheckEqInt(gGm.presentCount, 1);
    RoboCheckEqInt((int)gGm.lastW, 32);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

static void TestGfxScreenshotPixels(void)
{
    WindowId id = RoboWindowIdNone;
    GfxSurface *surf = NULL;
    GfxScreenshot *shot = NULL;
    GfxSurface *shotSurf = NULL;
    GfxColorRgba8 px;

    BeginGfx(&id, 48, 48);
    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    (void)GfxSurfaceClear(surf, (GfxColorRgba8){0, 0, 0, 255});
    (void)GfxSurfaceSetPixel(surf, 0, 0,
                                     (GfxColorRgba8){255, 255, 255, 255});
    (void)GfxSurfaceFillRect(
        surf, MakeGfxRect(16, 16, 8, 8),
        (GfxColorRgba8){14, 122, 255, 255});
    RoboCheckEqInt(WindowEndFrame(id), RoboOk);
    RoboCheckEqInt(WindowPresentFrame(id), RoboOk);

    RoboCheckEqInt(GfxScreenshotCaptureSurface(surf, NULL, &shot),
                      RoboOk);
    shotSurf = GfxScreenshotSurface(shot);
    RoboCheckEqInt(GfxSurfaceGetPixel(shotSurf, 0, 0, &px), RoboOk);
    RoboCheckEqInt((int)px.r, 255);
    RoboCheckEqInt(GfxSurfaceGetPixel(shotSurf, 16, 16, &px),
                      RoboOk);
    RoboCheckEqInt((int)px.r, 14);
    RoboCheckEqInt((int)px.g, 122);
    GfxScreenshotRelease(shot);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

void TestRegisterWindowGraphics(void)
{
    TestRegister("window.graphics.frame_lifecycle",
                       TestGfxFrameLifecycle);
    TestRegister("window.graphics.resize_recreate",
                       TestGfxResizeRecreate);
    TestRegister("window.graphics.scale_physical",
                       TestGfxScalePhysical);
    TestRegister("window.graphics.invalidate_errors",
                       TestGfxInvalidateAndErrors);
    TestRegister("window.graphics.present_owned",
                       TestGfxPresentNullUsesOwned);
    TestRegister("window.graphics.screenshot",
                       TestGfxScreenshotPixels);
}
