/*
 * RoboWindow keyboard foundation suite (Goal 7.10).
 * Headless mock injection — no GUI required.
 */

#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"

#include <string.h>

typedef struct kbMock {
    int initCount;
} KbMock;

static KbMock gKm;
static WindowBackendCaps gKmCaps;
static WindowBackendVtbl gKmVtbl;

static Result KmInit(void *ud)
{
    KbMock *m = (KbMock *)ud;
    m->initCount++;
    return RoboOk;
}

static void KmShutdown(void *ud)
{
    (void)ud;
}

static Result KmCreate(void *ud, WindowId id,
                               const WindowConfig *cfg,
                               WindowNativeHandle *out)
{
    (void)ud;
    (void)id;
    (void)cfg;
    WindowNativeHandleClear(out);
    return RoboOk;
}

static Result KmDestroy(void *ud, WindowId id,
                                WindowNativeHandle *n)
{
    (void)ud;
    (void)id;
    if (n) {
        WindowNativeHandleInvalidate(n);
    }
    return RoboOk;
}

static Result KmOk(void *ud, WindowId id,
                           WindowNativeHandle *n)
{
    (void)ud;
    (void)id;
    (void)n;
    return RoboOk;
}

static Result KmTitle(void *ud, WindowId id,
                              WindowNativeHandle *n, const char *t)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)t;
    return RoboOk;
}

static Result KmSize(void *ud, WindowId id,
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

static Result KmCons(void *ud, WindowId id,
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

static Result KmPos(void *ud, WindowId id,
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

static Result KmPresent(void *ud, WindowId id,
                                WindowNativeHandle *n,
                                GfxSurface *s)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)s;
    return RoboNotSupported;
}

static Result KmEvents(void *ud)
{
    (void)ud;
    return RoboOk;
}

static void KmPrepare(void)
{
    memset(&gKmCaps, 0, sizeof(gKmCaps));
    gKmCaps.RoboCreateWindow = 1;
    gKmCaps.RoboDestroyWindow = 1;
    gKmCaps.showHide = 1;
    gKmCaps.resize = 1;
    gKmCaps.move = 1;
    gKmCaps.SetTitle = 1;
    gKmCaps.focus = 1;
    gKmCaps.ProcessEvents = 1;
    gKmCaps.sizeConstraints = 1;

    memset(&gKmVtbl, 0, sizeof(gKmVtbl));
    gKmVtbl.name = "kb_mock";
    gKmVtbl.capabilities = &gKmCaps;
    gKmVtbl.initialize = KmInit;
    gKmVtbl.shutdown = KmShutdown;
    gKmVtbl.RoboCreateWindow = KmCreate;
    gKmVtbl.RoboDestroyWindow = KmDestroy;
    gKmVtbl.show = KmOk;
    gKmVtbl.hide = KmOk;
    gKmVtbl.SetTitle = KmTitle;
    gKmVtbl.SetSize = KmSize;
    gKmVtbl.SetSizeConstraints = KmCons;
    gKmVtbl.SetPosition = KmPos;
    gKmVtbl.focus = KmOk;
    gKmVtbl.PresentSurface = KmPresent;
    gKmVtbl.ProcessEvents = KmEvents;
}

static void drain(void)
{
    WindowEvent ev;
    while (WindowEventPoll(&ev) == RoboOk) {
    }
}

static void fixture(void)
{
    memset(&gKm, 0, sizeof(gKm));
    KmPrepare();
    WindowShutdown();
    RoboCheckEqInt(WindowInit(), RoboOk);
    RoboCheckEqInt(WindowBackendRegister(&gKmVtbl, &gKm), RoboOk);
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

static void InjectDown(WindowId id, PhysicalKey phys,
                        LogicalKey log, KeyModifierFlags mods,
                        int repeat)
{
    KeyboardEvent ke;
    KeyboardEventInit(&ke);
    ke.action = RoboKeyActionDown;
    ke.physicalKey = phys;
    ke.logicalKey = log;
    ke.modifiers = mods;
    ke.isRepeat = repeat;
    ke.timestampMs = 1234u;
    ke.windowId = id;
    RoboCheckEqInt(WindowObserveKey(id, &ke), RoboOk);
}

static void InjectUp(WindowId id, PhysicalKey phys,
                      LogicalKey log, KeyModifierFlags mods)
{
    KeyboardEvent ke;
    KeyboardEventInit(&ke);
    ke.action = RoboKeyActionUp;
    ke.physicalKey = phys;
    ke.logicalKey = log;
    ke.modifiers = mods;
    ke.windowId = id;
    RoboCheckEqInt(WindowObserveKey(id, &ke), RoboOk);
}

static void TestKeyModel(void)
{
    RoboCheck(PhysicalKeyIsKnown(RoboPhysicalKeyA));
    RoboCheck(LogicalKeyIsKnown(RoboLogicalKeyEnter));
    RoboCheck(!PhysicalKeyIsKnown(RoboPhysicalKeyUnknown));
    RoboCheck(!LogicalKeyIsKnown(RoboLogicalKeyUnknown));
    RoboCheckStrEq(PhysicalKeyName(RoboPhysicalKeySpace), "Space");
    RoboCheckStrEq(LogicalKeyName(RoboLogicalKeyEscape), "Escape");
    RoboCheckStrEq(KeyActionName(RoboKeyActionDown), "down");
    /* Physical A vs logical A are distinct enum namespaces (same ordinal OK). */
    RoboCheck(RoboPhysicalKeyA != RoboPhysicalKeyUnknown);
    RoboCheck(RoboLogicalKeyA != RoboLogicalKeyUnknown);
}

static void TestPressReleaseOrder(void)
{
    WindowId id;
    WindowEvent ev;

    fixture();
    id = MakeFocused();

    InjectDown(id, RoboPhysicalKeyA, RoboLogicalKeyA, RoboKeyModNone,
                0);
    InjectDown(id, RoboPhysicalKeyA, RoboLogicalKeyA, RoboKeyModNone,
                1);
    InjectUp(id, RoboPhysicalKeyA, RoboLogicalKeyA, RoboKeyModNone);

    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventKeyDown);
    RoboCheckEqInt((int)ev.key.physicalKey, (int)RoboPhysicalKeyA);
    RoboCheckEqInt(ev.key.isRepeat, 0);
    RoboCheckEqInt((int)ev.key.timestampMs, 1234);

    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventKeyDown);
    RoboCheckEqInt(ev.key.isRepeat, 1);

    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.type, (int)RoboWindowEventKeyUp);

    RoboCheck(KeyboardIsPhysicalPressed(RoboPhysicalKeyA) == 0);
    FixtureEnd();
}

static void TestModifiers(void)
{
    WindowId id;

    fixture();
    id = MakeFocused();

    InjectDown(id, RoboPhysicalKeyShiftLeft, RoboLogicalKeyShiftLeft,
                RoboKeyModShift | RoboKeyModShiftLeft |
                    RoboKeyModCapsLock,
                0);
    RoboCheck(KeyboardModifierActive(RoboKeyModShift));
    RoboCheck(KeyboardModifierActive(RoboKeyModShiftLeft));
    RoboCheck(KeyboardModifierActive(RoboKeyModCapsLock));
    RoboCheck(KeyboardIsPhysicalPressed(RoboPhysicalKeyShiftLeft));

    InjectUp(id, RoboPhysicalKeyShiftLeft, RoboLogicalKeyShiftLeft,
              RoboKeyModCapsLock);
    RoboCheck(!KeyboardModifierActive(RoboKeyModShift));
    RoboCheck(KeyboardModifierActive(RoboKeyModCapsLock));

    InjectDown(id, RoboPhysicalKeyControlLeft, RoboLogicalKeyControlLeft,
                RoboKeyModCtrl | RoboKeyModCtrlLeft | RoboKeyModAlt |
                    RoboKeyModAltLeft | RoboKeyModMeta |
                    RoboKeyModMetaLeft | RoboKeyModNumLock |
                    RoboKeyModScrollLock,
                0);
    RoboCheck(KeyboardModifierActive(RoboKeyModCtrl));
    RoboCheck(KeyboardModifierActive(RoboKeyModAlt));
    RoboCheck(KeyboardModifierActive(RoboKeyModMeta));
    RoboCheck(KeyboardModifierActive(RoboKeyModNumLock));
    RoboCheck(KeyboardModifierActive(RoboKeyModScrollLock));
    FixtureEnd();
}

static void TestTargeting(void)
{
    WindowId a;
    WindowId b;
    WindowConfig cfg;
    KeyboardEvent ke;
    WindowEvent ev;

    fixture();
    WindowConfigInit(&cfg);
    cfg.visible = 0;
    RoboCheckEqInt(WindowCreate(&cfg, &a), RoboOk);
    RoboCheckEqInt(WindowCreate(&cfg, &b), RoboOk);
    drain();

    /* No focused window + NONE target → NOT_FOUND (state may still update). */
    KeyboardEventInit(&ke);
    ke.action = RoboKeyActionDown;
    ke.physicalKey = RoboPhysicalKeyB;
    ke.logicalKey = RoboLogicalKeyB;
    RoboCheckEqInt(WindowObserveKey(RoboWindowIdNone, &ke),
                      RoboNotFound);

    RoboCheckEqInt(WindowObserveFocus(a, 1), RoboOk);
    drain();
    InjectDown(RoboWindowIdNone, RoboPhysicalKeyC, RoboLogicalKeyC,
                RoboKeyModNone, 0);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheck(ev.windowId == a);

    /* Focus change */
    RoboCheckEqInt(WindowObserveFocus(a, 0), RoboOk);
    RoboCheckEqInt(WindowObserveFocus(b, 1), RoboOk);
    drain();
    InjectDown(RoboWindowIdNone, RoboPhysicalKeyD, RoboLogicalKeyD,
                RoboKeyModNone, 0);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheck(ev.windowId == b);

    /* Destroyed / stale */
    RoboCheckEqInt(WindowDestroy(b), RoboOk);
    drain();
    RoboCheckEqInt(WindowObserveKey(b, &ke), RoboNotFound);
    RoboCheckEqInt(
        WindowObserveKey((WindowId)99999u, &ke), RoboNotFound);

    RoboCheckEqInt(WindowDestroy(a), RoboOk);
    FixtureEnd();
}

static void TestStuckKeyReset(void)
{
    WindowId id;

    fixture();
    id = MakeFocused();
    InjectDown(id, RoboPhysicalKeyE, RoboLogicalKeyE, RoboKeyModNone,
                0);
    RoboCheck(KeyboardIsPhysicalPressed(RoboPhysicalKeyE));

    RoboCheckEqInt(WindowObserveFocus(id, 0), RoboOk);
    RoboCheck(!KeyboardIsPhysicalPressed(RoboPhysicalKeyE));

    RoboCheckEqInt(WindowObserveFocus(id, 1), RoboOk);
    InjectDown(id, RoboPhysicalKeyF, RoboLogicalKeyF, RoboKeyModNone,
                0);
    RoboCheckEqInt(WindowDestroy(id), RoboOk);
    RoboCheck(!KeyboardIsPhysicalPressed(RoboPhysicalKeyF));

    fixture();
    id = MakeFocused();
    InjectDown(id, RoboPhysicalKeyG, RoboLogicalKeyG, RoboKeyModNone,
                0);
    FixtureEnd();
    RoboCheck(!KeyboardIsPhysicalPressed(RoboPhysicalKeyG));
}

static void TestUnknownAndErrors(void)
{
    WindowId id;
    KeyboardEvent ke;

    fixture();
    id = MakeFocused();

    KeyboardEventInit(&ke);
    ke.action = RoboKeyActionDown;
    ke.physicalKey = RoboPhysicalKeyUnknown;
    ke.logicalKey = RoboLogicalKeyUnknown;
    RoboCheckEqInt(WindowObserveKey(id, &ke), RoboOk);

    ke.action = RoboKeyActionNone;
    ke.physicalKey = RoboPhysicalKeyA;
    ke.logicalKey = RoboLogicalKeyA;
    RoboCheckEqInt(WindowObserveKey(id, &ke), RoboInvalidArgument);

    RoboCheckEqInt(WindowObserveKey(id, NULL), RoboInvalidArgument);

    RoboCheckEqInt(WindowThreadBindOwner(1), RoboOk);
    RoboCheckEqInt(WindowThreadEnter(2), RoboOk);
    ke.action = RoboKeyActionDown;
    RoboCheckEqInt(WindowObserveKey(id, &ke), RoboPermissionDenied);
    FixtureEnd();
}

static void TestPhysLogicalDistinction(void)
{
    WindowId id;
    WindowEvent ev;

    fixture();
    id = MakeFocused();
    /* Physical Q position reporting logical A (layout remap simulation). */
    InjectDown(id, RoboPhysicalKeyQ, RoboLogicalKeyA, RoboKeyModNone,
                0);
    RoboCheckEqInt(WindowEventPoll(&ev), RoboOk);
    RoboCheckEqInt((int)ev.key.physicalKey, (int)RoboPhysicalKeyQ);
    RoboCheckEqInt((int)ev.key.logicalKey, (int)RoboLogicalKeyA);
    RoboCheck(KeyboardIsPhysicalPressed(RoboPhysicalKeyQ));
    RoboCheck(KeyboardIsLogicalPressed(RoboLogicalKeyA));
    FixtureEnd();
}

void TestRegisterWindowKeyboard(void)
{
    TestRegister("window.key.model", TestKeyModel);
    TestRegister("window.key.press_release", TestPressReleaseOrder);
    TestRegister("window.key.modifiers", TestModifiers);
    TestRegister("window.key.targeting", TestTargeting);
    TestRegister("window.key.reset", TestStuckKeyReset);
    TestRegister("window.key.errors", TestUnknownAndErrors);
    TestRegister("window.key.phys_logical", TestPhysLogicalDistinction);
}
