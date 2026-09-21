/*
 * RoboWindow architecture suite (Goal 7.1).
 * Headless — mock / null backend only; no OS window.
 */

#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"

#include <string.h>

typedef struct mockBackendData {
    int initCount;
    int shutdownCount;
    int createCount;
    int destroyCount;
    int failCreate;
    int failInit;
    uint64_t lastNativeToken;
} MockBackendData;

static MockBackendData gMock;

static Result MockInitialize(void *userdata)
{
    MockBackendData *m = (MockBackendData *)userdata;
    if (!m) {
        return RoboInvalidArgument;
    }
    if (m->failInit) {
        return RoboInitError;
    }
    m->initCount++;
    return RoboOk;
}

static void MockShutdown(void *userdata)
{
    MockBackendData *m = (MockBackendData *)userdata;
    if (m) {
        m->shutdownCount++;
    }
}

static Result MockCreateWindow(void *userdata,
                                        WindowId id,
                                        const WindowConfig *cfg,
                                        WindowNativeHandle *outNative)
{
    MockBackendData *m = (MockBackendData *)userdata;
    (void)id;
    (void)cfg;
    if (!m || !outNative) {
        return RoboInvalidArgument;
    }
    if (m->failCreate) {
        return RoboError;
    }
    m->createCount++;
    m->lastNativeToken = (uint64_t)0x4D4F434Bu; /* "MOCK" */
    return WindowNativeHandleAssign(outNative, m->lastNativeToken, 1u);
}

static Result MockDestroyWindow(void *userdata,
                                         WindowId id,
                                         WindowNativeHandle *native)
{
    MockBackendData *m = (MockBackendData *)userdata;
    (void)id;
    if (m) {
        m->destroyCount++;
    }
    if (native) {
        WindowNativeHandleInvalidate(native);
    }
    return RoboOk;
}

static Result MockShow(void *userdata,
                               WindowId id,
                               WindowNativeHandle *native)
{
    (void)userdata;
    (void)id;
    (void)native;
    return RoboOk;
}

static Result MockHide(void *userdata,
                               WindowId id,
                               WindowNativeHandle *native)
{
    (void)userdata;
    (void)id;
    (void)native;
    return RoboOk;
}

static Result MockSetTitle(void *userdata,
                                    WindowId id,
                                    WindowNativeHandle *native,
                                    const char *utf8Title)
{
    (void)userdata;
    (void)id;
    (void)native;
    (void)utf8Title;
    return RoboOk;
}

static Result MockSetSize(void *userdata,
                                   WindowId id,
                                   WindowNativeHandle *native,
                                   int32_t width,
                                   int32_t height)
{
    (void)userdata;
    (void)id;
    (void)native;
    (void)width;
    (void)height;
    return RoboOk;
}

static Result MockSetPosition(void *userdata,
                                       WindowId id,
                                       WindowNativeHandle *native,
                                       int32_t x,
                                       int32_t y)
{
    (void)userdata;
    (void)id;
    (void)native;
    (void)x;
    (void)y;
    return RoboOk;
}

static Result MockFocus(void *userdata,
                                WindowId id,
                                WindowNativeHandle *native)
{
    (void)userdata;
    (void)id;
    (void)native;
    return RoboOk;
}

static Result MockPresent(void *userdata,
                                  WindowId id,
                                  WindowNativeHandle *native,
                                  GfxSurface *surface)
{
    (void)userdata;
    (void)id;
    (void)native;
    (void)surface;
    return RoboNotSupported;
}

static Result MockProcessEvents(void *userdata)
{
    (void)userdata;
    return RoboOk;
}

static const WindowBackendCaps gMockCaps = {
    1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 1, 0u, 0u, 0u, 0, 0, 0};

static Result MockSetSizeConstraints(void *userdata,
                                               WindowId id,
                                               WindowNativeHandle *native,
                                               int32_t minWidth,
                                               int32_t minHeight,
                                               int32_t maxWidth,
                                               int32_t maxHeight)
{
    (void)userdata;
    (void)id;
    (void)native;
    (void)minWidth;
    (void)minHeight;
    (void)maxWidth;
    (void)maxHeight;
    return RoboOk;
}

static const WindowBackendVtbl gMockVtbl = {
    "mock",
    &gMockCaps,
    MockInitialize,
    MockShutdown,
    MockCreateWindow,
    MockDestroyWindow,
    MockShow,
    MockHide,
    MockSetTitle,
    MockSetSize,
    MockSetSizeConstraints,
    MockSetPosition,
    MockFocus,
    NULL,
    MockPresent,
    MockProcessEvents,
    NULL,
    NULL,
    NULL,
    NULL};

static void DrainEvents(void)
{
    WindowEvent ev;
    while (WindowEventPoll(&ev) == RoboOk) {
    }
}

static void FixtureStart(void)
{
    memset(&gMock, 0, sizeof(gMock));
    WindowShutdown();
    RoboCheckEqInt(WindowInit(), RoboOk);
    RoboCheckEqInt(WindowBackendRegister(&gMockVtbl, &gMock),
                      RoboOk);
    DrainEvents();
}

static void FixtureEnd(void)
{
    WindowShutdown();
}

static void TestWindowVersionInit(void)
{
    RoboCheckStrEq(WindowVersionString(), "0.15.0");
    RoboCheckEqInt(RoboWindowApiFrozen, 0);
    WindowShutdown();
    RoboCheck(!WindowIsInitialized());
    RoboCheckEqInt(WindowInit(), RoboOk);
    RoboCheckEqInt(WindowInit(), RoboOk);
    RoboCheck(WindowIsInitialized());
    WindowShutdown();
    RoboCheck(!WindowIsInitialized());
}

static void TestWindowBackendContract(void)
{
    FixtureStart();
    RoboCheck(WindowBackendIsRegistered());
    RoboCheckStrEq(WindowBackendName(), "mock");
    RoboCheckEqInt(gMock.initCount, 1);

    RoboCheckEqInt(WindowBackendRegister(NULL, NULL),
                      RoboInvalidArgument);

    gMock.failInit = 1;
    RoboCheckEqInt(WindowBackendRegister(&gMockVtbl, &gMock),
                      RoboInitError);
    /* previous backend was cleared before failed init */
    RoboCheck(!WindowBackendIsRegistered());

    gMock.failInit = 0;
    RoboCheckEqInt(WindowBackendUseNull(), RoboOk);
    RoboCheckStrEq(WindowBackendName(), "null");
    FixtureEnd();
}

static void TestWindowNoBackend(void)
{
    WindowConfig cfg;
    WindowId id = 99u;

    WindowShutdown();
    RoboCheckEqInt(WindowInit(), RoboOk);
    WindowConfigInit(&cfg);
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboNotSupported);
    RoboCheckEqInt((int)id, (int)RoboWindowIdNone);
    FixtureEnd();
}

static void TestWindowIdentity(void)
{
    WindowConfig cfg;
    WindowId a = RoboWindowIdNone;
    WindowId b = RoboWindowIdNone;

    FixtureStart();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &a), RoboOk);
    RoboCheckEqInt(WindowCreate(&cfg, &b), RoboOk);
    RoboCheck(a != RoboWindowIdNone);
    RoboCheck(b != RoboWindowIdNone);
    RoboCheck(a != b);
    RoboCheck(WindowIdIsLive(a));
    RoboCheckEqInt(WindowDestroy(a), RoboOk);
    RoboCheck(!WindowIdIsLive(a));
    RoboCheckEqInt(WindowDestroy(a), RoboNotFound);
    RoboCheckEqInt(WindowGetState(a, NULL), RoboInvalidArgument);
    {
        WindowState st;
        RoboCheckEqInt(WindowGetState(a, &st), RoboNotFound);
    }
    RoboCheckEqInt(WindowDestroy(b), RoboOk);
    FixtureEnd();
}

static void TestWindowLifecycle(void)
{
    WindowConfig cfg;
    WindowId id;
    WindowState st;
    WindowEvent ev;

    FixtureStart();
    RoboCheckEqInt(
        WindowLifecycleCheckTransition(RoboWindowLifecycleCreated,
                                               RoboWindowLifecycleCreated),
        RoboInvalidArgument);
    RoboCheckEqInt(WindowLifecycleCheckTransition(
                          RoboWindowLifecycleClosed,
                          RoboWindowLifecycleCreated),
                      RoboInvalidArgument);
    RoboCheckEqInt(WindowLifecycleCheckTransition(
                          RoboWindowLifecycleClosed,
                          RoboWindowLifecycleClosed),
                      RoboOk);

    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventCreated);
    RoboCheckEqInt((int)ev.windowId, (int)id);

    RoboCheckEqInt(WindowGetState(id, &st), RoboOk);
    RoboCheckEqInt((int)st.lifecycle, (int)RoboWindowLifecycleCreated);
    RoboCheckEqInt(st.visible, 0);

    RoboCheckEqInt(WindowShow(id), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventShown);
    RoboCheckEqInt(WindowGetState(id, &st), RoboOk);
    RoboCheckEqInt(st.visible, 1);

    RoboCheckEqInt(WindowHide(id), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventHidden);

    RoboCheckEqInt(WindowRequestClose(id), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventCloseRequested);
    RoboCheckEqInt(WindowGetState(id, &st), RoboOk);
    RoboCheckEqInt((int)st.lifecycle, (int)RoboWindowLifecycleClosing);
    RoboCheckEqInt(WindowRequestClose(id), RoboAlreadyExists);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventClosed);
    RoboCheckEqInt(WindowDestroy(id), RoboNotFound);
    FixtureEnd();
}

static void TestWindowConfig(void)
{
    WindowConfig cfg;
    WindowId id;
    const char *title = NULL;
    WindowState st;

    FixtureStart();
    WindowConfigInit(&cfg);
    RoboCheckEqInt(cfg.width, 800);
    RoboCheckEqInt(cfg.height, 600);
    RoboCheckEqInt(cfg.visible, 1);
    RoboCheckEqInt(cfg.resizable, 1);

    cfg.width = 0;
    RoboCheckEqInt(WindowConfigValidate(&cfg), RoboInvalidArgument);
    cfg.width = 100;
    cfg.height = 50;
    cfg.minWidth = 200;
    RoboCheckEqInt(WindowConfigValidate(&cfg), RoboInvalidArgument);
    cfg.minWidth = 10;
    cfg.maxWidth = 5;
    RoboCheckEqInt(WindowConfigValidate(&cfg), RoboInvalidArgument);

    WindowConfigInit(&cfg);
    cfg.width = 320;
    cfg.height = 240;
    cfg.minWidth = 100;
    cfg.minHeight = 80;
    cfg.maxWidth = 1920;
    cfg.maxHeight = 1080;
    cfg.title = "Roboticela";
    cfg.visible = 1;
    cfg.fullscreen = 0;
    cfg.resizable = 1;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(WindowGetTitle(id, &title), RoboOk);
    RoboCheckStrEq(title, "Roboticela");
    RoboCheckEqInt(WindowGetState(id, &st), RoboOk);
    RoboCheckEqInt(st.width, 320);
    RoboCheckEqInt(st.height, 240);
    RoboCheckEqInt(st.visible, 1);
    RoboCheckEqInt(st.requestedWidth, 320);
    RoboCheckEqInt(WindowSetTitle(id, "Runtime"), RoboOk);
    RoboCheckEqInt(WindowGetTitle(id, &title), RoboOk);
    RoboCheckStrEq(title, "Runtime");
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    FixtureEnd();
}

static void TestWindowDisplay(void)
{
    DisplayInfo info;

    FixtureStart();
    RoboCheckEqInt((int)DisplayCount(), 1);
    RoboCheckEqInt(DisplayPrimary(&info), RoboOk);
    RoboCheck(info.id != RoboDisplayIdNone);
    RoboCheckEqInt(info.bounds.width, 1280);
    RoboCheckEqInt(info.bounds.height, 800);
    RoboCheckEqInt(info.workArea.width, 1280);
    RoboCheckEqInt(info.isPrimary, 1);
    RoboCheck(info.scale > 0.9f && info.scale < 1.1f);
    RoboCheckEqInt(info.physicalWidthMm, 0);
    RoboCheckEqInt(DisplayGet(info.id, &info), RoboOk);
    RoboCheckEqInt(DisplayGet(RoboDisplayIdNone, &info),
                      RoboNotFound);
    RoboCheckEqInt(DisplayAt(1u, &info), RoboNotFound);
    FixtureEnd();
}

static void TestWindowEvents(void)
{
    WindowEvent ev;
    WindowEvent out;

    FixtureStart();
    WindowEventInit(&ev);
    RoboCheckEqInt(WindowEventPush(&ev), RoboInvalidArgument);
    ev.type = RoboWindowEventFocusGained;
    ev.windowId = (WindowId)7u;
    RoboCheckEqInt(WindowEventPush(&ev), RoboOk);
    RoboCheckEqInt((int)WindowEventQueueSize(), 1);
    RoboCheckEqInt(WindowEventPoll(&out), RoboOk);
    RoboCheckEqInt((int)out.type, (int)RoboWindowEventFocusGained);
    RoboCheckEqInt((int)out.windowId, 7);
    RoboCheckEqInt(WindowEventPoll(&out), RoboNotFound);
    RoboCheck(WindowEventTypeIsKnown(RoboWindowEventResized));
    RoboCheck(!WindowEventTypeIsKnown(RoboWindowEventNone));
    FixtureEnd();
}

static void TestWindowPresentContract(void)
{
    WindowConfig cfg;
    WindowId id;
    GfxSurfaceDesc desc;
    GfxSurface *surf = NULL;

    FixtureStart();
    RoboCheckEqInt(WindowBackendUseNull(), RoboOk);
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);

    GfxSurfaceDescInit(&desc);
    desc.width = 8;
    desc.height = 8;
    desc.format = RoboGfxFormatRgba8;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surf), RoboOk);
    RoboCheckEqInt(WindowSetPresentSurface(id, surf), RoboOk);
    RoboCheck(WindowGetPresentSurface(id) == surf);
    RoboCheckEqInt(WindowPresent(id, NULL), RoboNotSupported);
    RoboCheckEqInt(WindowSetPresentSurface(id, NULL), RoboOk);
    RoboCheck(WindowGetPresentSurface(id) == NULL);
    GfxSurfaceRelease(surf);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    FixtureEnd();
}

static void TestWindowMockFailure(void)
{
    WindowConfig cfg;
    WindowId id = 1u;

    FixtureStart();
    gMock.failCreate = 1;
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboError);
    RoboCheckEqInt((int)id, (int)RoboWindowIdNone);
    RoboCheckEqInt((int)WindowLiveCount(), 0);
    FixtureEnd();
}

static void TestWindowNativeOpaque(void)
{
    WindowNativeHandle h;

    WindowNativeHandleClear(&h);
    RoboCheck(WindowNativeHandleIsEmpty(&h));
    RoboCheckEqInt(WindowNativeHandleAssign(&h, 1u, 1u), RoboOk);
    RoboCheck(!WindowNativeHandleIsEmpty(&h));
    WindowNativeHandleInvalidate(&h);
    RoboCheck(WindowNativeHandleIsEmpty(&h));
}

static void TestWindowSizePosition(void)
{
    WindowConfig cfg;
    WindowId id;
    WindowState st;
    WindowEvent ev;

    FixtureStart();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    DrainEvents();
    RoboCheckEqInt(WindowRequestSize(id, 640, 480), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventResized);
    RoboCheckEqInt(ev.width, 640);
    RoboCheckEqInt(WindowRequestPosition(id, 10, 20), RoboOk);
    RoboCheckEqInt(WindowGetState(id, &st), RoboOk);
    RoboCheckEqInt(st.x, 10);
    RoboCheckEqInt(st.y, 20);
    RoboCheckEqInt(WindowRequestSize(id, 0, 10), RoboInvalidArgument);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    FixtureEnd();
}

void TestRegisterWindow(void)
{
    TestRegister("window.version", TestWindowVersionInit);
    TestRegister("window.backend", TestWindowBackendContract);
    TestRegister("window.no_backend", TestWindowNoBackend);
    TestRegister("window.identity", TestWindowIdentity);
    TestRegister("window.lifecycle", TestWindowLifecycle);
    TestRegister("window.config", TestWindowConfig);
    TestRegister("window.display", TestWindowDisplay);
    TestRegister("window.events", TestWindowEvents);
    TestRegister("window.present", TestWindowPresentContract);
    TestRegister("window.mock_fail", TestWindowMockFailure);
    TestRegister("window.native", TestWindowNativeOpaque);
    TestRegister("window.geom", TestWindowSizePosition);
}
