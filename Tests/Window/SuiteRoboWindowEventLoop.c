/*
 * RoboWindow native event loop suite (Goal 7.9).
 *
 * Headless mock backend — injects lifecycle/state events without a GUI.
 */

#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"

#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

typedef enum {
    INJ_NONE = 0,
    INJ_CLOSE,
    INJ_RESIZE,
    INJ_MOVE,
    INJ_SHOW,
    INJ_HIDE,
    INJ_FOCUS_GAIN,
    INJ_FOCUS_LOST
} InjKind;

typedef struct injItem {
    InjKind kind;
    WindowId id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} InjItem;

#define INJ_CAP 32

typedef struct loopMock {
    int initCount;
    int failInit;
    int failEvents;
    int failWait;
    int failWakeup;
    int waitCalls;
    int wakeupCalls;
    int processCalls;
    int nestPollOnProcess;
    volatile int wakeFlag;
    size_t injCount;
    InjItem inj[INJ_CAP];
} LoopMock;

static LoopMock gLm;
static WindowBackendCaps gLmCaps;
static WindowBackendVtbl gLmVtbl;

static void InjPush(InjKind kind, WindowId id, int32_t x, int32_t y,
                     int32_t w, int32_t h)
{
    if (gLm.injCount >= INJ_CAP) {
        return;
    }
    gLm.inj[gLm.injCount].kind = kind;
    gLm.inj[gLm.injCount].id = id;
    gLm.inj[gLm.injCount].x = x;
    gLm.inj[gLm.injCount].y = y;
    gLm.inj[gLm.injCount].w = w;
    gLm.inj[gLm.injCount].h = h;
    gLm.injCount++;
}

static void ApplyInj(const InjItem *it)
{
    switch (it->kind) {
    case INJ_CLOSE:
        (void)WindowObserveCloseRequest(it->id);
        break;
    case INJ_RESIZE:
        (void)WindowObserveSize(it->id, it->w, it->h);
        break;
    case INJ_MOVE:
        (void)WindowObservePosition(it->id, it->x, it->y);
        break;
    case INJ_SHOW:
        (void)WindowObserveVisibility(it->id, 1);
        break;
    case INJ_HIDE:
        (void)WindowObserveVisibility(it->id, 0);
        break;
    case INJ_FOCUS_GAIN:
        (void)WindowObserveFocus(it->id, 1);
        break;
    case INJ_FOCUS_LOST:
        (void)WindowObserveFocus(it->id, 0);
        break;
    default:
        break;
    }
}

static Result LmInit(void *ud)
{
    LoopMock *m = (LoopMock *)ud;
    if (m->failInit) {
        return RoboInitError;
    }
    m->initCount++;
    m->wakeFlag = 0;
    return RoboOk;
}

static void LmShutdown(void *ud)
{
    (void)ud;
}

static Result LmCreate(void *ud, WindowId id,
                               const WindowConfig *cfg,
                               WindowNativeHandle *out)
{
    (void)ud;
    (void)id;
    (void)cfg;
    WindowNativeHandleClear(out);
    return RoboOk;
}

static Result LmDestroy(void *ud, WindowId id,
                                WindowNativeHandle *native)
{
    (void)ud;
    (void)id;
    if (native) {
        WindowNativeHandleInvalidate(native);
    }
    return RoboOk;
}

static Result LmShow(void *ud, WindowId id,
                             WindowNativeHandle *n)
{
    (void)ud;
    (void)id;
    (void)n;
    return RoboOk;
}

static Result LmHide(void *ud, WindowId id,
                             WindowNativeHandle *n)
{
    (void)ud;
    (void)id;
    (void)n;
    return RoboOk;
}

static Result LmTitle(void *ud, WindowId id,
                              WindowNativeHandle *n, const char *t)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)t;
    return RoboOk;
}

static Result LmSize(void *ud, WindowId id,
                             WindowNativeHandle *n, int32_t w,
                             int32_t h)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)w;
    (void)h;
    return RoboOk;
}

static Result LmConstraints(void *ud, WindowId id,
                                    WindowNativeHandle *n, int32_t a,
                                    int32_t b, int32_t c, int32_t d)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    return RoboOk;
}

static Result LmPos(void *ud, WindowId id,
                            WindowNativeHandle *n, int32_t x,
                            int32_t y)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)x;
    (void)y;
    return RoboOk;
}

static Result LmFocus(void *ud, WindowId id,
                              WindowNativeHandle *n)
{
    (void)ud;
    (void)id;
    (void)n;
    return RoboOk;
}

static Result LmPresent(void *ud, WindowId id,
                                WindowNativeHandle *n,
                                GfxSurface *s)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)s;
    return RoboNotSupported;
}

static Result LmProcess(void *ud)
{
    LoopMock *m = (LoopMock *)ud;
    size_t i;
    m->processCalls++;
    if (m->failEvents) {
        return RoboIoError;
    }
    if (m->nestPollOnProcess) {
        m->nestPollOnProcess = 0;
        RoboCheckEqInt(WindowEventLoopPoll(), RoboBusy);
    }
    for (i = 0u; i < m->injCount; i++) {
        ApplyInj(&m->inj[i]);
    }
    m->injCount = 0u;
    return RoboOk;
}

static Result LmWait(void *ud, uint32_t timeoutMs)
{
    LoopMock *m = (LoopMock *)ud;
    uint32_t waited = 0u;
    m->waitCalls++;
    if (m->failWait) {
        return RoboIoError;
    }
    while (!m->wakeFlag) {
        if (timeoutMs != RoboWindowEventWaitForever &&
            waited >= timeoutMs) {
            break;
        }
#if defined(_WIN32)
        Sleep(1);
#else
        /* Host tests are Win32; keep a portable busy slice. */
        {
            volatile int spin = 0;
            for (; spin < 10000; spin++) {
            }
        }
#endif
        waited++;
        if (timeoutMs == 0u) {
            break;
        }
    }
    m->wakeFlag = 0;
    return RoboOk;
}

static Result LmWakeup(void *ud)
{
    LoopMock *m = (LoopMock *)ud;
    m->wakeupCalls++;
    if (m->failWakeup) {
        return RoboIoError;
    }
    m->wakeFlag = 1;
    return RoboOk;
}

static void LmPrepareVtbl(void)
{
    memset(&gLmCaps, 0, sizeof(gLmCaps));
    gLmCaps.RoboCreateWindow = 1;
    gLmCaps.RoboDestroyWindow = 1;
    gLmCaps.showHide = 1;
    gLmCaps.resize = 1;
    gLmCaps.move = 1;
    gLmCaps.SetTitle = 1;
    gLmCaps.focus = 1;
    gLmCaps.ProcessEvents = 1;
    gLmCaps.sizeConstraints = 1;
    gLmCaps.WaitEvents = 1;
    gLmCaps.wakeup = 1;

    memset(&gLmVtbl, 0, sizeof(gLmVtbl));
    gLmVtbl.name = "loop_mock";
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
    gLmVtbl.ProcessEvents = LmProcess;
    gLmVtbl.WaitEvents = LmWait;
    gLmVtbl.wakeup = LmWakeup;
}

static void drain(void)
{
    WindowEvent ev;
    while (WindowEventPoll(&ev) == RoboOk) {
    }
}

static void fixture(void)
{
    memset(&gLm, 0, sizeof(gLm));
    LmPrepareVtbl();
    WindowShutdown();
    RoboCheckEqInt(WindowInit(), RoboOk);
    RoboCheckEqInt(WindowBackendRegister(&gLmVtbl, &gLm), RoboOk);
    drain();
}

static void FixtureEnd(void)
{
    WindowShutdown();
}

static int PollFind(WindowEventType type, WindowId id,
                     WindowEvent *out)
{
    WindowEvent ev;
    while (WindowEventPoll(&ev) == RoboOk) {
        if (ev.type == type && (id == RoboWindowIdNone || ev.windowId == id)) {
            if (out) {
                *out = ev;
            }
            return 1;
        }
    }
    return 0;
}

static void TestQueueBasics(void)
{
    WindowEvent ev;
    WindowEvent peek;
    size_t i;

    fixture();
    RoboCheck(WindowEventQueueIsEmpty());
    RoboCheckEqInt((int)WindowEventQueueSize(), 0);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboNotFound);
    RoboCheckEqInt(WindowEventPeek(&peek), RoboNotFound);

    WindowEventInit(&ev);
    ev.type = RoboWindowEventShown;
    ev.windowId = (WindowId)7u;
    RoboCheckEqInt(WindowEventPush(&ev), RoboOk);
    RoboCheckEqInt((int)WindowEventQueueSize(), 1);
    RoboCheckEqInt(WindowEventPeek(&peek), RoboOk);
    RoboCheckEqInt((int)peek.type, (int)RoboWindowEventShown);
    RoboCheckEqInt((int)WindowEventQueueSize(), 1);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.windowId, 7);

    for (i = 0u; i < RoboWindowEventQueueCapacity; i++) {
        WindowEventInit(&ev);
        ev.type = RoboWindowEventMoved;
        ev.windowId = (WindowId)(i + 1u);
        RoboCheckEqInt(WindowEventPush(&ev), RoboOk);
    }
    RoboCheckEqInt(WindowEventPush(&ev), RoboBusy);
    WindowEventQueueClear();
    RoboCheck(WindowEventQueueIsEmpty());
    FixtureEnd();
}

static void TestQueueOrderingMulti(void)
{
    WindowId a = RoboWindowIdNone;
    WindowId b = RoboWindowIdNone;
    WindowConfig cfg;
    WindowEvent ev;

    fixture();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &a), RoboOk);
    RoboCheckEqInt(WindowCreate(&cfg, &b), RoboOk);
    drain();

    InjPush(INJ_MOVE, a, 1, 2, 0, 0);
    InjPush(INJ_RESIZE, a, 0, 0, 100, 80);
    InjPush(INJ_MOVE, b, 9, 8, 0, 0);
    InjPush(INJ_CLOSE, a, 0, 0, 0, 0);
    RoboCheckEqInt(WindowEventLoopPoll(), RoboOk);

    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventMoved);
    RoboCheck(ev.windowId == a);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventResized);
    RoboCheck(ev.windowId == a);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventMoved);
    RoboCheck(ev.windowId == b);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventCloseRequested);
    RoboCheck(ev.windowId == a);

    RoboCheckEqInt(WindowDestroy(a), RoboOk);
    RoboCheckEqInt(WindowDestroy(b), RoboOk);
    FixtureEnd();
}

static void TestTargeting(void)
{
    WindowId id = RoboWindowIdNone;
    WindowId stale;
    WindowConfig cfg;

    fixture();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    drain();
    stale = id;

    RoboCheckEqInt(WindowObserveSize(RoboWindowIdNone, 10, 10),
                      RoboNotFound);
    RoboCheckEqInt(WindowObserveSize((WindowId)99999u, 10, 10),
                      RoboNotFound);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheckEqInt(WindowObserveCloseRequest(stale), RoboNotFound);
    RoboCheckEqInt(WindowObserveSize(stale, 20, 20), RoboNotFound);
    RoboCheckEqInt(WindowObserveFocus(stale, 1), RoboNotFound);
    FixtureEnd();
}

static void TestClosePath(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    WindowLifecycle life;
    WindowEvent ev;

    fixture();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    drain();

    InjPush(INJ_CLOSE, id, 0, 0, 0, 0);
    RoboCheckEqInt(WindowEventLoopPoll(), RoboOk);
    RoboCheck(PollFind(RoboWindowEventCloseRequested, id, &ev));
    RoboCheckEqInt(WindowGetLifecycle(id, &life), RoboOk);
    RoboCheckEqInt((int)life, (int)RoboWindowLifecycleClosing);
    RoboCheck(WindowIdIsLive(id));

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheck(PollFind(RoboWindowEventClosed, id, &ev));
    RoboCheckEqInt(WindowObserveCloseRequest(id), RoboNotFound);
    FixtureEnd();
}

static void TestResizeMoveVisibilityFocus(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    WindowState st;
    WindowEvent ev;

    fixture();
    WindowConfigInit(&cfg);
    cfg.width = 200;
    cfg.height = 150;
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    drain();

    RoboCheckEqInt(WindowObserveSize(id, 0, 10), RoboInvalidArgument);

    InjPush(INJ_RESIZE, id, 0, 0, 333, 222);
    InjPush(INJ_RESIZE, id, 0, 0, 333, 222); /* repeated — no second event */
    InjPush(INJ_MOVE, id, -40, -20, 0, 0);
    InjPush(INJ_SHOW, id, 0, 0, 0, 0);
    InjPush(INJ_SHOW, id, 0, 0, 0, 0);
    InjPush(INJ_FOCUS_GAIN, id, 0, 0, 0, 0);
    InjPush(INJ_FOCUS_GAIN, id, 0, 0, 0, 0);
    InjPush(INJ_FOCUS_LOST, id, 0, 0, 0, 0);
    InjPush(INJ_HIDE, id, 0, 0, 0, 0);
    RoboCheckEqInt(WindowEventLoopPoll(), RoboOk);

    RoboCheck(PollFind(RoboWindowEventResized, id, &ev));
    RoboCheckEqInt(ev.width, 333);
    RoboCheckEqInt(ev.height, 222);
    RoboCheck(PollFind(RoboWindowEventMoved, id, &ev));
    RoboCheckEqInt(ev.x, -40);
    RoboCheckEqInt(ev.y, -20);
    RoboCheck(PollFind(RoboWindowEventShown, id, &ev));
    RoboCheck(PollFind(RoboWindowEventFocusGained, id, &ev));
    RoboCheck(PollFind(RoboWindowEventFocusLost, id, &ev));
    RoboCheck(PollFind(RoboWindowEventHidden, id, &ev));

    RoboCheckEqInt(WindowGetState(id, &st), RoboOk);
    RoboCheckEqInt(st.width, 333);
    RoboCheckEqInt(st.height, 222);
    RoboCheckEqInt(st.x, -40);
    RoboCheckEqInt(st.y, -20);
    RoboCheckEqInt(st.visible, 0);
    RoboCheckEqInt(st.focused, 0);

    /* Direct observe path (no backend queue). */
    RoboCheckEqInt(WindowObserveSize(id, 400, 300), RoboOk);
    RoboCheck(PollFind(RoboWindowEventResized, id, &ev));

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    FixtureEnd();
}

static void TestLoopControls(void)
{
    fixture();
    RoboCheckEqInt(WindowEventLoopIsRunning(), 0);
    RoboCheckEqInt(WindowEventLoopStart(), RoboOk);
    RoboCheck(WindowEventLoopIsRunning());
    RoboCheckEqInt(WindowEventLoopRunOnce(0), RoboOk);
    RoboCheckEqInt(gLm.processCalls > 0, 1);

    gLm.processCalls = 0;
    RoboCheckEqInt(WindowEventLoopRunOnce(5), RoboOk);
    RoboCheck(gLm.waitCalls >= 1);
    RoboCheck(gLm.processCalls >= 1);

    RoboCheckEqInt(WindowEventLoopWakeup(), RoboOk);
    RoboCheck(gLm.wakeupCalls >= 1);

    RoboCheckEqInt(WindowEventLoopRequestStop(), RoboOk);
    RoboCheckEqInt(WindowEventLoopIsRunning(), 0);
    RoboCheckEqInt(WindowEventLoopRunOnce(RoboWindowEventWaitForever),
                      RoboOk);
    FixtureEnd();
}

static void TestReentrancy(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;

    fixture();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    drain();

    gLm.nestPollOnProcess = 1;
    RoboCheckEqInt(WindowEventLoopPoll(), RoboOk);

    /* Destroy from "handler" after close inject. */
    InjPush(INJ_CLOSE, id, 0, 0, 0, 0);
    RoboCheckEqInt(WindowEventLoopPoll(), RoboOk);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheckEqInt(WindowObserveSize(id, 10, 10), RoboNotFound);
    RoboCheckEqInt(WindowObservePosition(id, 1, 1), RoboNotFound);
    FixtureEnd();
}

static void TestThreading(void)
{
    fixture();
    RoboCheckEqInt(WindowThreadBindOwner(1), RoboOk);
    RoboCheckEqInt(WindowThreadEnter(2), RoboOk);
    RoboCheckEqInt(WindowEventLoopPoll(), RoboPermissionDenied);
    RoboCheckEqInt(WindowEventLoopStart(), RoboPermissionDenied);
    RoboCheckEqInt(WindowThreadEnter(1), RoboOk);
    RoboCheckEqInt(WindowEventLoopPoll(), RoboOk);
    FixtureEnd();
}

static void TestBackendFailures(void)
{
    fixture();
    gLm.failEvents = 1;
    RoboCheckEqInt(WindowEventLoopPoll(), RoboIoError);
    gLm.failEvents = 0;

    gLm.failWait = 1;
    RoboCheckEqInt(WindowEventLoopRunOnce(10), RoboIoError);
    gLm.failWait = 0;

    gLm.failWakeup = 1;
    RoboCheckEqInt(WindowEventLoopWakeup(), RoboIoError);
    gLm.failWakeup = 0;

    gLm.failInit = 1;
    RoboCheckEqInt(WindowBackendRegister(&gLmVtbl, &gLm),
                      RoboInitError);
    FixtureEnd();
}

static void TestDirectVsQueued(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    WindowEvent ev;

    fixture();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    drain();

    /* Direct observe (no ProcessEvents). */
    RoboCheckEqInt(WindowObservePosition(id, 5, 6), RoboOk);
    RoboCheck(PollFind(RoboWindowEventMoved, id, &ev));
    RoboCheckEqInt(ev.x, 5);
    RoboCheckEqInt(ev.y, 6);

    /* Queued via backend ProcessEvents. */
    InjPush(INJ_MOVE, id, 7, 8, 0, 0);
    RoboCheckEqInt(WindowProcessEvents(), RoboOk);
    RoboCheck(PollFind(RoboWindowEventMoved, id, &ev));
    RoboCheckEqInt(ev.x, 7);

    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    FixtureEnd();
}

void TestRegisterWindowEventLoop(void)
{
    TestRegister("window.loop.queue", TestQueueBasics);
    TestRegister("window.loop.order", TestQueueOrderingMulti);
    TestRegister("window.loop.target", TestTargeting);
    TestRegister("window.loop.close", TestClosePath);
    TestRegister("window.loop.state", TestResizeMoveVisibilityFocus);
    TestRegister("window.loop.controls", TestLoopControls);
    TestRegister("window.loop.reentrancy", TestReentrancy);
    TestRegister("window.loop.thread", TestThreading);
    TestRegister("window.loop.backend_fail", TestBackendFailures);
    TestRegister("window.loop.direct_queued", TestDirectVsQueued);
}
