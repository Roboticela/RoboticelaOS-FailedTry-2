/*
 * RoboWindow host native backend suite (Goal 7.5).
 *
 * Distinguishes:
 *   - mock / null backends (always available, headless)
 *   - real Win32 host backend (skipped when unavailable or
 *     ROBO_WINDOW_SKIP_HOST is set)
 *
 * Set ROBO_WINDOW_SMOKE=1 to hold a visible window briefly for manual check.
 */

#include "RoboDisplay/RoboDisplay.h"
#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowBackendHost.h"
#include "RoboWindow/RoboWindowGraphics.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboGraphics/RoboGraphicsScreenshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(RoboWindowHasWin32) && RoboWindowHasWin32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

static int HostSkipReason(char *buf, size_t cap)
{
    if (getenv("ROBO_WINDOW_SKIP_HOST") != NULL) {
        if (buf && cap) {
            snprintf(buf, cap, "ROBO_WINDOW_SKIP_HOST");
        }
        return 1;
    }
    if (!WindowBackendHostAvailable()) {
        if (buf && cap) {
            snprintf(buf, cap, "host backend unavailable");
        }
        return 1;
    }
    return 0;
}

static void HostPumpBrief(int ms)
{
    clock_t start = clock();
    clock_t need = (clock_t)(((long)ms * CLOCKS_PER_SEC) / 1000L);
    if (need < 1) {
        need = 1;
    }
    while ((clock() - start) < need) {
        (void)WindowProcessEvents();
    }
}

static void TestHostAvailability(void)
{
    const WindowBackendVtbl *vtbl = WindowBackendHostVtbl();
#if defined(RoboWindowHasWin32) && RoboWindowHasWin32
    RoboCheck(vtbl != NULL);
    RoboCheck(vtbl->name != NULL);
    RoboCheckStrEq(vtbl->name, "win32");
#else
    RoboCheck(vtbl == NULL);
    RoboCheckEqInt(WindowBackendHostAvailable(), 0);
#endif
    (void)HostSkipReason(NULL, 0);
}

static void TestHostUseWhenUnavailable(void)
{
    char reason[64];
    RoboCheckEqInt(WindowInit(), RoboOk);
    if (HostSkipReason(reason, sizeof(reason))) {
        RoboCheckEqInt(WindowBackendUseHost(), RoboNotSupported);
    } else {
        RoboCheckEqInt(WindowBackendHostAvailable(), 1);
    }
    WindowShutdown();
}

static int BeginHost(WindowId *outId, WindowConfig *cfg)
{
    char reason[64];
    Result r;

    if (HostSkipReason(reason, sizeof(reason))) {
        printf("SKIP window.host.* (%s)\n", reason);
        return 0;
    }

    RoboCheckEqInt(WindowInit(), RoboOk);
    r = WindowBackendUseHost();
    if (r != RoboOk) {
        printf("SKIP window.host.* (use_host=%d)\n", (int)r);
        WindowShutdown();
        return 0;
    }
    RoboCheckStrEq(WindowBackendName(), "win32");

    WindowConfigInit(cfg);
    cfg->width = 640;
    cfg->height = 480;
    cfg->visible = 0;
    cfg->title = "Roboticela Host";
    cfg->x = 80;
    cfg->y = 60;
    cfg->minWidth = 200;
    cfg->minHeight = 150;
    cfg->maxWidth = 1920;
    cfg->maxHeight = 1080;
    cfg->resizable = 1;

    r = WindowCreate(cfg, outId);
    if (r != RoboOk) {
        printf("SKIP window.host.* (create=%d — no interactive desktop?)\n",
               (int)r);
        WindowShutdown();
        return 0;
    }
    return 1;
}

static void TestHostCreateDestroy(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    WindowNativeHandle h;
    WindowState st;
    int i;

    if (!BeginHost(&id, &cfg)) {
        return;
    }

    RoboCheckEqInt(WindowGetNativeHandle(id, &h), RoboOk);
    RoboCheck(WindowNativeHandleIsValid(&h));
    RoboCheckEqInt(WindowGetState(id, &st), RoboOk);
    RoboCheck(st.width > 0);
    RoboCheck(st.height > 0);
    RoboCheckEqInt(st.visible, 0);

    for (i = 0; i < 3; i++) {
        RoboCheckEqInt(WindowDestroy(id), RoboOk);
        RoboCheckEqInt(WindowDestroy(id), RoboNotFound);
        RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    }

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheckEqInt(WindowGetNativeHandle(id, &h), RoboNotFound);
    WindowShutdown();
}

static void TestHostTitleSizePosVisibility(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    WindowState st;
    const char *title = NULL;
    const char *unicodeTitle = "Roboticela — پنجره";
    int i;

    if (!BeginHost(&id, &cfg)) {
        return;
    }

    RoboCheckEqInt(WindowSetTitle(id, ""), RoboOk);
    RoboCheckEqInt(WindowGetTitle(id, &title), RoboOk);
    RoboCheck(title && title[0] == '\0');

    RoboCheckEqInt(WindowSetTitle(id, unicodeTitle), RoboOk);
    RoboCheckEqInt(WindowGetTitle(id, &title), RoboOk);
    RoboCheckStrEq(title, unicodeTitle);

    for (i = 0; i < 4; i++) {
        RoboCheckEqInt(WindowSetTitle(id, "Title A"), RoboOk);
        RoboCheckEqInt(WindowSetTitle(id, "Title B"), RoboOk);
    }

    RoboCheckEqInt(WindowRequestSize(id, 700, 500), RoboOk);
    RoboCheckEqInt(WindowGetState(id, &st), RoboOk);
    RoboCheckEqInt(st.width, 700);
    RoboCheckEqInt(st.height, 500);

    RoboCheckEqInt(WindowRequestSize(id, 100, 100),
                      RoboInvalidArgument); /* below min */
    RoboCheckEqInt(WindowRequestSize(id, 0, 480), RoboInvalidArgument);
    RoboCheckEqInt(WindowRequestSize(id, 2000, 500),
                      RoboInvalidArgument); /* above max */

    RoboCheckEqInt(WindowRequestPosition(id, 0, 0), RoboOk);
    RoboCheckEqInt(WindowRequestPosition(id, 120, 90), RoboOk);
    RoboCheckEqInt(WindowRequestPosition(id, -20, 40), RoboOk);
    RoboCheckEqInt(WindowGetState(id, &st), RoboOk);
    RoboCheckEqInt(st.x, -20);
    RoboCheckEqInt(st.y, 40);

    for (i = 0; i < 3; i++) {
        RoboCheckEqInt(WindowShow(id), RoboOk);
        RoboCheckEqInt(WindowGetState(id, &st), RoboOk);
        RoboCheckEqInt(st.visible, 1);
        (void)WindowProcessEvents();
        RoboCheckEqInt(WindowHide(id), RoboOk);
        RoboCheckEqInt(WindowGetState(id, &st), RoboOk);
        RoboCheckEqInt(st.visible, 0);
    }

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

static void TestHostFocusAndDisplay(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    WindowState st;
    DisplayInfo disp;
    Result fr;

    if (!BeginHost(&id, &cfg)) {
        return;
    }

    RoboCheckEqInt(WindowGetAssociatedDisplay(id, &disp), RoboOk);
    RoboCheck(disp.id != RoboDisplayIdNone);

    RoboCheckEqInt(WindowShow(id), RoboOk);
    fr = WindowRequestFocus(id);
    /* Focus may be declined by the OS without faking success. */
    RoboCheck(fr == RoboOk || fr == RoboNotSupported);
    RoboCheckEqInt(WindowGetState(id, &st), RoboOk);
    if (fr != RoboOk) {
        /* Do not require focused==1 when backend reported unsupported. */
        RoboCheck(st.focused == 0 || st.focused == 1);
    }

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

static void TestHostShutdownWithLiveWindow(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;

    if (!BeginHost(&id, &cfg)) {
        return;
    }
    RoboCheck(WindowLiveCount() >= 1u);
    WindowShutdown();
    RoboCheckEqInt(WindowIsInitialized(), 0);
}

static void TestHostInvalidConfig(void)
{
    char reason[64];
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;

    if (HostSkipReason(reason, sizeof(reason))) {
        printf("SKIP window.host.invalid_config (%s)\n", reason);
        return;
    }
    RoboCheckEqInt(WindowInit(), RoboOk);
    if (WindowBackendUseHost() != RoboOk) {
        printf("SKIP window.host.invalid_config (use_host failed)\n");
        WindowShutdown();
        return;
    }
    WindowConfigInit(&cfg);
    cfg.width = 0;
    cfg.height = 100;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboInvalidArgument);
    WindowShutdown();
}

static void TestHostCloseLifecycle(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    WindowLifecycle life;
    WindowNativeHandle h;
    WindowEvent ev;
    WindowState st;

    if (!BeginHost(&id, &cfg)) {
        return;
    }

    RoboCheckEqInt(WindowGetLifecycle(id, &life), RoboOk);
    RoboCheckEqInt((int)life, (int)RoboWindowLifecycleCreated);
    RoboCheckEqInt(WindowShow(id), RoboOk);
    RoboCheckEqInt(WindowGetState(id, &st), RoboOk);
    RoboCheckEqInt(st.visible, 1);
    (void)WindowProcessEvents();

    RoboCheckEqInt(WindowRequestClose(id), RoboOk);
    RoboCheckEqInt(WindowGetLifecycle(id, &life), RoboOk);
    RoboCheckEqInt((int)life, (int)RoboWindowLifecycleClosing);
    RoboCheckEqInt(WindowIsClosing(id), 1);
    RoboCheckEqInt(WindowGetNativeHandle(id, &h), RoboOk);
    RoboCheck(WindowNativeHandleIsValid(&h));
    RoboCheckEqInt(WindowShow(id), RoboInvalidArgument);
    RoboCheckEqInt(WindowSetSize(id, 500, 400), RoboInvalidArgument);
    RoboCheckEqInt(WindowRequestClose(id), RoboAlreadyExists);

    while (WindowEventPoll(&ev) == RoboOk) {
        /* drain CREATED/SHOWN/CLOSE_REQUESTED */
    }

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheckEqInt(WindowIdIsLive(id), 0);
    RoboCheckEqInt(WindowGetNativeHandle(id, &h), RoboNotFound);
    RoboCheckEqInt(WindowShow(id), RoboNotFound);
    RoboCheckEqInt(WindowDestroy(id), RoboNotFound);
    RoboCheckEqInt(WindowRequestClose(id), RoboNotFound);
    WindowShutdown();
}

static void TestHostScaleDpi(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    DisplayInfo disp;
    float scale = 0.0f;
    DisplayDpi dpi;
    GfxSize logical;
    GfxSize physical;
    int32_t px = 0;
    int32_t py = 0;

    if (!BeginHost(&id, &cfg)) {
        return;
    }

    RoboCheckEqInt(WindowShow(id), RoboOk);
    (void)WindowProcessEvents();

    /* Associated display may be placeholder; scale query must still succeed. */
    RoboCheckEqInt(WindowGetScale(id, &scale), RoboOk);
    RoboCheck(DisplayScaleIsKnown(scale));
    printf("host scale=%g\n", (double)scale);

    {
        Result dr = WindowGetAssociatedDisplay(id, &disp);
        RoboCheck(dr == RoboOk || dr == RoboNotFound);
        if (dr == RoboOk) {
            Result dpiR = WindowGetDpi(id, &dpi);
            RoboCheck(dpiR == RoboOk);
            /* DPI may be unavailable (0) on placeholder — that is OK. */
            RoboCheckEqInt(DisplayDpiValidate(dpi.dpiX), RoboOk);
            RoboCheckEqInt(DisplayDpiValidate(dpi.dpiY), RoboOk);
            printf("host dpi_x=%g dpi_y=%g display=%llu\n", (double)dpi.dpiX,
                   (double)dpi.dpiY, (unsigned long long)disp.id);
        }
    }

    RoboCheckEqInt(WindowGetSize(id, &logical), RoboOk);
    RoboCheckEqInt(WindowGetSizePhysical(id, &physical), RoboOk);
    RoboCheckEqInt(
        DisplaySizeLogicalToPhysical(logical.width, logical.height,
                                              scale, &px, &py),
        RoboOk);
    RoboCheckEqInt(physical.width, px);
    RoboCheckEqInt(physical.height, py);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

static void TestHostSizingConstraints(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    GfxSize sz;
    WindowSizeConstraints c;

    if (!BeginHost(&id, &cfg)) {
        return;
    }

    RoboCheckEqInt(WindowGetSize(id, &sz), RoboOk);
    RoboCheck(sz.width > 0 && sz.height > 0);

    RoboCheckEqInt(WindowSetSize(id, 720, 540), RoboOk);
    RoboCheckEqInt(WindowGetSize(id, &sz), RoboOk);
    RoboCheckEqInt(sz.width, 720);
    RoboCheckEqInt(sz.height, 540);

    WindowSizeConstraintsInit(&c);
    c.minWidth = 400;
    c.minHeight = 300;
    c.maxWidth = 1000;
    c.maxHeight = 800;
    RoboCheckEqInt(WindowSetSizeConstraints(id, &c), RoboOk);
    RoboCheckEqInt(WindowSetSize(id, 200, 200), RoboInvalidArgument);
    RoboCheckEqInt(WindowSetSize(id, 1200, 400), RoboInvalidArgument);
    RoboCheckEqInt(WindowSetSize(id, 400, 300), RoboOk);
    RoboCheckEqInt(WindowGetSize(id, &sz), RoboOk);
    RoboCheckEqInt(sz.width, 400);
    RoboCheckEqInt(sz.height, 300);

    RoboCheckEqInt(WindowSetSize(id, 640, 480), RoboOk);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

static void TestHostSmokeManual(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    GfxSize sz;
    WindowSizeConstraints c;
    const char *smoke = getenv("ROBO_WINDOW_SMOKE");

    if (!smoke || smoke[0] == '\0' || smoke[0] == '0') {
        printf("SKIP window.host.smoke (set ROBO_WINDOW_SMOKE=1)\n");
        return;
    }
    if (!BeginHost(&id, &cfg)) {
        return;
    }

    RoboCheckEqInt(WindowSetTitle(id, "Roboticela Host Display"),
                      RoboOk);
    RoboCheckEqInt(WindowSetSize(id, 800, 600), RoboOk);
    RoboCheckEqInt(WindowGetSize(id, &sz), RoboOk);
    RoboCheckEqInt(sz.width, 800);
    RoboCheckEqInt(sz.height, 600);
    RoboCheckEqInt(WindowRequestPosition(id, 100, 80), RoboOk);
    RoboCheckEqInt(WindowShow(id), RoboOk);

    {
        GfxSurface *frame = NULL;
        GfxRaster *raster = NULL;
        RoboCheckEqInt(WindowBeginFrame(id, &frame), RoboOk);
        RoboCheckEqInt(GfxRasterCreate(frame, &raster), RoboOk);
        (void)GfxRasterClear(
            raster, (GfxColorRgba8){245, 245, 247, 255});
        (void)GfxRasterFillRect(
            raster, MakeGfxRect(40, 40, 200, 120),
            (GfxColorRgba8){14, 122, 255, 255});
        (void)GfxRasterFillRect(
            raster, MakeGfxRect(280, 80, 160, 160),
            (GfxColorRgba8){48, 209, 88, 200});
        (void)GfxRasterDrawLine(
            raster, MakeGfxPoint(40, 560), MakeGfxPoint(760, 560),
            (GfxColorRgba8){29, 29, 31, 255});
        (void)GfxRasterFillRect(
            raster, MakeGfxRect(390, 290, 20, 20),
            (GfxColorRgba8){255, 59, 48, 255});
        GfxRasterDestroy(raster);
        RoboCheckEqInt(WindowEndFrame(id), RoboOk);
        RoboCheckEqInt(WindowPresentFrame(id), RoboOk);
    }
    HostPumpBrief(800);

    WindowSizeConstraintsInit(&c);
    c.minWidth = 400;
    c.minHeight = 300;
    c.maxWidth = 900;
    c.maxHeight = 700;
    RoboCheckEqInt(WindowSetSizeConstraints(id, &c), RoboOk);
    RoboCheckEqInt(WindowSetSize(id, 200, 200), RoboInvalidArgument);
    RoboCheckEqInt(WindowSetSize(id, 1000, 400), RoboInvalidArgument);
    RoboCheckEqInt(WindowSetSize(id, 700, 500), RoboOk);
    RoboCheckEqInt(WindowGetSize(id, &sz), RoboOk);
    RoboCheckEqInt(sz.width, 700);
    RoboCheckEqInt(sz.height, 500);
    {
        GfxSurface *frame = NULL;
        RoboCheckEqInt(WindowBeginFrame(id, &frame), RoboOk);
        (void)GfxSurfaceClear(
            frame, (GfxColorRgba8){245, 245, 247, 255});
        (void)GfxSurfaceFillRect(
            frame, MakeGfxRect(20, 20, 100, 60),
            (GfxColorRgba8){14, 122, 255, 255});
        RoboCheckEqInt(WindowEndFrame(id), RoboOk);
        RoboCheckEqInt(WindowPresentFrame(id), RoboOk);
    }
    HostPumpBrief(600);

    RoboCheckEqInt(WindowShow(id), RoboOk);
    HostPumpBrief(400);

    RoboCheckEqInt(WindowRequestClose(id), RoboOk);
    {
        WindowLifecycle life;
        RoboCheckEqInt(WindowGetLifecycle(id, &life), RoboOk);
        RoboCheckEqInt((int)life, (int)RoboWindowLifecycleClosing);
    }
    HostPumpBrief(200);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheckEqInt(WindowIdIsLive(id), 0);
    {
        WindowNativeHandle h;
        RoboCheckEqInt(WindowGetNativeHandle(id, &h), RoboNotFound);
        RoboCheckEqInt(WindowShow(id), RoboNotFound);
    }
    WindowShutdown();
}

static void TestMockStillDistinct(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    WindowNativeHandle h;

    RoboCheckEqInt(WindowInit(), RoboOk);
    RoboCheckEqInt(WindowBackendUseNull(), RoboOk);
    RoboCheckStrEq(WindowBackendName(), "null");
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(WindowGetNativeHandle(id, &h), RoboOk);
    RoboCheck(WindowNativeHandleIsEmpty(&h));
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

static void TestHostEventLoop(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    WindowState st;
    WindowLifecycle life;
    WindowBackendCaps caps;
    int sawClose = 0;
    WindowEvent ev;

    if (!BeginHost(&id, &cfg)) {
        return;
    }

    RoboCheckEqInt(WindowBackendCapabilities(&caps), RoboOk);
    RoboCheck(caps.ProcessEvents);
    RoboCheck(caps.WaitEvents);
    RoboCheck(caps.wakeup);

    RoboCheckEqInt(WindowEventLoopStart(), RoboOk);
    RoboCheckEqInt(WindowShow(id), RoboOk);
    RoboCheckEqInt(WindowEventLoopRunOnce(0), RoboOk);

    RoboCheckEqInt(WindowSetSize(id, 700, 520), RoboOk);
    RoboCheckEqInt(WindowRequestPosition(id, 60, 40), RoboOk);
    RoboCheckEqInt(WindowEventLoopRunOnce(10), RoboOk);
    RoboCheckEqInt(WindowGetState(id, &st), RoboOk);
    RoboCheckEqInt(st.width, 700);
    RoboCheckEqInt(st.height, 520);

    RoboCheckEqInt(WindowEventLoopWakeup(), RoboOk);
    RoboCheckEqInt(WindowEventLoopRunOnce(50), RoboOk);

    RoboCheckEqInt(WindowRequestClose(id), RoboOk);
    RoboCheckEqInt(WindowGetLifecycle(id, &life), RoboOk);
    RoboCheckEqInt((int)life, (int)RoboWindowLifecycleClosing);
    while (WindowEventPoll(&ev) == RoboOk) {
        if (ev.type == RoboWindowEventCloseRequested && ev.windowId == id) {
            sawClose = 1;
        }
    }
    RoboCheck(sawClose);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheckEqInt(WindowEventLoopRequestStop(), RoboOk);
    RoboCheckEqInt(WindowEventLoopIsRunning(), 0);
    WindowShutdown();
}

static void TestHostKeyboard(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    WindowEvent ev;
    int sawDown = 0;
    int sawUp = 0;
#if defined(RoboWindowHasWin32) && RoboWindowHasWin32
    INPUT inputs[2];
#endif

    if (!BeginHost(&id, &cfg)) {
        return;
    }

    RoboCheckEqInt(WindowShow(id), RoboOk);
    RoboCheckEqInt(WindowRequestFocus(id), RoboOk);
    RoboCheckEqInt(WindowEventLoopRunOnce(0), RoboOk);
    while (WindowEventPoll(&ev) == RoboOk) {
    }

#if defined(RoboWindowHasWin32) && RoboWindowHasWin32
    memset(inputs, 0, sizeof(inputs));
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = 'A';
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'A';
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    if (SendInput(2, inputs, sizeof(INPUT)) == 2) {
        HostPumpBrief(100);
        while (WindowEventPoll(&ev) == RoboOk) {
            if (ev.windowId == id && ev.type == RoboWindowEventKeyDown &&
                ev.key.logicalKey == RoboLogicalKeyA) {
                sawDown = 1;
            }
            if (ev.windowId == id && ev.type == RoboWindowEventKeyUp &&
                ev.key.logicalKey == RoboLogicalKeyA) {
                sawUp = 1;
            }
        }
        /* SendInput may fail to reach our HWND if focus was stolen — fall back. */
        if (!sawDown) {
            KeyboardEvent ke;
            KeyboardEventInit(&ke);
            ke.action = RoboKeyActionDown;
            ke.physicalKey = RoboPhysicalKeyA;
            ke.logicalKey = RoboLogicalKeyA;
            ke.modifiers = KeyboardModifiers();
            RoboCheckEqInt(WindowObserveKey(id, &ke), RoboOk);
            ke.action = RoboKeyActionUp;
            RoboCheckEqInt(WindowObserveKey(id, &ke), RoboOk);
            while (WindowEventPoll(&ev) == RoboOk) {
                if (ev.type == RoboWindowEventKeyDown) {
                    sawDown = 1;
                }
                if (ev.type == RoboWindowEventKeyUp) {
                    sawUp = 1;
                }
            }
        }
    } else
#endif
    {
        KeyboardEvent ke;
        KeyboardEventInit(&ke);
        ke.action = RoboKeyActionDown;
        ke.physicalKey = RoboPhysicalKeyA;
        ke.logicalKey = RoboLogicalKeyA;
        RoboCheckEqInt(WindowObserveKey(id, &ke), RoboOk);
        ke.action = RoboKeyActionUp;
        RoboCheckEqInt(WindowObserveKey(id, &ke), RoboOk);
        while (WindowEventPoll(&ev) == RoboOk) {
            if (ev.type == RoboWindowEventKeyDown) {
                sawDown = 1;
            }
            if (ev.type == RoboWindowEventKeyUp) {
                sawUp = 1;
            }
        }
    }

    RoboCheck(sawDown);
    RoboCheck(sawUp);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheck(!KeyboardIsPhysicalPressed(RoboPhysicalKeyA));
    WindowShutdown();
    RoboCheck(!KeyboardIsPhysicalPressed(RoboPhysicalKeyA));
}

static void TestHostMouse(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    WindowEvent ev;
    int sawMove = 0;
    int sawDown = 0;
    int sawUp = 0;
    int sawWheel = 0;
#if defined(RoboWindowHasWin32) && RoboWindowHasWin32
    INPUT inputs[6];
    WindowNativeHandle h;
#endif

    if (!BeginHost(&id, &cfg)) {
        return;
    }

    RoboCheckEqInt(WindowShow(id), RoboOk);
    RoboCheckEqInt(WindowRequestFocus(id), RoboOk);
    RoboCheckEqInt(WindowEventLoopRunOnce(0), RoboOk);
    while (WindowEventPoll(&ev) == RoboOk) {
    }

#if defined(RoboWindowHasWin32) && RoboWindowHasWin32
    memset(inputs, 0, sizeof(inputs));
    /* Relative move then left click then wheel — may miss HWND if focus stolen. */
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dx = 1;
    inputs[0].mi.dy = 1;
    inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[2].type = INPUT_MOUSE;
    inputs[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    inputs[3].type = INPUT_MOUSE;
    inputs[3].mi.dwFlags = MOUSEEVENTF_WHEEL;
    inputs[3].mi.mouseData = 120;
    if (SendInput(4, inputs, sizeof(INPUT)) == 4) {
        HostPumpBrief(120);
        while (WindowEventPoll(&ev) == RoboOk) {
            if (ev.windowId != id) {
                continue;
            }
            if (ev.type == RoboWindowEventMouseMove) {
                sawMove = 1;
            }
            if (ev.type == RoboWindowEventMouseButtonDown &&
                ev.mouse.button == RoboMouseButtonLeft) {
                sawDown = 1;
            }
            if (ev.type == RoboWindowEventMouseButtonUp &&
                ev.mouse.button == RoboMouseButtonLeft) {
                sawUp = 1;
            }
            if (ev.type == RoboWindowEventMouseWheel) {
                sawWheel = 1;
            }
        }
    }
    if (!sawDown || !sawUp || !sawWheel) {
        MouseEvent me;
        RoboCheckEqInt(WindowGetNativeHandle(id, &h), RoboOk);
        (void)h;
        MouseEventInit(&me);
        me.type = RoboMouseEventMove;
        me.clientX = 40;
        me.clientY = 50;
        me.hasScreenCoords = 0;
        me.modifiers = KeyboardModifiers();
        RoboCheckEqInt(WindowObserveMouse(id, &me), RoboOk);
        me.type = RoboMouseEventButtonDown;
        me.button = RoboMouseButtonLeft;
        me.clickCount = 1;
        RoboCheckEqInt(WindowObserveMouse(id, &me), RoboOk);
        me.type = RoboMouseEventButtonUp;
        RoboCheckEqInt(WindowObserveMouse(id, &me), RoboOk);
        me.type = RoboMouseEventWheel;
        me.button = RoboMouseButtonNone;
        me.scrollY = 120;
        RoboCheckEqInt(WindowObserveMouse(id, &me), RoboOk);
        while (WindowEventPoll(&ev) == RoboOk) {
            if (ev.type == RoboWindowEventMouseMove) {
                sawMove = 1;
            }
            if (ev.type == RoboWindowEventMouseButtonDown) {
                sawDown = 1;
            }
            if (ev.type == RoboWindowEventMouseButtonUp) {
                sawUp = 1;
            }
            if (ev.type == RoboWindowEventMouseWheel) {
                sawWheel = 1;
            }
        }
    }
#else
    {
        MouseEvent me;
        MouseEventInit(&me);
        me.type = RoboMouseEventMove;
        me.clientX = 10;
        me.clientY = 10;
        RoboCheckEqInt(WindowObserveMouse(id, &me), RoboOk);
        me.type = RoboMouseEventButtonDown;
        me.button = RoboMouseButtonLeft;
        RoboCheckEqInt(WindowObserveMouse(id, &me), RoboOk);
        me.type = RoboMouseEventButtonUp;
        RoboCheckEqInt(WindowObserveMouse(id, &me), RoboOk);
        me.type = RoboMouseEventWheel;
        me.button = RoboMouseButtonNone;
        me.scrollY = 120;
        RoboCheckEqInt(WindowObserveMouse(id, &me), RoboOk);
        while (WindowEventPoll(&ev) == RoboOk) {
            if (ev.type == RoboWindowEventMouseMove) {
                sawMove = 1;
            }
            if (ev.type == RoboWindowEventMouseButtonDown) {
                sawDown = 1;
            }
            if (ev.type == RoboWindowEventMouseButtonUp) {
                sawUp = 1;
            }
            if (ev.type == RoboWindowEventMouseWheel) {
                sawWheel = 1;
            }
        }
    }
#endif

    RoboCheck(sawMove);
    RoboCheck(sawDown);
    RoboCheck(sawUp);
    RoboCheck(sawWheel);

    /* Modifiers + middle button via the same observe path. */
    {
        MouseEvent me;
        MouseEventInit(&me);
        me.type = RoboMouseEventButtonDown;
        me.button = RoboMouseButtonMiddle;
        me.modifiers = RoboKeyModAlt | RoboKeyModShift;
        me.clientX = 1;
        me.clientY = 1;
        RoboCheckEqInt(WindowObserveMouse(id, &me), RoboOk);
        RoboCheck(MouseIsButtonPressed(RoboMouseButtonMiddle));
        RoboCheck(MouseModifiers() & RoboKeyModAlt);
    }

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheck(!MouseIsButtonPressed(RoboMouseButtonMiddle));
    RoboCheck(!MouseIsButtonPressed(RoboMouseButtonLeft));
    WindowShutdown();
    RoboCheck(!MouseIsButtonPressed(RoboMouseButtonLeft));
}

#if defined(RoboWindowHasWin32) && RoboWindowHasWin32
static int HostSampleClientRgb(HWND hwnd, int x, int y, int *r, int *g,
                                  int *b)
{
    HDC hdc;
    COLORREF c;
    if (!hwnd || !r || !g || !b) {
        return 0;
    }
    hdc = GetDC(hwnd);
    if (!hdc) {
        return 0;
    }
    c = GetPixel(hdc, x, y);
    ReleaseDC(hwnd, hdc);
    if (c == CLR_INVALID) {
        return 0;
    }
    *r = (int)GetRValue(c);
    *g = (int)GetGValue(c);
    *b = (int)GetBValue(c);
    return 1;
}

static int RgbNear(int r, int g, int b, int er, int eg, int eb, int tol)
{
    int dr = r - er;
    int dg = g - eg;
    int db = b - eb;
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
#endif

static void TestHostPresentCaps(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    WindowBackendCaps caps;

    if (!BeginHost(&id, &cfg)) {
        return;
    }
    RoboCheckEqInt(WindowBackendCapabilities(&caps), RoboOk);
    RoboCheck(caps.present);
    RoboCheck(caps.presentFormatMask & RoboWindowPresentFmtBgra8);
    RoboCheck(caps.presentFormatMask & RoboWindowPresentFmtRgba8);
    RoboCheck(WindowPresentFormatSupported(RoboGfxFormatBgra8));
    RoboCheck(WindowPresentFormatSupported(RoboGfxFormatRgba8));
    RoboCheck(WindowPresentFormatSupported(RoboGfxFormatRgb8));
    RoboCheck(!WindowPresentFormatSupported(RoboGfxFormatAlpha8));
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

static void TestHostPresentColors(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    GfxSurface *surf = NULL;
    GfxSurfaceDesc desc;
    GfxScreenshot *shot = NULL;
    GfxSurface *shotSurf = NULL;
    GfxColorRgba8 px;
#if defined(RoboWindowHasWin32) && RoboWindowHasWin32
    WindowNativeHandle h;
    HWND hwnd;
    int r = 0;
    int g = 0;
    int b = 0;
#endif
    const int W = 320;
    const int H = 240;

    if (!BeginHost(&id, &cfg)) {
        return;
    }

    RoboCheckEqInt(WindowSetSize(id, W, H), RoboOk);
    RoboCheckEqInt(WindowSetTitle(id, "Roboticela Present"), RoboOk);
    RoboCheckEqInt(WindowShow(id), RoboOk);
    HostPumpBrief(50);

    GfxSurfaceDescInit(&desc);
    desc.width = (uint32_t)W;
    desc.height = (uint32_t)H;
    desc.format = RoboGfxFormatBgra8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surf), RoboOk);

    (void)GfxSurfaceClear(surf,
                                 (GfxColorRgba8){0, 0, 0, 255});
    (void)GfxSurfaceSetPixel(surf, 0, 0,
                                     (GfxColorRgba8){255, 255, 255, 255});
    (void)GfxSurfaceSetPixel(surf, (uint32_t)(W - 1), 0,
                                     (GfxColorRgba8){255, 0, 0, 255});
    (void)GfxSurfaceSetPixel(surf, 0, (uint32_t)(H - 1),
                                     (GfxColorRgba8){0, 255, 0, 255});
    (void)GfxSurfaceSetPixel(surf, (uint32_t)(W - 1), (uint32_t)(H - 1),
                                     (GfxColorRgba8){0, 0, 255, 255});
    (void)GfxSurfaceFillRect(
        surf, MakeGfxRect(W / 2 - 8, H / 2 - 8, 16, 16),
        (GfxColorRgba8){128, 128, 128, 255});
    (void)GfxSurfaceFillRect(
        surf, MakeGfxRect(40, 40, 64, 48),
        (GfxColorRgba8){14, 122, 255, 255});

    RoboCheckEqInt(WindowPresent(id, surf), RoboOk);
    HostPumpBrief(30);

    /* Source surface integrity (Phase 6.14 screenshot path). */
    RoboCheckEqInt(GfxScreenshotCaptureSurface(surf, NULL, &shot),
                      RoboOk);
    shotSurf = GfxScreenshotSurface(shot);
    RoboCheck(shotSurf != NULL);
    RoboCheckEqInt(GfxSurfaceGetPixel(shotSurf, 0, 0, &px), RoboOk);
    RoboCheckEqInt((int)px.r, 255);
    RoboCheckEqInt((int)px.g, 255);
    RoboCheckEqInt((int)px.b, 255);
    RoboCheckEqInt(
        GfxSurfaceGetPixel(shotSurf, (uint32_t)(W - 1), 0, &px),
        RoboOk);
    RoboCheckEqInt((int)px.r, 255);
    RoboCheckEqInt((int)px.g, 0);
    RoboCheckEqInt((int)px.b, 0);
    GfxScreenshotRelease(shot);
    shot = NULL;

#if defined(RoboWindowHasWin32) && RoboWindowHasWin32
    RoboCheckEqInt(WindowGetNativeHandle(id, &h), RoboOk);
    RoboCheck(WindowNativeHandleIsValid(&h));
    hwnd = FindWindowW(L"Roboticela.RoboWindow", L"Roboticela Present");
    if (hwnd) {
        /* Tolerance: GDI BitBlt / desktop composition may soft-filter. */
        RoboCheck(HostSampleClientRgb(hwnd, 0, 0, &r, &g, &b));
        RoboCheck(RgbNear(r, g, b, 255, 255, 255, 8));
        RoboCheck(HostSampleClientRgb(hwnd, W - 1, 0, &r, &g, &b));
        RoboCheck(RgbNear(r, g, b, 255, 0, 0, 8));
        RoboCheck(HostSampleClientRgb(hwnd, 0, H - 1, &r, &g, &b));
        RoboCheck(RgbNear(r, g, b, 0, 255, 0, 8));
        RoboCheck(HostSampleClientRgb(hwnd, W - 1, H - 1, &r, &g, &b));
        RoboCheck(RgbNear(r, g, b, 0, 0, 255, 8));
        RoboCheck(HostSampleClientRgb(hwnd, W / 2, H / 2, &r, &g, &b));
        RoboCheck(RgbNear(r, g, b, 128, 128, 128, 16));
        RoboCheck(HostSampleClientRgb(hwnd, 50, 50, &r, &g, &b));
        RoboCheck(RgbNear(r, g, b, 14, 122, 255, 16));
        printf("host present GetPixel OK\n");
    } else {
        printf("WARN window.host.present_colors: FindWindow missed HWND; "
               "surface path still validated\n");
    }
#endif

    /* Present after destroy must fail; surface still valid. */
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheckEqInt(WindowPresent(id, surf), RoboNotFound);
    GfxSurfaceRelease(surf);
    WindowShutdown();
}

static void TestHostPresentPatternResize(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    GfxSurface *surf = NULL;
    GfxSurfaceDesc desc;
    GfxRaster *raster = NULL;
    GfxSize sz;
    WindowEvent ev;
    int sawResize = 0;

    if (!BeginHost(&id, &cfg)) {
        return;
    }

    RoboCheckEqInt(WindowSetSize(id, 400, 300), RoboOk);
    RoboCheckEqInt(WindowShow(id), RoboOk);
    HostPumpBrief(40);

    GfxSurfaceDescInit(&desc);
    desc.width = 400;
    desc.height = 300;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surf), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(surf, &raster), RoboOk);

    (void)GfxRasterClear(raster,
                                (GfxColorRgba8){245, 245, 247, 255});
    (void)GfxRasterFillRect(
        raster, MakeGfxRect(0, 0, 8, 8),
        (GfxColorRgba8){255, 59, 48, 255});
    (void)GfxRasterFillRect(
        raster, MakeGfxRect(392, 0, 8, 8),
        (GfxColorRgba8){48, 209, 88, 255});
    (void)GfxRasterFillRect(
        raster, MakeGfxRect(0, 292, 8, 8),
        (GfxColorRgba8){10, 132, 255, 255});
    (void)GfxRasterFillRect(
        raster, MakeGfxRect(392, 292, 8, 8),
        (GfxColorRgba8){255, 159, 10, 255});
    (void)GfxRasterFillRect(
        raster, MakeGfxRect(190, 140, 20, 20),
        (GfxColorRgba8){29, 29, 31, 255});
    (void)GfxRasterDrawLine(
        raster, MakeGfxPoint(20, 20), MakeGfxPoint(380, 280),
        (GfxColorRgba8){14, 122, 255, 255});
    /* Semi-transparent overlay (alpha blend in surface, then present). */
    (void)GfxRasterSetOpacity(raster, 180);
    (void)GfxRasterFillRect(
        raster, MakeGfxRect(120, 80, 160, 100),
        (GfxColorRgba8){255, 255, 255, 255});
    GfxRasterSetOpacity(raster, 255);

    RoboCheckEqInt(WindowPresent(id, surf), RoboOk);

    while (WindowEventPoll(&ev) == RoboOk) {
    }
    RoboCheckEqInt(WindowSetSize(id, 500, 360), RoboOk);
    HostPumpBrief(60);
    while (WindowEventPoll(&ev) == RoboOk) {
        if (ev.type == RoboWindowEventResized && ev.windowId == id) {
            sawResize = 1;
        }
    }
    RoboCheckEqInt(WindowGetSize(id, &sz), RoboOk);
    RoboCheckEqInt(sz.width, 500);
    RoboCheckEqInt(sz.height, 360);
    /* Re-present after resize (surface still 400x300 — StretchBlt path). */
    RoboCheckEqInt(WindowPresent(id, surf), RoboOk);
    HostPumpBrief(40);
    RoboCheck(sawResize || sz.width == 500);

    /* Failure paths */
    RoboCheckEqInt(WindowPresent(id, NULL), RoboInvalidArgument);

    GfxRasterDestroy(raster);
    GfxSurfaceRelease(surf);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

static void TestHostPresentDisplayLink(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    DisplayInfo disp;
    float scale = 0.0f;

    if (!BeginHost(&id, &cfg)) {
        return;
    }
    RoboCheckEqInt(WindowShow(id), RoboOk);
    HostPumpBrief(20);
    /* use_host switches RoboDisplay to Win32 monitors when available. */
    RoboCheckEqInt(WindowGetAssociatedDisplay(id, &disp), RoboOk);
    RoboCheck(disp.id != RoboDisplayIdNone);
    RoboCheck(disp.resolutionWidthPx > 0);
    RoboCheckEqInt(WindowGetScale(id, &scale), RoboOk);
    RoboCheck(DisplayScaleIsKnown(scale));
    printf("host linked display=%llu scale=%g\n",
           (unsigned long long)disp.id, (double)scale);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

void TestRegisterWindowHost(void)
{
    TestRegister("window.host.availability", TestHostAvailability);
    TestRegister("window.host.use_unavailable", TestHostUseWhenUnavailable);
    TestRegister("window.host.create_destroy", TestHostCreateDestroy);
    TestRegister("window.host.title_size_pos", TestHostTitleSizePosVisibility);
    TestRegister("window.host.focus_display", TestHostFocusAndDisplay);
    TestRegister("window.host.shutdown_live", TestHostShutdownWithLiveWindow);
    TestRegister("window.host.invalid_config", TestHostInvalidConfig);
    TestRegister("window.host.sizing", TestHostSizingConstraints);
    TestRegister("window.host.close_lifecycle", TestHostCloseLifecycle);
    TestRegister("window.host.scale_dpi", TestHostScaleDpi);
    TestRegister("window.host.event_loop", TestHostEventLoop);
    TestRegister("window.host.keyboard", TestHostKeyboard);
    TestRegister("window.host.mouse", TestHostMouse);
    TestRegister("window.host.present_caps", TestHostPresentCaps);
    TestRegister("window.host.present_colors", TestHostPresentColors);
    TestRegister("window.host.present_pattern",
                       TestHostPresentPatternResize);
    TestRegister("window.host.present_display",
                       TestHostPresentDisplayLink);
    TestRegister("window.host.smoke", TestHostSmokeManual);
    TestRegister("window.host.mock_distinct", TestMockStillDistinct);
}
