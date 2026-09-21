/*
 * RoboWindow native-abstraction suite (Goal 7.2).
 * Headless mock backend — not a real OS window.
 */

#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"

#include <string.h>

typedef struct richMock {
    int initCount;
    int shutdownCount;
    int createCount;
    int destroyCount;
    int showCount;
    int hideCount;
    int titleCount;
    int sizeCount;
    int posCount;
    int focusCount;
    int failDestroy;
    int failShow;
    uint32_t nextGen;
    char lastTitle[RoboWindowTitleMax];
    int32_t actualW;
    int32_t actualH;
    int32_t actualX;
    int32_t actualY;
    int32_t minW;
    int32_t minH;
    int32_t maxW;
    int32_t maxH;
    int visible;
    int focused;
} RichMock;

static RichMock gRm;
static WindowBackendCaps gRichCaps;

static Result RmInit(void *userdata)
{
    RichMock *m = (RichMock *)userdata;
    if (!m) {
        return RoboInvalidArgument;
    }
    m->initCount++;
    m->nextGen = 1u;
    return RoboOk;
}

static void RmShutdown(void *userdata)
{
    RichMock *m = (RichMock *)userdata;
    if (m) {
        m->shutdownCount++;
    }
}

static Result RmCreate(void *userdata,
                               WindowId id,
                               const WindowConfig *cfg,
                               WindowNativeHandle *outNative)
{
    RichMock *m = (RichMock *)userdata;
    uint32_t gen;
    (void)id;
    if (!m || !cfg || !outNative) {
        return RoboInvalidArgument;
    }
    m->createCount++;
    gen = m->nextGen++;
    m->actualW = cfg->width;
    m->actualH = cfg->height;
    m->actualX = 0;
    m->actualY = 0;
    m->visible = 0;
    m->focused = 0;
    if (cfg->title) {
        size_t i;
        for (i = 0u; i + 1u < sizeof(m->lastTitle) && cfg->title[i]; i++) {
            m->lastTitle[i] = cfg->title[i];
        }
        m->lastTitle[i] = '\0';
    } else {
        m->lastTitle[0] = '\0';
    }
    return WindowNativeHandleAssign(outNative, 0x52494348ull /* RICH */,
                                            gen);
}

static Result RmDestroy(void *userdata,
                                WindowId id,
                                WindowNativeHandle *native)
{
    RichMock *m = (RichMock *)userdata;
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

static Result RmShow(void *userdata,
                             WindowId id,
                             WindowNativeHandle *native)
{
    RichMock *m = (RichMock *)userdata;
    (void)id;
    (void)native;
    if (!m) {
        return RoboInvalidArgument;
    }
    if (m->failShow) {
        return RoboError;
    }
    m->showCount++;
    m->visible = 1;
    return RoboOk;
}

static Result RmHide(void *userdata,
                             WindowId id,
                             WindowNativeHandle *native)
{
    RichMock *m = (RichMock *)userdata;
    (void)id;
    (void)native;
    if (!m) {
        return RoboInvalidArgument;
    }
    m->hideCount++;
    m->visible = 0;
    return RoboOk;
}

static Result RmTitle(void *userdata,
                              WindowId id,
                              WindowNativeHandle *native,
                              const char *utf8Title)
{
    RichMock *m = (RichMock *)userdata;
    size_t i;
    (void)id;
    (void)native;
    if (!m) {
        return RoboInvalidArgument;
    }
    m->titleCount++;
    if (!utf8Title) {
        m->lastTitle[0] = '\0';
        return RoboOk;
    }
    for (i = 0u; i + 1u < sizeof(m->lastTitle) && utf8Title[i]; i++) {
        m->lastTitle[i] = utf8Title[i];
    }
    m->lastTitle[i] = '\0';
    return RoboOk;
}

static Result RmSize(void *userdata,
                             WindowId id,
                             WindowNativeHandle *native,
                             int32_t width,
                             int32_t height)
{
    RichMock *m = (RichMock *)userdata;
    (void)id;
    (void)native;
    if (!m) {
        return RoboInvalidArgument;
    }
    m->sizeCount++;
    /* Report actual as request + 1 to exercise state sync. */
    m->actualW = width + 1;
    m->actualH = height + 1;
    return RoboOk;
}

static Result RmSizeConstraints(void *userdata,
                                         WindowId id,
                                         WindowNativeHandle *native,
                                         int32_t minWidth,
                                         int32_t minHeight,
                                         int32_t maxWidth,
                                         int32_t maxHeight)
{
    RichMock *m = (RichMock *)userdata;
    (void)id;
    (void)native;
    if (!m) {
        return RoboInvalidArgument;
    }
    m->minW = minWidth;
    m->minH = minHeight;
    m->maxW = maxWidth;
    m->maxH = maxHeight;
    return RoboOk;
}

static Result RmPos(void *userdata,
                            WindowId id,
                            WindowNativeHandle *native,
                            int32_t x,
                            int32_t y)
{
    RichMock *m = (RichMock *)userdata;
    (void)id;
    (void)native;
    if (!m) {
        return RoboInvalidArgument;
    }
    m->posCount++;
    m->actualX = x;
    m->actualY = y;
    return RoboOk;
}

static Result RmFocus(void *userdata,
                              WindowId id,
                              WindowNativeHandle *native)
{
    RichMock *m = (RichMock *)userdata;
    (void)id;
    (void)native;
    if (!m) {
        return RoboInvalidArgument;
    }
    m->focusCount++;
    m->focused = 1;
    return RoboOk;
}

static Result RmQuery(void *userdata,
                              WindowId id,
                              const WindowNativeHandle *native,
                              WindowState *inoutState)
{
    RichMock *m = (RichMock *)userdata;
    (void)id;
    (void)native;
    if (!m || !inoutState) {
        return RoboInvalidArgument;
    }
    inoutState->width = m->actualW;
    inoutState->height = m->actualH;
    inoutState->x = m->actualX;
    inoutState->y = m->actualY;
    inoutState->visible = m->visible;
    inoutState->focused = m->focused;
    return RoboOk;
}

static Result RmPresent(void *userdata,
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

static Result RmEvents(void *userdata)
{
    (void)userdata;
    return RoboOk;
}

static const WindowBackendVtbl gRichVtbl = {
    "rich_mock",
    &gRichCaps,
    RmInit,
    RmShutdown,
    RmCreate,
    RmDestroy,
    RmShow,
    RmHide,
    RmTitle,
    RmSize,
    RmSizeConstraints,
    RmPos,
    RmFocus,
    RmQuery,
    RmPresent,
    RmEvents,
    NULL,
    NULL,
    NULL,
    NULL};

static void RichCapsFull(void)
{
    memset(&gRichCaps, 0, sizeof(gRichCaps));
    gRichCaps.RoboCreateWindow = 1;
    gRichCaps.RoboDestroyWindow = 1;
    gRichCaps.showHide = 1;
    gRichCaps.resize = 1;
    gRichCaps.move = 1;
    gRichCaps.SetTitle = 1;
    gRichCaps.focus = 1;
    gRichCaps.QueryState = 1;
    gRichCaps.present = 0;
    gRichCaps.ProcessEvents = 1;
    gRichCaps.reportsActualState = 1;
    gRichCaps.sizeConstraints = 1;
}

static void drain(void)
{
    WindowEvent ev;
    while (WindowEventPoll(&ev) == RoboOk) {
    }
}

static void fixture(void)
{
    memset(&gRm, 0, sizeof(gRm));
    RichCapsFull();
    WindowShutdown();
    RoboCheckEqInt(WindowInit(), RoboOk);
    RoboCheckEqInt(WindowBackendRegister(&gRichVtbl, &gRm), RoboOk);
    drain();
}

static void TestNativeHandleOps(void)
{
    WindowNativeHandle a;
    WindowNativeHandle b;

    WindowNativeHandleClear(&a);
    RoboCheck(WindowNativeHandleIsEmpty(&a));
    RoboCheck(!WindowNativeHandleIsValid(&a));
    RoboCheckEqInt(WindowNativeHandleAssign(&a, 0u, 1u),
                      RoboInvalidArgument);
    RoboCheckEqInt(WindowNativeHandleAssign(&a, 42u, 0u),
                      RoboInvalidArgument);
    RoboCheckEqInt(WindowNativeHandleAssign(&a, 42u, 3u), RoboOk);
    RoboCheck(WindowNativeHandleIsValid(&a));
    b = a;
    RoboCheck(WindowNativeHandleEqual(&a, &b));
    WindowNativeHandleInvalidate(&a);
    RoboCheck(!WindowNativeHandleEqual(&a, &b));
    RoboCheck(WindowNativeHandleIsValid(&b));
}

static void TestNativeCreateHandle(void)
{
    WindowConfig cfg;
    WindowId id;
    WindowNativeHandle h;
    WindowNativeHandle stale;

    fixture();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    cfg.title = "Hello";
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(WindowGetNativeHandle(id, &h), RoboOk);
    RoboCheck(WindowNativeHandleIsValid(&h));
    stale = h;
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheckEqInt(WindowGetNativeHandle(id, &h), RoboNotFound);
    RoboCheck(WindowNativeHandleIsValid(&stale)); /* stale copy */
    RoboCheckEqInt(gRm.destroyCount, 1);
    WindowShutdown();
}

static void TestNativeTitleUnicode(void)
{
    WindowConfig cfg;
    WindowId id;
    const char *title = NULL;
    /* "café" in UTF-8 */
    static const char cafe[] = "caf\xC3\xA9";
    static const char bad[] = "\xFF\xFE";

    fixture();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    cfg.title = "";
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(WindowSetTitle(id, cafe), RoboOk);
    RoboCheckEqInt(WindowGetTitle(id, &title), RoboOk);
    RoboCheckStrEq(title, cafe);
    RoboCheckStrEq(gRm.lastTitle, cafe);
    RoboCheckEqInt(WindowSetTitle(id, NULL), RoboOk);
    RoboCheckEqInt(WindowGetTitle(id, &title), RoboOk);
    RoboCheckStrEq(title, "");
    RoboCheckEqInt(WindowSetTitle(id, bad), RoboParseError);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

static void TestNativeSizeSync(void)
{
    WindowConfig cfg;
    WindowId id;
    WindowState st;

    fixture();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    cfg.width = 200;
    cfg.height = 100;
    cfg.minWidth = 50;
    cfg.maxWidth = 400;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(WindowRequestSize(id, 10, 100),
                      RoboInvalidArgument);
    RoboCheckEqInt(WindowRequestSize(id, 500, 100),
                      RoboInvalidArgument);
    RoboCheckEqInt(WindowRequestSize(id, 100, 80), RoboOk);
    RoboCheckEqInt(WindowGetState(id, &st), RoboOk);
    RoboCheckEqInt(st.requestedWidth, 100);
    RoboCheckEqInt(st.requestedHeight, 80);
    /* Mock reports actual = request + 1 */
    RoboCheckEqInt(st.width, 101);
    RoboCheckEqInt(st.height, 81);
    RoboCheckEqInt(WindowRequestPosition(id, -10, 20), RoboOk);
    RoboCheckEqInt(WindowGetState(id, &st), RoboOk);
    RoboCheckEqInt(st.requestedX, -10);
    RoboCheckEqInt(st.x, -10);
    RoboCheckEqInt(st.y, 20);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

static void TestNativeFocusCaps(void)
{
    WindowConfig cfg;
    WindowId id;
    WindowBackendCaps caps;
    WindowState st;
    WindowEvent ev;

    fixture();
    RoboCheckEqInt(WindowBackendCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.focus, 1);
    RoboCheckEqInt(caps.present, 0);
    RoboCheckEqInt(caps.reportsActualState, 1);

    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    drain();
    RoboCheckEqInt(WindowRequestFocus(id), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventFocusGained);
    RoboCheckEqInt(WindowGetState(id, &st), RoboOk);
    RoboCheckEqInt(st.focused, 1);
    RoboCheckEqInt(gRm.focusCount, 1);

    /* Null backend: focus unsupported */
    RoboCheckEqInt(WindowBackendUseNull(), RoboOk);
    {
        WindowId id2;
        WindowConfigInit(&cfg);
        cfg.visible = 0;
        RoboCheckEqInt(WindowCreate(&cfg, &id2), RoboOk);
        RoboCheckEqInt(WindowRequestFocus(id2), RoboNotSupported);
        RoboCheckEqInt(WindowBackendCapabilities(&caps), RoboOk);
        RoboCheckEqInt(caps.focus, 0);
        RoboCheckEqInt(WindowDestroy(id2), RoboOk);
    }
    WindowShutdown();
}

static void TestNativeDestroyFail(void)
{
    WindowConfig cfg;
    WindowId id;

    fixture();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    gRm.failDestroy = 1;
    RoboCheckEqInt(WindowDestroy(id), RoboError);
    RoboCheck(WindowIdIsLive(id));
    gRm.failDestroy = 0;
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheckEqInt(WindowDestroy(id), RoboNotFound);
    WindowShutdown();
}

static void TestNativeVisibility(void)
{
    WindowConfig cfg;
    WindowId id;

    fixture();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(WindowShow(id), RoboOk);
    RoboCheckEqInt(WindowShow(id), RoboOk); /* idempotent */
    RoboCheckEqInt(gRm.showCount, 2);
    RoboCheckEqInt(WindowHide(id), RoboOk);
    RoboCheckEqInt(gRm.hideCount, 1);
    gRm.failShow = 1;
    RoboCheckEqInt(WindowShow(id), RoboError);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

static void TestNativePresentBoundary(void)
{
    WindowConfig cfg;
    WindowId id;
    GfxSurfaceDesc desc;
    GfxSurface *surf = NULL;

    fixture();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(WindowPresent(id, NULL), RoboInvalidArgument);
    GfxSurfaceDescInit(&desc);
    desc.width = 4;
    desc.height = 4;
    desc.format = RoboGfxFormatRgba8;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surf), RoboOk);
    RoboCheckEqInt(WindowSetPresentSurface(id, surf), RoboOk);
    RoboCheckEqInt(WindowPresent(id, NULL), RoboNotSupported);
    GfxSurfaceRelease(surf);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);

    WindowShutdown();
    RoboCheckEqInt(WindowInit(), RoboOk);
    {
        WindowBackendCaps caps;
        RoboCheckEqInt(WindowBackendCapabilities(&caps),
                          RoboNotSupported);
    }
    WindowShutdown();
}

static void TestNativeThreading(void)
{
    WindowConfig cfg;
    WindowId id;

    fixture();
    RoboCheckEqInt(WindowThreadBindOwner(1u), RoboOk);
    RoboCheckEqInt(WindowThreadEnter(1u), RoboOk);
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(WindowThreadEnter(2u), RoboOk);
    RoboCheckEqInt(WindowShow(id), RoboPermissionDenied);
    RoboCheckEqInt(WindowThreadEnter(1u), RoboOk);
    RoboCheckEqInt(WindowShow(id), RoboOk);
    RoboCheckEqInt(WindowThreadBindOwner(RoboWindowThreadNone),
                      RoboOk);
    RoboCheckEqInt(WindowThreadEnter(99u), RoboOk);
    RoboCheckEqInt(WindowHide(id), RoboOk);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

static void TestNativeCapsWithoutBackend(void)
{
    WindowBackendCaps caps;

    WindowShutdown();
    RoboCheckEqInt(WindowInit(), RoboOk);
    RoboCheckEqInt(WindowBackendCapabilities(&caps),
                      RoboNotSupported);
    RoboCheckEqInt(caps.RoboCreateWindow, 0);
    WindowShutdown();
}

void TestRegisterWindowNative(void)
{
    TestRegister("window.native.handle", TestNativeHandleOps);
    TestRegister("window.native.create", TestNativeCreateHandle);
    TestRegister("window.native.title", TestNativeTitleUnicode);
    TestRegister("window.native.size_sync", TestNativeSizeSync);
    TestRegister("window.native.focus_caps", TestNativeFocusCaps);
    TestRegister("window.native.destroy_fail", TestNativeDestroyFail);
    TestRegister("window.native.visibility", TestNativeVisibility);
    TestRegister("window.native.present", TestNativePresentBoundary);
    TestRegister("window.native.thread", TestNativeThreading);
    TestRegister("window.native.caps_none", TestNativeCapsWithoutBackend);
}
