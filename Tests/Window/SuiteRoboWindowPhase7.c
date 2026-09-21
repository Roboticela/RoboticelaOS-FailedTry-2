/*
 * SuiteRoboWindowPhase7 — Goal 7.15 Phase 7 acceptance / regression.
 *
 * Consolidates lifecycle, graphics-through-window, input, stale-ID safety,
 * failure injection, multi-window, pixel validation, and light perf.
 * Host/QEMU real backends remain in window.host.* / window.qemu.* + smoke scripts.
 */

#include "RoboDisplay/RoboDisplay.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboGraphics/RoboGraphicsPath.h"
#include "RoboGraphics/RoboGraphicsScreenshot.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowBackendHost.h"
#include "RoboWindow/RoboWindowBackendQemu.h"
#include "RoboWindow/RoboWindowGraphics.h"
#include "RoboWindow/RoboWindowVersion.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* ---- Mock window backend ---- */

typedef struct p7Mock {
    int presentCount;
    int createCount;
    int destroyCount;
    int failCreate;
    int failPresent;
    int failSize;
    uint32_t lastW;
    uint32_t lastH;
    int nextToken;
} P7Mock;

static P7Mock gP7;
static WindowBackendCaps gP7Caps;
static WindowBackendVtbl gP7Vtbl;

static Result P7Ok(void *ud)
{
    (void)ud;
    return RoboOk;
}

static void P7Shutdown(void *ud)
{
    (void)ud;
}

static Result P7Create(void *ud, WindowId id,
                               const WindowConfig *cfg,
                               WindowNativeHandle *out)
{
    P7Mock *m = (P7Mock *)ud;
    (void)id;
    (void)cfg;
    if (!out) {
        return RoboInvalidArgument;
    }
    if (m && m->failCreate) {
        return RoboError;
    }
    if (m) {
        m->createCount++;
    }
    WindowNativeHandleClear(out);
    out->token = (uint64_t)(m ? m->nextToken++ : 1);
    out->generation = 1u;
    return RoboOk;
}

static Result P7Destroy(void *ud, WindowId id,
                                WindowNativeHandle *native)
{
    P7Mock *m = (P7Mock *)ud;
    (void)id;
    if (m) {
        m->destroyCount++;
    }
    WindowNativeHandleInvalidate(native);
    return RoboOk;
}

static Result P7NopWin(void *ud, WindowId id,
                                WindowNativeHandle *native)
{
    (void)ud;
    (void)id;
    (void)native;
    return RoboOk;
}

static Result P7Title(void *ud, WindowId id,
                              WindowNativeHandle *native,
                              const char *title)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)title;
    return RoboOk;
}

static Result P7Size(void *ud, WindowId id,
                             WindowNativeHandle *native, int32_t w,
                             int32_t h)
{
    P7Mock *m = (P7Mock *)ud;
    (void)id;
    (void)native;
    (void)w;
    (void)h;
    if (m && m->failSize) {
        return RoboError;
    }
    return RoboOk;
}

static Result P7Constraints(void *ud, WindowId id,
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

static Result P7Pos(void *ud, WindowId id,
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

static Result P7Query(void *ud, WindowId id,
                              const WindowNativeHandle *native,
                              WindowState *inout)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)inout;
    return RoboOk;
}

static Result P7Present(void *ud, WindowId id,
                                WindowNativeHandle *native,
                                GfxSurface *surface)
{
    P7Mock *m = (P7Mock *)ud;
    (void)id;
    (void)native;
    if (!m || !surface) {
        return RoboInvalidArgument;
    }
    if (m->failPresent) {
        return RoboIoError;
    }
    m->presentCount++;
    m->lastW = GfxSurfaceWidth(surface);
    m->lastH = GfxSurfaceHeight(surface);
    return RoboOk;
}

static Result P7Events(void *ud)
{
    (void)ud;
    return RoboOk;
}

static void P7Bind(void)
{
    memset(&gP7, 0, sizeof(gP7));
    gP7.nextToken = 1;
    memset(&gP7Caps, 0, sizeof(gP7Caps));
    gP7Caps.RoboCreateWindow = 1;
    gP7Caps.RoboDestroyWindow = 1;
    gP7Caps.showHide = 1;
    gP7Caps.resize = 1;
    gP7Caps.move = 1;
    gP7Caps.SetTitle = 1;
    gP7Caps.focus = 1;
    gP7Caps.QueryState = 1;
    gP7Caps.present = 1;
    gP7Caps.ProcessEvents = 1;
    gP7Caps.sizeConstraints = 1;
    gP7Caps.presentFormatMask = RoboWindowPresentFmtCpuDefault;
    gP7Caps.presentMaxWidth = 8192u;
    gP7Caps.presentMaxHeight = 8192u;

    memset(&gP7Vtbl, 0, sizeof(gP7Vtbl));
    gP7Vtbl.name = "p7_mock";
    gP7Vtbl.capabilities = &gP7Caps;
    gP7Vtbl.initialize = P7Ok;
    gP7Vtbl.shutdown = P7Shutdown;
    gP7Vtbl.RoboCreateWindow = P7Create;
    gP7Vtbl.RoboDestroyWindow = P7Destroy;
    gP7Vtbl.show = P7NopWin;
    gP7Vtbl.hide = P7NopWin;
    gP7Vtbl.SetTitle = P7Title;
    gP7Vtbl.SetSize = P7Size;
    gP7Vtbl.SetSizeConstraints = P7Constraints;
    gP7Vtbl.SetPosition = P7Pos;
    gP7Vtbl.focus = P7NopWin;
    gP7Vtbl.QueryState = P7Query;
    gP7Vtbl.PresentSurface = P7Present;
    gP7Vtbl.ProcessEvents = P7Events;
}

static void P7Fixture(void)
{
    P7Bind();
    RoboCheckEqInt(WindowInit(), RoboOk);
    RoboCheckEqInt(WindowBackendRegister(&gP7Vtbl, &gP7), RoboOk);
}

static void P7End(void)
{
    WindowShutdown();
}

static void DrainEvents(void)
{
    WindowEvent ev;
    while (WindowEventPoll(&ev) == RoboOk) {
    }
}

static WindowId P7CreateWindow(int32_t w, int32_t h, float scale)
{
    WindowConfig cfg;
    WindowId id = RoboWindowIdNone;

    WindowConfigInit(&cfg);
    cfg.width = w;
    cfg.height = h;
    cfg.visible = 0;
    cfg.title = "p7";
    if (scale > 0.0f) {
        cfg.scalePreference = scale;
    }
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    return id;
}

static int ColorNear(GfxColorRgba8 c, uint8_t r, uint8_t g, uint8_t b,
                      int tol)
{
    int dr = (int)c.r - (int)r;
    int dg = (int)c.g - (int)g;
    int db = (int)c.b - (int)b;
    if (dr < 0) {
        dr = -dr;
    }
    if (dg < 0) {
        dg = -dg;
    }
    if (db < 0) {
        db = -db;
    }
    return dr <= tol && dg <= tol && db <= tol;
}

static void ExpectPixel(GfxSurface *s, uint32_t x, uint32_t y,
                         uint8_t r, uint8_t g, uint8_t b, int tol)
{
    GfxColorRgba8 px;
    RoboCheckEqInt(GfxSurfaceGetPixel(s, x, y, &px), RoboOk);
    if (!ColorNear(px, r, g, b, tol)) {
        char msg[160];
        (void)snprintf(msg, sizeof(msg),
                       "pixel (%u,%u) got rgb(%u,%u,%u) expected (~%u,%u,%u) "
                       "tol=%d backend=p7_mock",
                       (unsigned)x, (unsigned)y, (unsigned)px.r, (unsigned)px.g,
                       (unsigned)px.b, (unsigned)r, (unsigned)g, (unsigned)b,
                       tol);
        RoboCheckMsg(0, msg);
    }
}

/** Deterministic Phase 7 visual fixture (logical/physical coords). */
static void DrawPhase7Pattern(GfxSurface *surf)
{
    GfxRaster *raster = NULL;
    GfxPath *path = NULL;
    uint32_t w = GfxSurfaceWidth(surf);
    uint32_t h = GfxSurfaceHeight(surf);

    RoboCheckEqInt(GfxRasterCreate(surf, &raster), RoboOk);
    RoboCheckEqInt(
        GfxRasterClear(raster, (GfxColorRgba8){245, 245, 247, 255}),
        RoboOk);

    /* Border */
    RoboCheckEqInt(
        GfxRasterFillRect(raster, MakeGfxRect(0, 0, (int32_t)w, 2),
                                  (GfxColorRgba8){29, 29, 31, 255}),
        RoboOk);
    RoboCheckEqInt(
        GfxRasterFillRect(
            raster, MakeGfxRect(0, (int32_t)h - 2, (int32_t)w, 2),
            (GfxColorRgba8){29, 29, 31, 255}),
        RoboOk);

    /* Interior rectangle + diagonal (before corners so markers stay exact) */
    RoboCheckEqInt(
        GfxRasterFillRect(raster, MakeGfxRect(20, 20, 40, 30),
                                  (GfxColorRgba8){14, 122, 255, 255}),
        RoboOk);
    RoboCheckEqInt(
        GfxRasterDrawLine(
            raster, MakeGfxPoint(8, 8),
            MakeGfxPoint((int32_t)w - 9, (int32_t)h - 9),
            (GfxColorRgba8){255, 59, 48, 255}),
        RoboOk);

    /* Corner markers (exact pixels for validation) */
    RoboCheckEqInt(
        GfxRasterFillRect(raster, MakeGfxRect(0, 0, 4, 4),
                                  (GfxColorRgba8){255, 59, 48, 255}),
        RoboOk);
    RoboCheckEqInt(
        GfxRasterFillRect(
            raster, MakeGfxRect((int32_t)w - 4, 0, 4, 4),
            (GfxColorRgba8){48, 209, 88, 255}),
        RoboOk);
    RoboCheckEqInt(
        GfxRasterFillRect(
            raster, MakeGfxRect(0, (int32_t)h - 4, 4, 4),
            (GfxColorRgba8){14, 122, 255, 255}),
        RoboOk);
    RoboCheckEqInt(
        GfxRasterFillRect(
            raster,
            MakeGfxRect((int32_t)w - 4, (int32_t)h - 4, 4, 4),
            (GfxColorRgba8){255, 159, 10, 255}),
        RoboOk);

    /* Alpha region */
    GfxRasterSetOpacity(raster, 128);
    RoboCheckEqInt(
        GfxRasterFillRect(raster, MakeGfxRect(50, 40, 40, 30),
                                  (GfxColorRgba8){48, 209, 88, 255}),
        RoboOk);
    GfxRasterSetOpacity(raster, 255);

    /* Path triangle (foundation): build path, stroke via line segments */
    RoboCheckEqInt(GfxPathCreate(&path), RoboOk);
    RoboCheckEqInt(
        GfxPathMoveTo(path, MakeGfxPoint((int32_t)w / 2, 10)),
        RoboOk);
    RoboCheckEqInt(
        GfxPathLineTo(path, MakeGfxPoint((int32_t)w / 2 + 20, 40)),
        RoboOk);
    RoboCheckEqInt(
        GfxPathLineTo(path, MakeGfxPoint((int32_t)w / 2 - 20, 40)),
        RoboOk);
    RoboCheckEqInt(GfxPathClose(path), RoboOk);
    RoboCheckEqInt((int)GfxPathCommandCount(path) >= 3, 1);
    RoboCheckEqInt(
        GfxRasterDrawLine(
            raster, MakeGfxPoint((int32_t)w / 2, 10),
            MakeGfxPoint((int32_t)w / 2 + 20, 40),
            (GfxColorRgba8){110, 110, 115, 255}),
        RoboOk);
    RoboCheckEqInt(
        GfxRasterDrawLine(
            raster, MakeGfxPoint((int32_t)w / 2 + 20, 40),
            MakeGfxPoint((int32_t)w / 2 - 20, 40),
            (GfxColorRgba8){110, 110, 115, 255}),
        RoboOk);
    RoboCheckEqInt(
        GfxRasterDrawLine(
            raster, MakeGfxPoint((int32_t)w / 2 - 20, 40),
            MakeGfxPoint((int32_t)w / 2, 10),
            (GfxColorRgba8){110, 110, 115, 255}),
        RoboOk);
    GfxPathRelease(path);

    /* Center marker */
    RoboCheckEqInt(
        GfxRasterFillRect(
            raster,
            MakeGfxRect((int32_t)w / 2 - 2, (int32_t)h / 2 - 2, 4, 4),
            (GfxColorRgba8){0, 0, 0, 255}),
        RoboOk);

    GfxRasterDestroy(raster);
}

static void ValidatePhase7Pattern(GfxSurface *surf)
{
    uint32_t w = GfxSurfaceWidth(surf);
    uint32_t h = GfxSurfaceHeight(surf);

    ExpectPixel(surf, 0, 0, 255, 59, 48, 0);
    ExpectPixel(surf, w - 1, 0, 48, 209, 88, 0);
    ExpectPixel(surf, 0, h - 1, 14, 122, 255, 0);
    ExpectPixel(surf, w - 1, h - 1, 255, 159, 10, 0);
    ExpectPixel(surf, 25, 25, 14, 122, 255, 0);
    ExpectPixel(surf, w / 2, h / 2, 0, 0, 0, 0);
    /* Background sample away from geometry */
    ExpectPixel(surf, 10, h / 2, 245, 245, 247, 2);
}

/* ---- Tests ---- */

static void TestP7Version(void)
{
    RoboCheckStrEq(RoboWindowVersionString, "0.15.0");
    RoboCheckStrEq(WindowVersionString(), "0.15.0");
    RoboCheckEqInt(RoboWindowApiFrozen, 0);
}

static void TestP7LifecycleMatrix(void)
{
    WindowId id;
    GfxSurface *surf = NULL;
    size_t live0;

    P7Fixture();
    live0 = MemDebugIsEnabled() ? MemDebugLiveCount() : 0u;

    /* create → destroy */
    id = P7CreateWindow(120, 80, 0.0f);
    RoboCheck(WindowIdIsLive(id));
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheck(!WindowIdIsLive(id));

    /* create → draw → present → resize → redraw → close path */
    id = P7CreateWindow(160, 100, 0.0f);
    DrainEvents();
    RoboCheckEqInt(WindowShow(id), RoboOk);
    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    DrawPhase7Pattern(surf);
    RoboCheckEqInt(WindowEndFrame(id), RoboOk);
    RoboCheckEqInt(WindowPresentFrame(id), RoboOk);
    RoboCheckEqInt(gP7.presentCount, 1);

    RoboCheckEqInt(WindowSetSize(id, 200, 140), RoboOk);
    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    RoboCheckEqInt((int)GfxSurfaceWidth(surf), 200);
    RoboCheckEqInt((int)GfxSurfaceHeight(surf), 140);
    DrawPhase7Pattern(surf);
    RoboCheckEqInt(WindowEndFrame(id), RoboOk);
    RoboCheckEqInt(WindowPresentFrame(id), RoboOk);

    RoboCheckEqInt(WindowRequestClose(id), RoboOk);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);

    /* repeated create/destroy cycles */
    {
        int i;
        for (i = 0; i < 8; i++) {
            id = P7CreateWindow(64 + i, 48 + i, 0.0f);
            RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
            (void)GfxSurfaceClear(
                surf, (GfxColorRgba8){0, 0, 0, 255});
            RoboCheckEqInt(WindowEndFrame(id), RoboOk);
            RoboCheckEqInt(WindowPresentFrame(id), RoboOk);
            RoboCheckEqInt(WindowDestroy(id), RoboOk);
        }
    }

    RoboCheckEqInt((int)WindowLiveCount(), 0);
    if (MemDebugIsEnabled()) {
        RoboCheckEqInt((int)MemDebugLiveCount(), (int)live0);
    }
    P7End();
}

static void TestP7GraphicsIntegration(void)
{
    WindowId id;
    GfxSurface *surf = NULL;
    GfxScreenshot *shot = NULL;
    GfxSurface *shotSurf;

    P7Fixture();
    id = P7CreateWindow(128, 96, 0.0f);
    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    DrawPhase7Pattern(surf);
    ValidatePhase7Pattern(surf);
    RoboCheckEqInt(WindowEndFrame(id), RoboOk);
    RoboCheckEqInt(WindowPresentFrame(id), RoboOk);

    RoboCheckEqInt(GfxScreenshotCaptureSurface(surf, NULL, &shot),
                      RoboOk);
    shotSurf = GfxScreenshotSurface(shot);
    ValidatePhase7Pattern(shotSurf);
    GfxScreenshotRelease(shot);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    P7End();
}

static void TestP7ScalePhysical(void)
{
    WindowId id;
    GfxSurface *surf = NULL;
    GfxSize logical;
    GfxSize physical;
    float scale = 0.0f;

    P7Fixture();
    id = P7CreateWindow(100, 50, 2.0f);
    RoboCheckEqInt(WindowGetSize(id, &logical), RoboOk);
    RoboCheckEqInt(logical.width, 100);
    RoboCheckEqInt(logical.height, 50);
    RoboCheckEqInt(WindowGetScale(id, &scale), RoboOk);
    RoboCheck(scale > 1.9f && scale < 2.1f);
    RoboCheckEqInt(WindowGetSizePhysical(id, &physical), RoboOk);
    RoboCheckEqInt(physical.width, 200);
    RoboCheckEqInt(physical.height, 100);

    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    RoboCheckEqInt((int)GfxSurfaceWidth(surf), 200);
    RoboCheckEqInt((int)GfxSurfaceHeight(surf), 100);
    RoboCheckEqInt(WindowEndFrame(id), RoboOk);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    P7End();
}

static void TestP7ResizeSafety(void)
{
    WindowId id;
    GfxSurface *surf = NULL;
    GfxSurface *old = NULL;
    int i;

    P7Fixture();
    id = P7CreateWindow(80, 60, 0.0f);

    /* Resize before first frame */
    RoboCheckEqInt(WindowSetSize(id, 90, 70), RoboOk);
    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    RoboCheckEqInt((int)GfxSurfaceWidth(surf), 90);
    RoboCheckEqInt(WindowEndFrame(id), RoboOk);
    RoboCheckEqInt(WindowPresentFrame(id), RoboOk);

    /* Repeated resize while dirty */
    RoboCheckEqInt(WindowInvalidate(id), RoboOk);
    for (i = 0; i < 5; i++) {
        RoboCheckEqInt(WindowSetSize(id, 100 + i * 10, 80 + i * 5),
                          RoboOk);
    }
    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    RoboCheckEqInt((int)GfxSurfaceWidth(surf), 140);
    old = surf;
    RoboCheckEqInt(WindowEndFrame(id), RoboOk);

    RoboCheckEqInt(WindowSetSize(id, 160, 120), RoboOk);
    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    RoboCheck(surf != old);
    RoboCheckEqInt((int)GfxSurfaceWidth(surf), 160);
    RoboCheckEqInt(WindowEndFrame(id), RoboOk);
    RoboCheckEqInt(WindowPresentFrame(id), RoboOk);

    /* Resize then close */
    RoboCheckEqInt(WindowSetSize(id, 50, 40), RoboOk);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheck(WindowGetRenderSurface(id) == NULL);
    P7End();
}

static void TestP7StaleEvents(void)
{
    WindowId a;
    WindowId b;
    KeyboardEvent ke;
    MouseEvent me;
    WindowEvent ev;
    int sawClosedA = 0;
    int sawKeyForA = 0;
    int sawKeyForB = 0;

    P7Fixture();
    a = P7CreateWindow(100, 80, 0.0f);
    RoboCheckEqInt(WindowObserveFocus(a, 1), RoboOk);
    DrainEvents();

    KeyboardEventInit(&ke);
    ke.action = RoboKeyActionDown;
    ke.physicalKey = RoboPhysicalKeyA;
    ke.logicalKey = RoboLogicalKeyA;
    RoboCheckEqInt(WindowObserveKey(a, &ke), RoboOk);
    RoboCheckEqInt((int)WindowEventQueueSize() >= 1, 1);

    /* Destroy A: pending key for A must be purged; CLOSED remains. */
    RoboCheckEqInt(WindowDestroy(a), RoboOk);
    RoboCheckEqInt(WindowObserveKey(a, &ke), RoboNotFound);

    b = P7CreateWindow(100, 80, 0.0f);
    RoboCheck(b != a);
    RoboCheckEqInt(WindowObserveFocus(b, 1), RoboOk);

    while (WindowEventPoll(&ev) == RoboOk) {
        if (ev.type == RoboWindowEventClosed && ev.windowId == a) {
            sawClosedA = 1;
        }
        if ((ev.type == RoboWindowEventKeyDown ||
             ev.type == RoboWindowEventKeyUp) &&
            ev.windowId == a) {
            sawKeyForA = 1;
        }
        if ((ev.type == RoboWindowEventKeyDown ||
             ev.type == RoboWindowEventKeyUp) &&
            ev.windowId == b) {
            sawKeyForB = 1;
        }
    }
    RoboCheck(sawClosedA);
    RoboCheck(!sawKeyForA);
    RoboCheck(!sawKeyForB); /* focus events drained; no key for B yet */

    KeyboardEventInit(&ke);
    ke.action = RoboKeyActionDown;
    ke.physicalKey = RoboPhysicalKeyB;
    ke.logicalKey = RoboLogicalKeyB;
    RoboCheckEqInt(WindowObserveKey(b, &ke), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheck(ev.windowId == b);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventKeyDown);

    MouseEventInit(&me);
    me.type = RoboMouseEventMove;
    me.clientX = 10;
    me.clientY = 12;
    RoboCheckEqInt(WindowObserveMouse(a, &me), RoboNotFound);
    RoboCheckEqInt(WindowObserveMouse(b, &me), RoboOk);

    RoboCheck(WindowGetRenderSurface(a) == NULL);
    RoboCheckEqInt(WindowDestroy(b), RoboOk);
    P7End();
}

static void TestP7InputIntegration(void)
{
    WindowId id;
    KeyboardEvent ke;
    MouseEvent me;
    WindowEvent ev;

    P7Fixture();
    id = P7CreateWindow(200, 150, 0.0f);
    RoboCheckEqInt(WindowObserveFocus(id, 1), RoboOk);
    DrainEvents();

    /* Keyboard: press/release + modifiers */
    KeyboardEventInit(&ke);
    ke.action = RoboKeyActionDown;
    ke.physicalKey = RoboPhysicalKeyEnter;
    ke.logicalKey = RoboLogicalKeyEnter;
    ke.modifiers = RoboKeyModShift | RoboKeyModCtrl;
    RoboCheckEqInt(WindowObserveKey(id, &ke), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventKeyDown);
    RoboCheck(ev.windowId == id);

    ke.action = RoboKeyActionUp;
    RoboCheckEqInt(WindowObserveKey(id, &ke), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventKeyUp);

    /* Mouse move / button / wheel */
    MouseEventInit(&me);
    me.type = RoboMouseEventMove;
    me.clientX = 40;
    me.clientY = 50;
    RoboCheckEqInt(WindowObserveMouse(id, &me), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventMouseMove);
    RoboCheckEqInt(ev.mouse.clientX, 40);
    RoboCheckEqInt(ev.mouse.clientY, 50);

    me.type = RoboMouseEventButtonDown;
    me.button = RoboMouseButtonLeft;
    RoboCheckEqInt(WindowObserveMouse(id, &me), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventMouseButtonDown);

    me.type = RoboMouseEventWheel;
    me.scrollY = -120;
    RoboCheckEqInt(WindowObserveMouse(id, &me), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventMouseWheel);

    /* Resize then mouse coords still accepted */
    RoboCheckEqInt(WindowSetSize(id, 300, 200), RoboOk);
    me.type = RoboMouseEventMove;
    me.clientX = 290;
    me.clientY = 190;
    me.button = RoboMouseButtonNone;
    me.scrollY = 0;
    RoboCheckEqInt(WindowObserveMouse(id, &me), RoboOk);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheckEqInt(WindowObserveKey(id, &ke), RoboNotFound);
    RoboCheckEqInt(WindowObserveMouse(id, &me), RoboNotFound);
    P7End();
}

static void TestP7MultiWindow(void)
{
    WindowId a;
    WindowId b;
    WindowId c;
    GfxSurface *sa = NULL;
    GfxSurface *sb = NULL;
    GfxSurface *sc = NULL;

    P7Fixture();
    a = P7CreateWindow(80, 60, 0.0f);
    b = P7CreateWindow(100, 70, 0.0f);
    c = P7CreateWindow(120, 90, 0.0f);
    RoboCheck(a != b && b != c && a != c);
    RoboCheckEqInt((int)WindowLiveCount(), 3);

    RoboCheckEqInt(WindowBeginFrame(a, &sa), RoboOk);
    RoboCheckEqInt(WindowBeginFrame(b, &sb), RoboOk);
    RoboCheckEqInt(WindowBeginFrame(c, &sc), RoboOk);
    RoboCheck(sa != sb && sb != sc);
    RoboCheckEqInt((int)GfxSurfaceWidth(sa), 80);
    RoboCheckEqInt((int)GfxSurfaceWidth(sb), 100);
    RoboCheckEqInt((int)GfxSurfaceWidth(sc), 120);
    (void)GfxSurfaceClear(sa, (GfxColorRgba8){255, 0, 0, 255});
    (void)GfxSurfaceClear(sb, (GfxColorRgba8){0, 255, 0, 255});
    (void)GfxSurfaceClear(sc, (GfxColorRgba8){0, 0, 255, 255});
    RoboCheckEqInt(WindowEndFrame(a), RoboOk);
    RoboCheckEqInt(WindowEndFrame(b), RoboOk);
    RoboCheckEqInt(WindowEndFrame(c), RoboOk);
    RoboCheckEqInt(WindowPresentFrame(a), RoboOk);
    RoboCheckEqInt(WindowPresentFrame(b), RoboOk);
    RoboCheckEqInt(WindowPresentFrame(c), RoboOk);

    RoboCheckEqInt(WindowDestroy(b), RoboOk);
    RoboCheck(WindowIdIsLive(a));
    RoboCheck(WindowIdIsLive(c));
    RoboCheck(!WindowIdIsLive(b));

    RoboCheckEqInt(WindowDestroy(c), RoboOk);
    RoboCheckEqInt(WindowDestroy(a), RoboOk);
    RoboCheckEqInt((int)WindowLiveCount(), 0);
    P7End();
}

static void TestP7FailurePaths(void)
{
    WindowConfig cfg;
    WindowId id = RoboWindowIdNone;
    GfxSurface *surf = NULL;

    P7Fixture();
    gP7.failCreate = 1;
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    cfg.width = 64;
    cfg.height = 64;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboError);
    RoboCheckEqInt((int)id, (int)RoboWindowIdNone);
    RoboCheckEqInt((int)WindowLiveCount(), 0);
    gP7.failCreate = 0;

    id = P7CreateWindow(64, 48, 0.0f);
    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    RoboCheckEqInt(WindowEndFrame(id), RoboOk);
    gP7.failPresent = 1;
    RoboCheckEqInt(WindowPresentFrame(id), RoboIoError);
    RoboCheck(WindowIsDirty(id)); /* dirty retained on failure */
    gP7.failPresent = 0;
    RoboCheckEqInt(WindowPresentFrame(id), RoboOk);
    RoboCheck(!WindowIsDirty(id));

    /* Present while frame open */
    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    RoboCheckEqInt(WindowPresentFrame(id), RoboBusy);
    RoboCheckEqInt(WindowEndFrame(id), RoboOk);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    P7End();

    /* Shutdown with live windows */
    P7Fixture();
    id = P7CreateWindow(32, 32, 0.0f);
    RoboCheckEqInt((int)WindowLiveCount(), 1);
    P7End();
    RoboCheckEqInt((int)WindowLiveCount(), 0);
}

static void TestP7ShutdownMatrix(void)
{
    WindowId id;
    GfxSurface *surf = NULL;
    KeyboardEvent ke;

    /* Idle */
    P7Fixture();
    P7End();

    /* Active rendering */
    P7Fixture();
    id = P7CreateWindow(40, 30, 0.0f);
    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    P7End();

    /* After input */
    P7Fixture();
    id = P7CreateWindow(40, 30, 0.0f);
    RoboCheckEqInt(WindowObserveFocus(id, 1), RoboOk);
    KeyboardEventInit(&ke);
    ke.action = RoboKeyActionDown;
    ke.physicalKey = RoboPhysicalKeyEscape;
    ke.logicalKey = RoboLogicalKeyEscape;
    (void)WindowObserveKey(id, &ke);
    P7End();
    RoboCheck(!KeyboardIsPhysicalPressed(RoboPhysicalKeyEscape));

    /* After resize */
    P7Fixture();
    id = P7CreateWindow(40, 30, 0.0f);
    RoboCheckEqInt(WindowSetSize(id, 80, 60), RoboOk);
    P7End();
}

static void TestP7NullBackendContract(void)
{
    WindowConfig cfg;
    WindowId id;

    RoboCheckEqInt(WindowInit(), RoboOk);
    RoboCheckEqInt(WindowBackendUseNull(), RoboOk);
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    cfg.width = 32;
    cfg.height = 32;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    /* Null backend: present unsupported / no-op path */
    RoboCheckEqInt(WindowBeginFrame(id, NULL), RoboInvalidArgument);
    {
        GfxSurface *surf = NULL;
        RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
        RoboCheck(surf != NULL);
        RoboCheckEqInt(WindowEndFrame(id), RoboOk);
        /* present may be NOT_SUPPORTED on null */
        {
            Result r = WindowPresentFrame(id);
            RoboCheck(r == RoboOk || r == RoboNotSupported);
        }
    }
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

static void TestP7QemuSimContract(void)
{
    WindowConfig cfg;
    WindowId id;
    GfxSurface *surf = NULL;
    Result r;

    RoboCheckEqInt(WindowInit(), RoboOk);
    r = WindowBackendUseQemuSim(320, 200, 0u); /* RoboQemuPixBgrx */
    if (r != RoboOk) {
        WindowShutdown();
        RoboCheckMsg(0, "qemu_sim unavailable");
        return;
    }
    WindowConfigInit(&cfg);
    cfg.width = 320;
    cfg.height = 200;
    cfg.visible = 1;
    cfg.title = "p7-qemu-sim";
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    DrawPhase7Pattern(surf);
    ValidatePhase7Pattern(surf);
    RoboCheckEqInt(WindowEndFrame(id), RoboOk);
    RoboCheckEqInt(WindowPresentFrame(id), RoboOk);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

static void TestP7EventLoopWithFrame(void)
{
    WindowId id;
    GfxSurface *surf = NULL;
    WindowEvent ev;
    int sawResize = 0;

    P7Fixture();
    id = P7CreateWindow(100, 80, 0.0f);
    DrainEvents();

    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    (void)GfxSurfaceClear(surf, (GfxColorRgba8){1, 2, 3, 255});
    RoboCheckEqInt(WindowEndFrame(id), RoboOk);
    RoboCheckEqInt(WindowPresentFrame(id), RoboOk);

    RoboCheckEqInt(WindowObserveSize(id, 140, 100), RoboOk);
    RoboCheckEqInt(WindowProcessEvents(), RoboOk);
    while (WindowEventPoll(&ev) == RoboOk) {
        if (ev.type == RoboWindowEventResized && ev.windowId == id) {
            sawResize = 1;
            RoboCheckEqInt(ev.width, 140);
            RoboCheckEqInt(ev.height, 100);
        }
    }
    RoboCheck(sawResize);

    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    RoboCheckEqInt((int)GfxSurfaceWidth(surf), 140);
    RoboCheckEqInt(WindowEndFrame(id), RoboOk);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    P7End();
}

static void TestP7PerfSanity(void)
{
    WindowId id;
    GfxSurface *surf = NULL;
    clock_t t0;
    clock_t t1;
    int i;

    P7Fixture();
    id = P7CreateWindow(128, 96, 0.0f);
    t0 = clock();
    for (i = 0; i < 40; i++) {
        RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
        (void)GfxSurfaceFillRect(
            surf, MakeGfxRect(0, 0, 128, 96),
            (GfxColorRgba8){(uint8_t)i, 40, 80, 255});
        RoboCheckEqInt(WindowEndFrame(id), RoboOk);
        RoboCheckEqInt(WindowPresentFrame(id), RoboOk);
        if ((i % 5) == 0) {
            RoboCheckEqInt(WindowSetSize(id, 128 + (i % 3), 96),
                              RoboOk);
        }
    }
    t1 = clock();
    RoboCheck(t1 >= t0);
    RoboCheck((t1 - t0) < 20 * CLOCKS_PER_SEC);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    P7End();
}

static void TestP7HostAcceptance(void)
{
    WindowConfig cfg;
    WindowId id;
    GfxSurface *surf = NULL;
    Result r;
    int i;

    if (!WindowBackendHostAvailable()) {
        /* Pass when host GUI is unavailable (guest / CI without Win32). */
        return;
    }

    RoboCheckEqInt(WindowInit(), RoboOk);
    r = WindowBackendUseHost();
    if (r != RoboOk) {
        WindowShutdown();
        return;
    }

    WindowConfigInit(&cfg);
    cfg.width = 320;
    cfg.height = 240;
    cfg.visible = 1;
    cfg.title = "Roboticela Phase 7.15";
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);

    for (i = 0; i < 3; i++) {
        RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
        DrawPhase7Pattern(surf);
        ValidatePhase7Pattern(surf);
        RoboCheckEqInt(WindowEndFrame(id), RoboOk);
        RoboCheckEqInt(WindowPresentFrame(id), RoboOk);
        RoboCheckEqInt(WindowProcessEvents(), RoboOk);
    }

    RoboCheckEqInt(WindowSetSize(id, 400, 300), RoboOk);
    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    DrawPhase7Pattern(surf);
    RoboCheckEqInt(WindowEndFrame(id), RoboOk);
    RoboCheckEqInt(WindowPresentFrame(id), RoboOk);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

void TestRegisterWindowPhase7(void)
{
    TestRegister("window.p7.version", TestP7Version);
    TestRegister("window.p7.lifecycle", TestP7LifecycleMatrix);
    TestRegister("window.p7.graphics", TestP7GraphicsIntegration);
    TestRegister("window.p7.scale", TestP7ScalePhysical);
    TestRegister("window.p7.resize", TestP7ResizeSafety);
    TestRegister("window.p7.stale", TestP7StaleEvents);
    TestRegister("window.p7.input", TestP7InputIntegration);
    TestRegister("window.p7.multi", TestP7MultiWindow);
    TestRegister("window.p7.failures", TestP7FailurePaths);
    TestRegister("window.p7.shutdown", TestP7ShutdownMatrix);
    TestRegister("window.p7.null_contract", TestP7NullBackendContract);
    TestRegister("window.p7.qemu_sim", TestP7QemuSimContract);
    TestRegister("window.p7.loop_frame", TestP7EventLoopWithFrame);
    TestRegister("window.p7.perf", TestP7PerfSanity);
    TestRegister("window.p7.host", TestP7HostAcceptance);
}
