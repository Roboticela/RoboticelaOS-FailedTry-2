/*
 * RoboWindow close & lifecycle suite (Goal 7.7).
 * Headless mock — simulates close request, destroy failure, shutdown.
 */

#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"

#include <string.h>

typedef struct lifeMock {
    int initCount;
    int shutdownCount;
    int createCount;
    int destroyCount;
    int showCount;
    int hideCount;
    int sizeCount;
    int presentCount;
    int failDestroy;
    int failCreate;
    uint32_t nextGen;
    uint64_t lastToken;
} LifeMock;

static LifeMock gLm;
static WindowBackendCaps gLmCaps;
static WindowBackendVtbl gLmVtbl;

static Result LmInit(void *ud)
{
    LifeMock *m = (LifeMock *)ud;
    if (!m) {
        return RoboInvalidArgument;
    }
    m->initCount++;
    m->nextGen = 1u;
    return RoboOk;
}

static void LmShutdown(void *ud)
{
    LifeMock *m = (LifeMock *)ud;
    if (m) {
        m->shutdownCount++;
    }
}

static Result LmCreate(void *ud,
                               WindowId id,
                               const WindowConfig *cfg,
                               WindowNativeHandle *outNative)
{
    LifeMock *m = (LifeMock *)ud;
    (void)id;
    (void)cfg;
    if (!m || !outNative) {
        return RoboInvalidArgument;
    }
    if (m->failCreate) {
        return RoboError;
    }
    m->createCount++;
    m->lastToken = 0x4C494645ull; /* LIFE */
    return WindowNativeHandleAssign(outNative, m->lastToken,
                                            m->nextGen++);
}

static Result LmDestroy(void *ud,
                                WindowId id,
                                WindowNativeHandle *native)
{
    LifeMock *m = (LifeMock *)ud;
    (void)id;
    if (!m) {
        return RoboInvalidArgument;
    }
    if (m->failDestroy) {
        return RoboError;
    }
    m->destroyCount++;
    if (native) {
        WindowNativeHandleInvalidate(native);
    }
    return RoboOk;
}

static Result LmShow(void *ud,
                             WindowId id,
                             WindowNativeHandle *native)
{
    LifeMock *m = (LifeMock *)ud;
    (void)id;
    (void)native;
    if (m) {
        m->showCount++;
    }
    return RoboOk;
}

static Result LmHide(void *ud,
                             WindowId id,
                             WindowNativeHandle *native)
{
    LifeMock *m = (LifeMock *)ud;
    (void)id;
    (void)native;
    if (m) {
        m->hideCount++;
    }
    return RoboOk;
}

static Result LmTitle(void *ud,
                              WindowId id,
                              WindowNativeHandle *native,
                              const char *utf8Title)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)utf8Title;
    return RoboOk;
}

static Result LmSize(void *ud,
                             WindowId id,
                             WindowNativeHandle *native,
                             int32_t width,
                             int32_t height)
{
    LifeMock *m = (LifeMock *)ud;
    (void)id;
    (void)native;
    (void)width;
    (void)height;
    if (m) {
        m->sizeCount++;
    }
    return RoboOk;
}

static Result LmConstraints(void *ud,
                                    WindowId id,
                                    WindowNativeHandle *native,
                                    int32_t minW,
                                    int32_t minH,
                                    int32_t maxW,
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

static Result LmPos(void *ud,
                            WindowId id,
                            WindowNativeHandle *native,
                            int32_t x,
                            int32_t y)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)x;
    (void)y;
    return RoboOk;
}

static Result LmFocus(void *ud,
                              WindowId id,
                              WindowNativeHandle *native)
{
    (void)ud;
    (void)id;
    (void)native;
    return RoboOk;
}

static Result LmPresent(void *ud,
                                WindowId id,
                                WindowNativeHandle *native,
                                GfxSurface *surface)
{
    LifeMock *m = (LifeMock *)ud;
    (void)id;
    (void)native;
    (void)surface;
    if (m) {
        m->presentCount++;
    }
    return RoboOk;
}

static Result LmEvents(void *ud)
{
    (void)ud;
    return RoboOk;
}

static void LifeCapsBind(void)
{
    memset(&gLmCaps, 0, sizeof(gLmCaps));
    gLmCaps.RoboCreateWindow = 1;
    gLmCaps.RoboDestroyWindow = 1;
    gLmCaps.showHide = 1;
    gLmCaps.SetTitle = 1;
    gLmCaps.resize = 1;
    gLmCaps.sizeConstraints = 1;
    gLmCaps.move = 1;
    gLmCaps.focus = 1;
    gLmCaps.present = 1;
    gLmCaps.ProcessEvents = 1;
    gLmCaps.presentFormatMask = RoboWindowPresentFmtCpuDefault;
}

static void LifeVtblBind(void)
{
    memset(&gLmVtbl, 0, sizeof(gLmVtbl));
    gLmVtbl.name = "life_mock";
    gLmVtbl.capabilities = &gLmCaps;
    gLmVtbl.initialize = LmInit;
    gLmVtbl.shutdown = LmShutdown;
    gLmVtbl.RoboCreateWindow = LmCreate;
    gLmVtbl.RoboDestroyWindow = LmDestroy;
    gLmVtbl.show = LmShow;
    gLmVtbl.hide = LmHide;
    gLmVtbl.SetTitle = LmTitle;
    gLmVtbl.SetSize = LmSize;
    gLmVtbl.SetSizeConstraints = LmConstraints;
    gLmVtbl.SetPosition = LmPos;
    gLmVtbl.focus = LmFocus;
    gLmVtbl.PresentSurface = LmPresent;
    gLmVtbl.ProcessEvents = LmEvents;
}

static void fixture(void)
{
    memset(&gLm, 0, sizeof(gLm));
    LifeCapsBind();
    LifeVtblBind();
    RoboCheckEqInt(WindowInit(), RoboOk);
    RoboCheckEqInt(WindowBackendRegister(&gLmVtbl, &gLm), RoboOk);
}

static void FixtureEnd(void)
{
    WindowShutdown();
}

static Result MakeSurface(uint32_t w,
                                  uint32_t h,
                                  GfxSurface **out)
{
    GfxSurfaceDesc desc;
    GfxSurfaceDescInit(&desc);
    desc.width = w;
    desc.height = h;
    desc.format = RoboGfxFormatRgba8;
    return GfxSurfaceCreate(&desc, out);
}

static void DrainCreated(WindowId id)
{
    WindowEvent ev;
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventCreated);
    RoboCheckEqInt((int)ev.windowId, (int)id);
}

static void TestLifeTransitions(void)
{
    WindowConfig cfg;
    WindowId id;
    WindowLifecycle life;
    WindowEvent ev;
    WindowNativeHandle h;

    fixture();
    RoboCheckEqInt(WindowLifecycleAllowsMutation(
                          RoboWindowLifecycleCreated),
                      1);
    RoboCheckEqInt(WindowLifecycleAllowsMutation(
                          RoboWindowLifecycleClosing),
                      0);
    RoboCheckEqInt(WindowLifecycleCheckTransition(
                          RoboWindowLifecycleClosing,
                          RoboWindowLifecycleCreated),
                      RoboInvalidArgument);
    RoboCheckEqInt(WindowLifecycleCheckTransition(
                          RoboWindowLifecycleCreated,
                          RoboWindowLifecycleClosing),
                      RoboOk);

    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    DrainCreated(id);
    RoboCheckEqInt(WindowGetLifecycle(id, &life), RoboOk);
    RoboCheckEqInt((int)life, (int)RoboWindowLifecycleCreated);
    RoboCheckEqInt(WindowIsClosing(id), 0);

    RoboCheckEqInt(WindowShow(id), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventShown);
    RoboCheckEqInt(WindowHide(id), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventHidden);

    RoboCheckEqInt(WindowRequestClose(id), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventCloseRequested);
    RoboCheckEqInt(WindowGetLifecycle(id, &life), RoboOk);
    RoboCheckEqInt((int)life, (int)RoboWindowLifecycleClosing);
    RoboCheckEqInt(WindowIsClosing(id), 1);
    RoboCheck(WindowIdIsLive(id));
    RoboCheckEqInt(WindowGetNativeHandle(id, &h), RoboOk);
    RoboCheck(WindowNativeHandleIsValid(&h));

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventClosed);
    RoboCheckEqInt(WindowGetLifecycle(id, &life), RoboNotFound);
    RoboCheckEqInt(WindowIsClosing(id), 0);
    RoboCheckEqInt(WindowIdIsLive(id), 0);
    RoboCheckEqInt(WindowGetNativeHandle(id, &h), RoboNotFound);
    FixtureEnd();
}

static void TestLifeCloseRequest(void)
{
    WindowConfig cfg;
    WindowId id;
    WindowEvent ev;
    WindowLifecycle life;

    fixture();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    DrainCreated(id);

    RoboCheckEqInt(WindowRequestClose(id), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventCloseRequested);
    RoboCheckEqInt(WindowRequestClose(id), RoboAlreadyExists);
    RoboCheckEqInt(WindowObserveCloseRequest(id), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboNotFound);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt(WindowRequestClose(id), RoboNotFound);
    RoboCheckEqInt(WindowObserveCloseRequest(id), RoboNotFound);
    RoboCheckEqInt(WindowGetLifecycle(id, &life), RoboNotFound);

    /* Close while hidden */
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    DrainCreated(id);
    RoboCheckEqInt(WindowHide(id), RoboOk);
    (void)WindowEventPoll(&ev);
    RoboCheckEqInt(WindowRequestClose(id), RoboOk);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    FixtureEnd();
}

static void TestLifeObserveNativeClose(void)
{
    WindowConfig cfg;
    WindowId id;
    WindowEvent ev;
    WindowLifecycle life;

    fixture();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    DrainCreated(id);

    /* Simulate native WM_CLOSE injection path. */
    RoboCheckEqInt(WindowObserveCloseRequest(id), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventCloseRequested);
    RoboCheckEqInt(WindowGetLifecycle(id, &life), RoboOk);
    RoboCheckEqInt((int)life, (int)RoboWindowLifecycleClosing);
    RoboCheckEqInt(WindowObserveCloseRequest(id), RoboOk);
    RoboCheckEqInt(gLm.destroyCount, 0);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    FixtureEnd();
}

static void TestLifeDestroy(void)
{
    WindowConfig cfg;
    WindowId id;
    WindowEvent ev;
    WindowNativeHandle h;

    fixture();
    WindowConfigInit(&cfg);
    cfg.visible = 0;

    /* Destroy without close request */
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    DrainCreated(id);
    RoboCheckEqInt(WindowGetNativeHandle(id, &h), RoboOk);
    RoboCheck(WindowNativeHandleIsValid(&h));
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventClosed);
    RoboCheckEqInt(gLm.destroyCount, 1);
    RoboCheckEqInt(WindowDestroy(id), RoboNotFound);
    RoboCheckEqInt(gLm.destroyCount, 1);

    /* Destroy after close */
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    DrainCreated(id);
    RoboCheckEqInt(WindowRequestClose(id), RoboOk);
    (void)WindowEventPoll(&ev);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventClosed);
    RoboCheckEqInt(gLm.destroyCount, 2);

    /* Backend destroy failure leaves window live */
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    DrainCreated(id);
    gLm.failDestroy = 1;
    RoboCheckEqInt(WindowDestroy(id), RoboError);
    RoboCheck(WindowIdIsLive(id));
    RoboCheckEqInt((int)WindowIsClosing(id), 0);
    gLm.failDestroy = 0;
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventClosed);
    FixtureEnd();
}

static void TestLifeOpsRestricted(void)
{
    WindowConfig cfg;
    WindowId id;
    WindowEvent ev;
    GfxSurface *surf = NULL;
    WindowSizeConstraints c;

    fixture();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    DrainCreated(id);
    RoboCheckEqInt(MakeSurface(4, 4, &surf), RoboOk);
    RoboCheckEqInt(WindowSetPresentSurface(id, surf), RoboOk);
    RoboCheckEqInt(WindowPresent(id, NULL), RoboOk);

    RoboCheckEqInt(WindowRequestClose(id), RoboOk);
    (void)WindowEventPoll(&ev);

    RoboCheckEqInt(WindowShow(id), RoboInvalidArgument);
    RoboCheckEqInt(WindowHide(id), RoboInvalidArgument);
    RoboCheckEqInt(WindowSetSize(id, 320, 240), RoboInvalidArgument);
    RoboCheckEqInt(WindowSetTitle(id, "x"), RoboInvalidArgument);
    RoboCheckEqInt(WindowRequestPosition(id, 1, 1),
                      RoboInvalidArgument);
    RoboCheckEqInt(WindowRequestFocus(id), RoboInvalidArgument);
    RoboCheckEqInt(WindowPresent(id, surf), RoboInvalidArgument);
    RoboCheckEqInt(WindowSetPresentSurface(id, surf),
                      RoboInvalidArgument);
    WindowSizeConstraintsInit(&c);
    c.minWidth = 100;
    c.minHeight = 100;
    RoboCheckEqInt(WindowSetSizeConstraints(id, &c),
                      RoboInvalidArgument);
    /* Reads still allowed while CLOSING */
    {
        GfxSize sz;
        RoboCheckEqInt(WindowGetSize(id, &sz), RoboOk);
    }

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    (void)WindowEventPoll(&ev);

    RoboCheckEqInt(WindowShow(id), RoboNotFound);
    RoboCheckEqInt(WindowSetSize(id, 320, 240), RoboNotFound);
    RoboCheckEqInt(WindowPresent(id, surf), RoboNotFound);
    RoboCheckEqInt(WindowRequestClose(id), RoboNotFound);

    GfxSurfaceRelease(surf);
    FixtureEnd();
}

static void TestLifeShutdownLive(void)
{
    WindowConfig cfg;
    WindowId id;
    WindowNativeHandle h;

    fixture();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    DrainCreated(id);
    RoboCheckEqInt(WindowRequestClose(id), RoboOk);
    RoboCheck(WindowLiveCount() >= 1u);
    RoboCheckEqInt(gLm.destroyCount, 0);

    FixtureEnd();
    RoboCheck(gLm.shutdownCount >= 1);
    RoboCheck(gLm.destroyCount >= 1);
    RoboCheckEqInt(WindowIdIsLive(id), 0);
    RoboCheckEqInt(WindowGetNativeHandle(id, &h), RoboInitError);
    RoboCheckEqInt(WindowShow(id), RoboInitError);
}

static void TestLifeCallbacks(void)
{
    WindowConfig cfg;
    WindowId id;
    WindowEvent ev;
    int closeN = 0;
    int closedN = 0;

    fixture();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    DrainCreated(id);

    RoboCheckEqInt(WindowRequestClose(id), RoboOk);
    while (WindowEventPoll(&ev) == RoboOk) {
        if (ev.type == RoboWindowEventCloseRequested) {
            closeN++;
        }
        if (ev.type == RoboWindowEventClosed) {
            closedN++;
        }
    }
    RoboCheckEqInt(closeN, 1);
    RoboCheckEqInt(closedN, 0);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    while (WindowEventPoll(&ev) == RoboOk) {
        if (ev.type == RoboWindowEventCloseRequested) {
            closeN++;
        }
        if (ev.type == RoboWindowEventClosed) {
            closedN++;
        }
    }
    RoboCheckEqInt(closeN, 1);
    RoboCheckEqInt(closedN, 1);

    /* No further callbacks after destroy */
    RoboCheckEqInt(WindowEventPoll(&ev), RoboNotFound);
    FixtureEnd();
}

static void TestLifeThreading(void)
{
    WindowConfig cfg;
    WindowId id;

    fixture();
    RoboCheckEqInt(WindowThreadBindOwner(3u), RoboOk);
    RoboCheckEqInt(WindowThreadEnter(3u), RoboOk);

    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    DrainCreated(id);

    RoboCheckEqInt(WindowThreadEnter(4u), RoboOk);
    RoboCheckEqInt(WindowRequestClose(id), RoboPermissionDenied);
    RoboCheckEqInt(WindowDestroy(id), RoboPermissionDenied);

    RoboCheckEqInt(WindowThreadEnter(3u), RoboOk);
    RoboCheckEqInt(WindowRequestClose(id), RoboOk);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);

    RoboCheckEqInt(WindowThreadBindOwner(RoboWindowThreadNone),
                      RoboOk);
    FixtureEnd();
}

void TestRegisterWindowLifecycle(void)
{
    TestRegister("window.life.transitions", TestLifeTransitions);
    TestRegister("window.life.close_request", TestLifeCloseRequest);
    TestRegister("window.life.observe_native", TestLifeObserveNativeClose);
    TestRegister("window.life.destroy", TestLifeDestroy);
    TestRegister("window.life.ops_restricted", TestLifeOpsRestricted);
    TestRegister("window.life.shutdown_live", TestLifeShutdownLive);
    TestRegister("window.life.callbacks", TestLifeCallbacks);
    TestRegister("window.life.thread", TestLifeThreading);
}
