/*
 * RoboWindow sizing suite (Goal 7.6).
 * Deterministic mock backends — headless-safe.
 */

#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"

#include <string.h>

typedef struct sizeMock {
    int initCount;
    int failResize;
    int clampMode; /* 0 = actual==request; 1 = +2; 2 = clamp to [min,max] stored */
    int32_t actualW;
    int32_t actualH;
    int32_t minW;
    int32_t minH;
    int32_t maxW;
    int32_t maxH;
    int constraintsCount;
    int sizeCount;
} SizeMock;

static SizeMock gSm;
static WindowBackendCaps gSmCaps;
static WindowBackendVtbl gSmVtbl;

static Result SmInit(void *ud)
{
    SizeMock *m = (SizeMock *)ud;
    if (!m) {
        return RoboInvalidArgument;
    }
    m->initCount++;
    return RoboOk;
}

static void SmShutdown(void *ud)
{
    (void)ud;
}

static Result SmCreate(void *ud,
                               WindowId id,
                               const WindowConfig *cfg,
                               WindowNativeHandle *out)
{
    SizeMock *m = (SizeMock *)ud;
    (void)id;
    if (!m || !cfg || !out) {
        return RoboInvalidArgument;
    }
    m->actualW = cfg->width;
    m->actualH = cfg->height;
    m->minW = cfg->minWidth;
    m->minH = cfg->minHeight;
    m->maxW = cfg->maxWidth;
    m->maxH = cfg->maxHeight;
    return WindowNativeHandleAssign(out, 0x53495A45ull, 1u);
}

static Result SmDestroy(void *ud,
                                WindowId id,
                                WindowNativeHandle *native)
{
    (void)ud;
    (void)id;
    if (native) {
        WindowNativeHandleInvalidate(native);
    }
    return RoboOk;
}

static Result SmShow(void *ud, WindowId id,
                             WindowNativeHandle *n)
{
    (void)ud;
    (void)id;
    (void)n;
    return RoboOk;
}

static Result SmHide(void *ud, WindowId id,
                             WindowNativeHandle *n)
{
    (void)ud;
    (void)id;
    (void)n;
    return RoboOk;
}

static Result SmTitle(void *ud, WindowId id,
                              WindowNativeHandle *n, const char *t)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)t;
    return RoboOk;
}

static Result SmSize(void *ud,
                             WindowId id,
                             WindowNativeHandle *n,
                             int32_t width,
                             int32_t height)
{
    SizeMock *m = (SizeMock *)ud;
    (void)id;
    (void)n;
    if (!m) {
        return RoboInvalidArgument;
    }
    if (m->failResize) {
        return RoboIoError;
    }
    m->sizeCount++;
    if (m->clampMode == 1) {
        m->actualW = width + 2;
        m->actualH = height + 2;
    } else if (m->clampMode == 2) {
        m->actualW = width;
        m->actualH = height;
        if (m->minW > 0 && m->actualW < m->minW) {
            m->actualW = m->minW;
        }
        if (m->minH > 0 && m->actualH < m->minH) {
            m->actualH = m->minH;
        }
        if (m->maxW > 0 && m->actualW > m->maxW) {
            m->actualW = m->maxW;
        }
        if (m->maxH > 0 && m->actualH > m->maxH) {
            m->actualH = m->maxH;
        }
    } else {
        m->actualW = width;
        m->actualH = height;
    }
    return RoboOk;
}

static Result SmConstraints(void *ud,
                                    WindowId id,
                                    WindowNativeHandle *n,
                                    int32_t minW,
                                    int32_t minH,
                                    int32_t maxW,
                                    int32_t maxH)
{
    SizeMock *m = (SizeMock *)ud;
    (void)id;
    (void)n;
    if (!m) {
        return RoboInvalidArgument;
    }
    m->constraintsCount++;
    m->minW = minW;
    m->minH = minH;
    m->maxW = maxW;
    m->maxH = maxH;
    return RoboOk;
}

static Result SmPos(void *ud, WindowId id,
                            WindowNativeHandle *n, int32_t x, int32_t y)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)x;
    (void)y;
    return RoboOk;
}

static Result SmFocus(void *ud, WindowId id,
                              WindowNativeHandle *n)
{
    (void)ud;
    (void)id;
    (void)n;
    return RoboNotSupported;
}

static Result SmQuery(void *ud,
                              WindowId id,
                              const WindowNativeHandle *n,
                              WindowState *st)
{
    SizeMock *m = (SizeMock *)ud;
    (void)id;
    (void)n;
    if (!m || !st) {
        return RoboInvalidArgument;
    }
    st->width = m->actualW;
    st->height = m->actualH;
    return RoboOk;
}

static Result SmPresent(void *ud, WindowId id,
                                WindowNativeHandle *n,
                                GfxSurface *s)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)s;
    return RoboNotSupported;
}

static Result SmEvents(void *ud)
{
    (void)ud;
    return RoboOk;
}

static void SmCaps(void)
{
    memset(&gSmCaps, 0, sizeof(gSmCaps));
    gSmCaps.RoboCreateWindow = 1;
    gSmCaps.RoboDestroyWindow = 1;
    gSmCaps.showHide = 1;
    gSmCaps.resize = 1;
    gSmCaps.move = 1;
    gSmCaps.SetTitle = 1;
    gSmCaps.QueryState = 1;
    gSmCaps.ProcessEvents = 1;
    gSmCaps.reportsActualState = 1;
    gSmCaps.sizeConstraints = 1;
}

static void SmBind(void)
{
    memset(&gSmVtbl, 0, sizeof(gSmVtbl));
    gSmVtbl.name = "size_mock";
    gSmVtbl.capabilities = &gSmCaps;
    gSmVtbl.initialize = SmInit;
    gSmVtbl.shutdown = SmShutdown;
    gSmVtbl.RoboCreateWindow = SmCreate;
    gSmVtbl.RoboDestroyWindow = SmDestroy;
    gSmVtbl.show = SmShow;
    gSmVtbl.hide = SmHide;
    gSmVtbl.SetTitle = SmTitle;
    gSmVtbl.SetSize = SmSize;
    gSmVtbl.SetSizeConstraints = SmConstraints;
    gSmVtbl.SetPosition = SmPos;
    gSmVtbl.focus = SmFocus;
    gSmVtbl.QueryState = SmQuery;
    gSmVtbl.PresentSurface = SmPresent;
    gSmVtbl.ProcessEvents = SmEvents;
}

static void fixture(int clampMode)
{
    memset(&gSm, 0, sizeof(gSm));
    gSm.clampMode = clampMode;
    SmCaps();
    SmBind();
    WindowShutdown();
    RoboCheckEqInt(WindowInit(), RoboOk);
    RoboCheckEqInt(WindowBackendRegister(&gSmVtbl, &gSm), RoboOk);
}

static void TestSizeHelpers(void)
{
    WindowSizeConstraints c;

    RoboCheckEqInt(WindowSizeValidate(0, 10), RoboInvalidArgument);
    RoboCheckEqInt(WindowSizeValidate(-1, 10), RoboInvalidArgument);
    RoboCheckEqInt(WindowSizeValidate(10, 0), RoboInvalidArgument);
    RoboCheckEqInt(WindowSizeValidate(RoboWindowMaxDim + 1, 10),
                      RoboInvalidArgument);
    RoboCheckEqInt(WindowSizeValidate(100, 80), RoboOk);

    WindowSizeConstraintsInit(&c);
    RoboCheckEqInt(WindowSizeConstraintsValidate(&c), RoboOk);
    c.minWidth = 100;
    c.maxWidth = 50;
    RoboCheckEqInt(WindowSizeConstraintsValidate(&c),
                      RoboInvalidArgument);
    c.maxWidth = 100;
    RoboCheckEqInt(WindowSizeConstraintsValidate(&c), RoboOk);
    c.minWidth = -1;
    RoboCheckEqInt(WindowSizeConstraintsValidate(&c),
                      RoboInvalidArgument);

    WindowSizeConstraintsInit(&c);
    c.minWidth = 200;
    c.minHeight = 100;
    c.maxWidth = 800;
    c.maxHeight = 600;
    RoboCheckEqInt(
        WindowSizeValidateAgainstConstraints(100, 200, &c),
        RoboInvalidArgument);
    RoboCheckEqInt(
        WindowSizeValidateAgainstConstraints(200, 100, &c), RoboOk);
    RoboCheckEqInt(
        WindowSizeValidateAgainstConstraints(900, 100, &c),
        RoboInvalidArgument);
}

static void TestInitialSize(void)
{
    WindowConfig cfg;
    WindowId id;
    GfxSize sz;
    WindowSizeConstraints c;

    fixture(0);
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    cfg.width = 640;
    cfg.height = 480;
    cfg.minWidth = 100;
    cfg.minHeight = 80;
    cfg.maxWidth = 1200;
    cfg.maxHeight = 900;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(WindowGetSize(id, &sz), RoboOk);
    RoboCheckEqInt(sz.width, 640);
    RoboCheckEqInt(sz.height, 480);
    RoboCheckEqInt(WindowGetRequestedSize(id, &sz), RoboOk);
    RoboCheckEqInt(sz.width, 640);
    RoboCheckEqInt(WindowGetSizeConstraints(id, &c), RoboOk);
    RoboCheckEqInt(c.minWidth, 100);
    RoboCheckEqInt(c.maxHeight, 900);

    cfg.width = 50; /* below min */
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboInvalidArgument);
    cfg.width = 640;
    cfg.minWidth = 900;
    cfg.maxWidth = 800;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboInvalidArgument);
    WindowShutdown();
}

static void TestRuntimeResize(void)
{
    WindowConfig cfg;
    WindowId id;
    GfxSize sz;
    WindowState st;
    int i;

    fixture(0);
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    cfg.width = 400;
    cfg.height = 300;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);

    RoboCheckEqInt(WindowSetSize(id, 500, 400), RoboOk);
    RoboCheckEqInt(WindowGetSize(id, &sz), RoboOk);
    RoboCheckEqInt(sz.width, 500);
    RoboCheckEqInt(sz.height, 400);
    RoboCheckEqInt(WindowSetSize(id, 500, 400), RoboOk); /* same */

    for (i = 0; i < 5; i++) {
        RoboCheckEqInt(WindowRequestSize(id, 410 + i, 310 + i),
                          RoboOk);
    }

    RoboCheckEqInt(WindowSetSize(id, 0, 100), RoboInvalidArgument);
    RoboCheckEqInt(WindowSetSize(id, -5, 100), RoboInvalidArgument);
    RoboCheckEqInt(WindowSetSize(id, RoboWindowMaxDim + 1, 100),
                      RoboInvalidArgument);

    gSm.failResize = 1;
    RoboCheckEqInt(WindowGetRequestedSize(id, &sz), RoboOk);
    RoboCheckEqInt(WindowSetSize(id, 450, 350), RoboIoError);
    RoboCheckEqInt(WindowGetRequestedSize(id, &sz), RoboOk);
    /* requested unchanged after failure */
    RoboCheckEqInt(sz.width, 414);
    RoboCheckEqInt(WindowGetState(id, &st), RoboOk);
    RoboCheck(st.width > 0);
    gSm.failResize = 0;

    RoboCheckEqInt(WindowHide(id), RoboOk);
    RoboCheckEqInt(WindowSetSize(id, 320, 240), RoboOk);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheckEqInt(WindowSetSize(id, 100, 100), RoboNotFound);
    RoboCheckEqInt(WindowGetSize(id, &sz), RoboNotFound);
    WindowShutdown();
}

static void TestMinMaxConstraints(void)
{
    WindowConfig cfg;
    WindowId id;
    WindowSizeConstraints c;
    GfxSize sz;

    fixture(0);
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    cfg.width = 400;
    cfg.height = 300;
    cfg.minWidth = 200;
    cfg.minHeight = 150;
    cfg.maxWidth = 800;
    cfg.maxHeight = 600;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);

    RoboCheckEqInt(WindowSetSize(id, 100, 200), RoboInvalidArgument);
    RoboCheckEqInt(WindowSetSize(id, 200, 150), RoboOk); /* at min */
    RoboCheckEqInt(WindowSetSize(id, 800, 600), RoboOk); /* at max */
    RoboCheckEqInt(WindowSetSize(id, 900, 400), RoboInvalidArgument);

    WindowSizeConstraintsInit(&c);
    c.minWidth = 300;
    c.minHeight = 200;
    c.maxWidth = 700;
    c.maxHeight = 500;
    RoboCheckEqInt(WindowSetSizeConstraints(id, &c), RoboOk);
    RoboCheckEqInt(gSm.constraintsCount, 1);
    /* was 800x600 → corrective clamp to 700x500 */
    RoboCheckEqInt(WindowGetSize(id, &sz), RoboOk);
    RoboCheckEqInt(sz.width, 700);
    RoboCheckEqInt(sz.height, 500);

    c.minWidth = 900;
    c.maxWidth = 100;
    RoboCheckEqInt(WindowSetSizeConstraints(id, &c),
                      RoboInvalidArgument);

    /* equal min/max */
    WindowSizeConstraintsInit(&c);
    c.minWidth = 400;
    c.minHeight = 300;
    c.maxWidth = 400;
    c.maxHeight = 300;
    RoboCheckEqInt(WindowSetSizeConstraints(id, &c), RoboOk);
    RoboCheckEqInt(WindowSetSize(id, 400, 300), RoboOk);
    RoboCheckEqInt(WindowSetSize(id, 401, 300), RoboInvalidArgument);

    /* clear constraints */
    WindowSizeConstraintsInit(&c);
    RoboCheckEqInt(WindowSetSizeConstraints(id, &c), RoboOk);
    RoboCheckEqInt(WindowSetSize(id, 50, 40), RoboOk);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

static void TestRequestedVsActual(void)
{
    WindowConfig cfg;
    WindowId id;
    GfxSize req;
    GfxSize act;

    fixture(1); /* actual = request + 2 */
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    cfg.width = 200;
    cfg.height = 100;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(WindowSetSize(id, 300, 200), RoboOk);
    RoboCheckEqInt(WindowGetRequestedSize(id, &req), RoboOk);
    RoboCheckEqInt(WindowGetSize(id, &act), RoboOk);
    RoboCheckEqInt(req.width, 300);
    RoboCheckEqInt(req.height, 200);
    RoboCheckEqInt(act.width, 302);
    RoboCheckEqInt(act.height, 202);
    RoboCheck(req.width != act.width);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

static void TestObserveSize(void)
{
    WindowConfig cfg;
    WindowId id;
    GfxSize sz;
    WindowEvent ev;

    fixture(0);
    /* Observe path is for OS-reported sizes without treating query as
     * authoritative overwrite in this test. */
    gSmCaps.reportsActualState = 0;
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    while (WindowEventPoll(&ev) == RoboOk) {
    }
    RoboCheckEqInt(WindowObserveSize(id, 111, 222), RoboOk);
    RoboCheckEqInt(WindowGetSize(id, &sz), RoboOk);
    RoboCheckEqInt(sz.width, 111);
    RoboCheckEqInt(sz.height, 222);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventResized);
    /* requested unchanged */
    RoboCheckEqInt(WindowGetRequestedSize(id, &sz), RoboOk);
    RoboCheckEqInt(sz.width, 800);
    RoboCheckEqInt(WindowObserveSize(id, 0, 10), RoboInvalidArgument);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

static void TestSizeThreading(void)
{
    WindowConfig cfg;
    WindowId id;

    fixture(0);
    RoboCheckEqInt(WindowThreadBindOwner(7), RoboOk);
    RoboCheckEqInt(WindowThreadEnter(7), RoboOk);
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    RoboCheckEqInt(WindowSetSize(id, 100, 100), RoboOk);
    RoboCheckEqInt(WindowThreadEnter(8), RoboOk);
    RoboCheckEqInt(WindowSetSize(id, 120, 120),
                      RoboPermissionDenied);
    RoboCheckEqInt(WindowThreadEnter(7), RoboOk);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    WindowShutdown();
}

void TestRegisterWindowSize(void)
{
    TestRegister("window.size.helpers", TestSizeHelpers);
    TestRegister("window.size.initial", TestInitialSize);
    TestRegister("window.size.runtime", TestRuntimeResize);
    TestRegister("window.size.constraints", TestMinMaxConstraints);
    TestRegister("window.size.requested_actual", TestRequestedVsActual);
    TestRegister("window.size.observe", TestObserveSize);
    TestRegister("window.size.thread", TestSizeThreading);
}
