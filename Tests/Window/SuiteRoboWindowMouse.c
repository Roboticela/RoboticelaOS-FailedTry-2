/*
 * RoboWindow mouse foundation suite (Goal 7.11).
 * Headless mock injection — no GUI required.
 */

#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"

#include <string.h>

typedef struct mouseMock {
    int initCount;
} MouseMock;

static MouseMock gMm;
static WindowBackendCaps gMmCaps;
static WindowBackendVtbl gMmVtbl;

static Result MmInit(void *ud)
{
    MouseMock *m = (MouseMock *)ud;
    m->initCount++;
    return RoboOk;
}

static void MmShutdown(void *ud)
{
    (void)ud;
}

static Result MmCreate(void *ud, WindowId id,
                               const WindowConfig *cfg,
                               WindowNativeHandle *out)
{
    (void)ud;
    (void)id;
    (void)cfg;
    WindowNativeHandleClear(out);
    return RoboOk;
}

static Result MmDestroy(void *ud, WindowId id,
                                WindowNativeHandle *n)
{
    (void)ud;
    (void)id;
    if (n) {
        WindowNativeHandleInvalidate(n);
    }
    return RoboOk;
}

static Result MmOk(void *ud, WindowId id,
                           WindowNativeHandle *n)
{
    (void)ud;
    (void)id;
    (void)n;
    return RoboOk;
}

static Result MmTitle(void *ud, WindowId id,
                              WindowNativeHandle *n, const char *t)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)t;
    return RoboOk;
}

static Result MmSize(void *ud, WindowId id,
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

static Result MmCons(void *ud, WindowId id,
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

static Result MmPos(void *ud, WindowId id,
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

static Result MmPresent(void *ud, WindowId id,
                                WindowNativeHandle *n,
                                GfxSurface *s)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)s;
    return RoboNotSupported;
}

static Result MmEvents(void *ud)
{
    (void)ud;
    return RoboOk;
}

static void MmPrepare(void)
{
    memset(&gMmCaps, 0, sizeof(gMmCaps));
    gMmCaps.RoboCreateWindow = 1;
    gMmCaps.RoboDestroyWindow = 1;
    gMmCaps.showHide = 1;
    gMmCaps.resize = 1;
    gMmCaps.move = 1;
    gMmCaps.SetTitle = 1;
    gMmCaps.focus = 1;
    gMmCaps.ProcessEvents = 1;
    gMmCaps.sizeConstraints = 1;

    memset(&gMmVtbl, 0, sizeof(gMmVtbl));
    gMmVtbl.name = "mouse_mock";
    gMmVtbl.capabilities = &gMmCaps;
    gMmVtbl.initialize = MmInit;
    gMmVtbl.shutdown = MmShutdown;
    gMmVtbl.RoboCreateWindow = MmCreate;
    gMmVtbl.RoboDestroyWindow = MmDestroy;
    gMmVtbl.show = MmOk;
    gMmVtbl.hide = MmOk;
    gMmVtbl.SetTitle = MmTitle;
    gMmVtbl.SetSize = MmSize;
    gMmVtbl.SetSizeConstraints = MmCons;
    gMmVtbl.SetPosition = MmPos;
    gMmVtbl.focus = MmOk;
    gMmVtbl.PresentSurface = MmPresent;
    gMmVtbl.ProcessEvents = MmEvents;
}

static void drain(void)
{
    WindowEvent ev;
    while (WindowEventPoll(&ev) == RoboOk) {
    }
}

static void fixture(void)
{
    memset(&gMm, 0, sizeof(gMm));
    MmPrepare();
    WindowShutdown();
    RoboCheckEqInt(WindowInit(), RoboOk);
    RoboCheckEqInt(WindowBackendRegister(&gMmVtbl, &gMm), RoboOk);
    drain();
}

static void FixtureEnd(void)
{
    WindowShutdown();
}

static WindowId MakeFocused(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    drain();
    RoboCheckEqInt(WindowObserveFocus(id, 1), RoboOk);
    drain();
    return id;
}

static void InjectMove(WindowId id, int32_t x, int32_t y,
                        KeyModifierFlags mods)
{
    MouseEvent me;
    MouseEventInit(&me);
    me.type = RoboMouseEventMove;
    me.clientX = x;
    me.clientY = y;
    me.screenX = x + 10;
    me.screenY = y + 20;
    me.hasScreenCoords = 1;
    me.modifiers = mods;
    me.timestampMs = 1000u;
    RoboCheckEqInt(WindowObserveMouse(id, &me), RoboOk);
}

static void InjectButton(WindowId id, MouseEventType type,
                          MouseButton btn, int32_t x, int32_t y,
                          int clickCount, KeyModifierFlags mods,
                          uint64_t ts)
{
    MouseEvent me;
    MouseEventInit(&me);
    me.type = type;
    me.button = btn;
    me.clientX = x;
    me.clientY = y;
    me.hasScreenCoords = 0;
    me.clickCount = clickCount;
    me.modifiers = mods;
    me.timestampMs = ts;
    RoboCheckEqInt(WindowObserveMouse(id, &me), RoboOk);
}

static void InjectWheel(WindowId id, int32_t sx, int32_t sy,
                         KeyModifierFlags mods)
{
    MouseEvent me;
    MouseEventInit(&me);
    me.type = RoboMouseEventWheel;
    me.clientX = 5;
    me.clientY = 5;
    me.scrollX = sx;
    me.scrollY = sy;
    me.scrollUnit = RoboMouseScrollUnitLine;
    me.modifiers = mods;
    RoboCheckEqInt(WindowObserveMouse(id, &me), RoboOk);
}

static void TestButtonModel(void)
{
    RoboCheck(MouseButtonIsKnown(RoboMouseButtonLeft));
    RoboCheck(MouseButtonIsKnown(RoboMouseButtonRight));
    RoboCheck(MouseButtonIsKnown(RoboMouseButtonMiddle));
    RoboCheck(MouseButtonIsKnown(RoboMouseButtonX1));
    RoboCheck(MouseButtonIsKnown(RoboMouseButtonX2));
    RoboCheck(!MouseButtonIsKnown(RoboMouseButtonNone));
    RoboCheck(!MouseButtonIsKnown(RoboMouseButtonUnknown));
    RoboCheckStrEq(MouseButtonName(RoboMouseButtonLeft), "left");
    RoboCheckStrEq(MouseEventTypeName(RoboMouseEventWheel),
                      "wheel");
}

static void TestMoveAndDelta(void)
{
    WindowId id;
    WindowEvent ev;
    MousePosition pos;

    fixture();
    id = MakeFocused();

    InjectMove(id, 10, 20, RoboKeyModNone);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventMouseMove);
    RoboCheckEqInt(ev.mouse.clientX, 10);
    RoboCheckEqInt(ev.mouse.clientY, 20);
    RoboCheckEqInt(ev.mouse.deltaX, 0);
    RoboCheckEqInt(ev.mouse.deltaY, 0);
    RoboCheck(ev.mouse.hasScreenCoords);
    RoboCheck(MouseGetPosition(&pos));
    RoboCheckEqInt(pos.x, 10);
    RoboCheckEqInt(pos.y, 20);

    InjectMove(id, 15, 28, RoboKeyModShift);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt(ev.mouse.deltaX, 5);
    RoboCheckEqInt(ev.mouse.deltaY, 8);
    RoboCheck(ev.mouse.modifiers & RoboKeyModShift);

    /* Explicit delta from injector is preserved when non-zero. */
    {
        MouseEvent me;
        MouseEventInit(&me);
        me.type = RoboMouseEventMove;
        me.clientX = 100;
        me.clientY = 100;
        me.deltaX = 3;
        me.deltaY = -1;
        RoboCheckEqInt(WindowObserveMouse(id, &me), RoboOk);
        RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
        RoboCheckEqInt(ev.mouse.deltaX, 3);
        RoboCheckEqInt(ev.mouse.deltaY, -1);
    }

    FixtureEnd();
}

static void TestButtonsAndState(void)
{
    WindowId id;
    WindowEvent ev;

    fixture();
    id = MakeFocused();

    InjectButton(id, RoboMouseEventButtonDown, RoboMouseButtonLeft, 1, 1,
                  1, RoboKeyModCtrl, 10u);
    InjectButton(id, RoboMouseEventButtonDown, RoboMouseButtonRight, 1, 1,
                  1, RoboKeyModCtrl, 11u);
    InjectButton(id, RoboMouseEventButtonDown, RoboMouseButtonMiddle, 1,
                  1, 1, RoboKeyModNone, 12u);
    InjectButton(id, RoboMouseEventButtonDown, RoboMouseButtonX1, 1, 1, 1,
                  RoboKeyModNone, 13u);
    InjectButton(id, RoboMouseEventButtonDown, RoboMouseButtonX2, 1, 1, 1,
                  RoboKeyModNone, 14u);

    RoboCheck(MouseIsButtonPressed(RoboMouseButtonLeft));
    RoboCheck(MouseIsButtonPressed(RoboMouseButtonRight));
    RoboCheck(MouseIsButtonPressed(RoboMouseButtonMiddle));
    RoboCheck(MouseIsButtonPressed(RoboMouseButtonX1));
    RoboCheck(MouseIsButtonPressed(RoboMouseButtonX2));

    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventMouseButtonDown);
    RoboCheckEqInt((int)ev.mouse.button, (int)RoboMouseButtonLeft);
    RoboCheck(ev.mouse.modifiers & RoboKeyModCtrl);

    InjectButton(id, RoboMouseEventButtonUp, RoboMouseButtonLeft, 1, 1, 0,
                  RoboKeyModNone, 20u);
    RoboCheck(!MouseIsButtonPressed(RoboMouseButtonLeft));
    RoboCheck(MouseIsButtonPressed(RoboMouseButtonRight));

    FixtureEnd();
}

static void TestUnknownButton(void)
{
    WindowId id;
    MouseEvent me;
    WindowEvent ev;

    fixture();
    id = MakeFocused();

    MouseEventInit(&me);
    me.type = RoboMouseEventButtonDown;
    me.button = RoboMouseButtonUnknown;
    me.clientX = 0;
    me.clientY = 0;
    RoboCheckEqInt(WindowObserveMouse(id, &me), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboNotFound);
    RoboCheck(!MouseIsButtonPressed(RoboMouseButtonUnknown));

    FixtureEnd();
}

static void TestClickCount(void)
{
    WindowId id;
    WindowEvent ev;

    fixture();
    id = MakeFocused();

    InjectButton(id, RoboMouseEventButtonDown, RoboMouseButtonLeft, 50,
                  50, 0, RoboKeyModNone, 100u);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt(ev.mouse.clickCount, 1);

    InjectButton(id, RoboMouseEventButtonUp, RoboMouseButtonLeft, 50, 50,
                  0, RoboKeyModNone, 110u);
    drain();

    InjectButton(id, RoboMouseEventButtonDown, RoboMouseButtonLeft, 51,
                  50, 0, RoboKeyModNone, 200u);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt(ev.mouse.clickCount, 2);

    /* Native double-click count preserved when provided. */
    InjectButton(id, RoboMouseEventButtonDown, RoboMouseButtonRight, 0, 0,
                  2, RoboKeyModNone, 300u);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt(ev.mouse.clickCount, 2);

    FixtureEnd();
}

static void TestWheel(void)
{
    WindowId id;
    WindowEvent ev;
    MouseEvent me;

    fixture();
    id = MakeFocused();

    InjectWheel(id, 0, 120, RoboKeyModShift);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventMouseWheel);
    RoboCheckEqInt(ev.mouse.scrollY, 120);
    RoboCheckEqInt(ev.mouse.scrollX, 0);
    RoboCheck(ev.mouse.modifiers & RoboKeyModShift);

    InjectWheel(id, -60, 0, RoboKeyModNone);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt(ev.mouse.scrollX, -60);

    /* High-resolution fraction of a notch. */
    InjectWheel(id, 0, 30, RoboKeyModNone);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt(ev.mouse.scrollY, 30);

    /* Zero delta ignored. */
    MouseEventInit(&me);
    me.type = RoboMouseEventWheel;
    me.scrollX = 0;
    me.scrollY = 0;
    RoboCheckEqInt(WindowObserveMouse(id, &me), RoboOk);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboNotFound);

    FixtureEnd();
}

static void TestTargetingAndCapture(void)
{
    WindowId a;
    WindowId b;
    WindowConfig cfg;
    MouseEvent me;
    WindowEvent ev;

    fixture();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &a), RoboOk);
    RoboCheckEqInt(WindowCreate(&cfg, &b), RoboOk);
    drain();

    MouseEventInit(&me);
    me.type = RoboMouseEventMove;
    me.clientX = 1;
    me.clientY = 1;
    RoboCheckEqInt(WindowObserveMouse(RoboWindowIdNone, &me),
                      RoboNotFound);

    RoboCheckEqInt(WindowObserveFocus(a, 1), RoboOk);
    drain();
    InjectMove(RoboWindowIdNone, 2, 3, RoboKeyModNone);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheck(ev.windowId == a);

    /* Soft capture redirects NONE targeting even without native caps. */
    RoboCheckEqInt(WindowMouseCapture(b), RoboOk);
    RoboCheck(WindowMouseCapturedId() == b);
    InjectMove(RoboWindowIdNone, 4, 5, RoboKeyModNone);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheck(ev.windowId == b);

    RoboCheckEqInt(WindowMouseReleaseCapture(), RoboOk);
    RoboCheck(WindowMouseCapturedId() == RoboWindowIdNone);

    RoboCheckEqInt(WindowDestroy(b), RoboOk);
    drain();
    RoboCheckEqInt(WindowObserveMouse(b, &me), RoboNotFound);
    RoboCheckEqInt(
        WindowObserveMouse((WindowId)99999u, &me),
        RoboNotFound);

    /* Resize does not invalidate subsequent mouse delivery. */
    RoboCheckEqInt(WindowObserveSize(a, 640, 480), RoboOk);
    drain();
    InjectMove(a, 9, 9, RoboKeyModNone);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheck(ev.windowId == a);

    RoboCheckEqInt(WindowDestroy(a), RoboOk);
    FixtureEnd();
}

static void TestStuckButtonReset(void)
{
    WindowId id;

    fixture();
    id = MakeFocused();
    InjectButton(id, RoboMouseEventButtonDown, RoboMouseButtonLeft, 0, 0,
                  1, RoboKeyModNone, 1u);
    RoboCheck(MouseIsButtonPressed(RoboMouseButtonLeft));

    RoboCheckEqInt(WindowObserveFocus(id, 0), RoboOk);
    RoboCheck(!MouseIsButtonPressed(RoboMouseButtonLeft));

    RoboCheckEqInt(WindowObserveFocus(id, 1), RoboOk);
    InjectButton(id, RoboMouseEventButtonDown, RoboMouseButtonRight, 0,
                  0, 1, RoboKeyModNone, 2u);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheck(!MouseIsButtonPressed(RoboMouseButtonRight));

    fixture();
    id = MakeFocused();
    InjectButton(id, RoboMouseEventButtonDown, RoboMouseButtonMiddle, 0,
                  0, 1, RoboKeyModNone, 3u);
    FixtureEnd();
    RoboCheck(!MouseIsButtonPressed(RoboMouseButtonMiddle));
}

static void TestMalformed(void)
{
    WindowId id;
    MouseEvent me;

    fixture();
    id = MakeFocused();

    RoboCheckEqInt(WindowObserveMouse(id, NULL),
                      RoboInvalidArgument);
    MouseEventInit(&me);
    me.type = RoboMouseEventNone;
    RoboCheckEqInt(WindowObserveMouse(id, &me),
                      RoboInvalidArgument);

    FixtureEnd();
}

static void TestDpiCoordSemantics(void)
{
    WindowId id;
    WindowEvent ev;
    float scale = 0.0f;
    GfxSize phys;

    fixture();
    id = MakeFocused();
    RoboCheckEqInt(WindowObserveSize(id, 200, 100), RoboOk);
    drain();
    RoboCheckEqInt(WindowGetScale(id, &scale), RoboOk);
    RoboCheck(scale > 0.0f);

    InjectMove(id, 100, 50, RoboKeyModNone);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    /* Client coords remain logical DIPs; physical size uses scale separately. */
    RoboCheckEqInt(ev.mouse.clientX, 100);
    RoboCheckEqInt(WindowGetSizePhysical(id, &phys), RoboOk);
    RoboCheck(phys.width > 0);
    RoboCheck(phys.height > 0);

    FixtureEnd();
}

void TestRegisterWindowMouse(void)
{
    TestRegister("window.mouse.button_model", TestButtonModel);
    TestRegister("window.mouse.move_delta", TestMoveAndDelta);
    TestRegister("window.mouse.buttons_state", TestButtonsAndState);
    TestRegister("window.mouse.unknown_button", TestUnknownButton);
    TestRegister("window.mouse.click_count", TestClickCount);
    TestRegister("window.mouse.wheel", TestWheel);
    TestRegister("window.mouse.targeting_capture",
                       TestTargetingAndCapture);
    TestRegister("window.mouse.stuck_reset", TestStuckButtonReset);
    TestRegister("window.mouse.malformed", TestMalformed);
    TestRegister("window.mouse.dpi_coords", TestDpiCoordSemantics);
}
