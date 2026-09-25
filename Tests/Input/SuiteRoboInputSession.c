/*
 * RoboInput session suite (Phase 13.44 / 13.78) — `input.session.*`.
 *
 * Six cases about one claim: **after a device goes away, or after a reset, nothing stale remains** -- and the
 * suite's job is to make that claim checkable rather than rhetorical.
 *
 *   `sweep`      the record itself: an empty sweep, the "did anything go?" question, the line it prints, and
 *                the refusal of a buffer one byte short;
 *
 *   `device`     the connect/disconnect pair. A device that holds keys, pointers, captures and contacts is
 *                dismantled *by device*: its keys go, its pointers go, the captures those pointers held go,
 *                their hovers and click sequences go, its contacts go -- while **another** device's state is
 *                untouched, which is the half of the claim that a careless sweep fails;
 *
 *   `transient`  what a disconnect does **not** do and a reset does: a pending accent and a running drag are
 *                cancelled by both, while focus survives the reset -- the one deliberate exception, and the
 *                reason it is deliberate;
 *
 *   `target`     a destroyed target loses its focus and its captures by identity, and the count says so;
 *
 *   `window`     a deactivation releases the captures held in that window and the focus it had, and cancels
 *                a drag that was happening *in* it -- but leaves a drag that merely passed through alone;
 *
 *   `dump`       the session's line, which is the state a reviewer reads when a sweep count is unexpected.
 */

#include "RoboInputTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

static InputDeviceDescriptor SessionKeyboardDevice(InputDeviceId id)
{
    return InputFixtureDescriptor(RoboInputDeviceTypeKeyboard, id, "kbd",
                                  RoboInputDeviceCapabilityKeyboardKeys);
}

static InputDeviceDescriptor SessionMouseDevice(InputDeviceId id)
{
    return InputFixtureDescriptor(RoboInputDeviceTypeMouse, id, "mouse",
                                  RoboInputDeviceCapabilityPointer | RoboInputDeviceCapabilityButtons);
}

static InputTarget SessionNodeTarget(WindowId window, LayoutNodeId node)
{
    InputTarget target;
    InputDiag diag;

    InputTargetInit(&target);
    if (InputTargetSetDocument(&target, window, NULL, node, 0u, RoboInputSpaceWindow, &diag) != RoboOk) {
        RoboCheckMsg(0, "the fixture's document target was refused");
    }
    return target;
}

static InputTarget SessionWindowTarget(WindowId window)
{
    InputTarget target;
    InputDiag diag;

    InputTargetInit(&target);
    if (InputTargetSetWindow(&target, window, RoboInputSpaceWindow, &diag) != RoboOk) {
        RoboCheckMsg(0, "the fixture's window target was refused");
    }
    return target;
}

/* Press @p physical as a device's key, through the public payload door. */
static void SessionPressKey(InputSession *session, InputDeviceId device, PhysicalKey physical, uint64_t atMs)
{
    InputEvent event = InputKeyEvent(physical, RoboLogicalKeyA, 1, RoboInputModifiersNone);
    InputDiag diag;

    InputEventSetTimestamp(&event, atMs);
    event.device.id = device;
    if (InputKeyboardStateApply(&session->keyboard, &event, &diag) != RoboOk) {
        RoboCheckMsg(0, "the fixture's key was not applied");
    }
}

/* A pointer of @p device at a point, with the primary button down, in @p space. */
static void SessionHoldPointerIn(InputSession *session,
                                 InputDeviceId device,
                                 InputPointerId pointer,
                                 InputSpace space)
{
    InputPointerData data = GetRoboInputPointerData(10, 20, RoboMouseButtonLeft,
                                                       InputButtonMaskApply(0u, RoboMouseButtonLeft, 1));
    InputDiag diag;

    data.pointer = pointer;
    if (InputPointerStateApply(&session->pointers, device, RoboInputDeviceTypeMouse, &data, space,
                               RoboInputModifiersNone, NULL, &diag) != RoboOk) {
        RoboCheckMsg(0, "the fixture's pointer was not applied");
    }
}

static void SessionHoldPointer(InputSession *session, InputDeviceId device, InputPointerId pointer)
{
    SessionHoldPointerIn(session, device, pointer, RoboInputSpaceWindow);
}

/* A built-in layout, so a pending accent is one the pipeline could actually have armed. */
static InputLayout SessionLayout(void)
{
    InputLayout layout;
    InputDiag diag;

    InputLayoutInit(&layout);
    if (InputLayoutCreateBuiltin(&layout, RoboInputLayoutUsAscii, &diag) != RoboOk) {
        RoboCheckMsg(0, "the fixture's layout was refused");
    }
    return layout;
}

/* A touch contact of @p device, begun. */
static void SessionBeginTouch(InputSession *session, InputDeviceId device, uint32_t contactId)
{
    InputDiag diag;

    if (InputTouchStateApply(&session->touches, contactId, RoboInputTouchPhaseBegin, MakeGfxPoint(5, 6), device,
                             RoboInputSpaceWindow, 0.5f, 0.0f, 0, 1u, NULL, &diag) != RoboOk) {
        RoboCheckMsg(0, "the fixture's contact was not applied");
    }
}

/* A session with a keyboard and a mouse, both holding state. */
typedef struct SessionFixture {
    InputSession session;
    InputDeviceId keyboard;
    InputDeviceId mouse;
} SessionFixture;

/* Make @p window the active scope: a focus request needs a live scope to land in (13.25/13.28). */
static void SessionActivate(SessionFixture *fixture, WindowId window)
{
    InputDiag diag;

    if (InputFocusManagerSetActiveWindow(&fixture->session.focus, window, RoboInputApplicationIdNone, NULL,
                                         &diag) != RoboOk) {
        RoboCheckMsg(0, "the fixture's activation was refused");
    }
}

static void SessionBuild(SessionFixture *fixture)
{
    InputSessionSweep sweep;

    InputSessionInit(&fixture->session);
    fixture->keyboard = RoboInputDeviceIdNone;
    fixture->mouse = RoboInputDeviceIdNone;
    {
        InputDeviceDescriptor keyboard = SessionKeyboardDevice((InputDeviceId)1u);
        InputDeviceDescriptor mouse = SessionMouseDevice((InputDeviceId)2u);

        if (InputSessionConnect(&fixture->session, &keyboard, &fixture->keyboard, NULL) != RoboOk) {
            RoboCheckMsg(0, "the fixture's keyboard was refused");
        }
        if (InputSessionConnect(&fixture->session, &mouse, &fixture->mouse, NULL) != RoboOk) {
            RoboCheckMsg(0, "the fixture's mouse was refused");
        }
    }
    RoboCheckEqInt((int)fixture->keyboard, 1);
    RoboCheckEqInt((int)fixture->mouse, 2);
    RoboCheckEqInt((int)InputSessionDeviceCount(&fixture->session), 2);
    InputSessionSweepInit(&sweep);
    (void)sweep;
}

/* ------------------------------------------------------------------ */
/* The sweep record                                                    */
/* ------------------------------------------------------------------ */

static void TestSessionSweep(void)
{
    InputSessionSweep sweep;
    InputDiag diag;
    char line[512];
    size_t length = 0u;

    InputDiagReset(&diag);
    InputSessionSweepInit(&sweep);
    /* Nothing went, and the record says so: the assertion "no stale state remained" is this question. */
    RoboCheck(InputSessionSweepIsEmpty(&sweep));
    RoboCheckEqInt(sweep.keysReleased, 0);
    RoboCheckEqInt(sweep.pointersRemoved, 0);
    RoboCheckEqInt(sweep.capturesReleased, 0);
    RoboCheckEqInt(sweep.clicksReset, 0);
    RoboCheckEqInt(sweep.hoversCleared, 0);
    RoboCheckEqInt(sweep.touchesRemoved, 0);
    RoboCheckEqInt(sweep.focusReleased, 0);
    RoboCheckEqInt(sweep.deadKeyCancelled, 0);
    RoboCheckEqInt(sweep.repeatStopped, 0);
    RoboCheckEqInt(sweep.dragCancelled, 0);
    RoboCheckEqInt(sweep.gesturesReset, 0);

    RoboCheckEqInt(InputSessionSweepWrite(&sweep, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckEqInt((int)strlen(line), (int)length);
    RoboCheckStrEq(line,
                   "sweep keys=0 pointers=0 captures=0 clicks=0 hovers=0 touches=0 focus=0 accent=0 repeat=0 "
                   "drag=0 gestures=0\n");

    /* One field is enough to make it non-empty: an unemptied sweep is the thing a test looks for. */
    sweep.touchesRemoved = 1u;
    RoboCheck(!InputSessionSweepIsEmpty(&sweep));
    sweep.touchesRemoved = 0u;
    sweep.deadKeyCancelled = 1;
    RoboCheck(!InputSessionSweepIsEmpty(&sweep));

    RoboCheckEqInt(InputSessionSweepWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "sweep <none>\n");
    {
        char small[8];
        size_t needed = 0u;

        RoboCheckEqInt(InputSessionSweepWrite(&sweep, small, sizeof(small), &needed, &diag), RoboNotSupported);
        RoboCheckStrEq(small, "?");
    }
    InputSessionSweepInit(NULL);
    RoboCheck(!InputSessionSweepIsEmpty(NULL));
}

/* ------------------------------------------------------------------ */
/* Device sweep                                                        */
/* ------------------------------------------------------------------ */

static void TestSessionDevice(void)
{
    SessionFixture fixture;
    InputSessionSweep sweep;
    InputDiag diag;
    InputTarget target = SessionNodeTarget((WindowId)7u, (LayoutNodeId)31u);

    InputDiagReset(&diag);
    SessionBuild(&fixture);

    /* Two devices holding state: the keyboard holds two keys, the mouse holds a pointer with a capture, a
     * hover and a click, and both have a contact down. */
    SessionPressKey(&fixture.session, fixture.keyboard, RoboPhysicalKeyA, 0u);
    SessionPressKey(&fixture.session, fixture.keyboard, RoboPhysicalKeyShiftLeft, 0u);
    SessionHoldPointer(&fixture.session, fixture.mouse, (InputPointerId)1u);
    RoboCheckEqInt(InputPointerCaptureSet(&fixture.session.captures, (InputPointerId)1u, &target, &diag),
                   RoboOk);
    RoboCheck(InputHoverSet(&fixture.session.hover, (InputPointerId)1u, &target, NULL, NULL));
    {
        InputClickConfig click = InputClickConfigDefault();

        RoboCheck(InputClickStatePress(&fixture.session.clicks, (InputPointerId)1u, RoboMouseButtonLeft,
                                       MakeGfxPoint(10, 20), 7u, &click) >= 0);
    }
    SessionBeginTouch(&fixture.session, fixture.mouse, 4u);
    SessionBeginTouch(&fixture.session, fixture.keyboard, 5u);

    RoboCheckEqInt((int)InputKeyboardStateHeldCount(&fixture.session.keyboard), 2);
    RoboCheck(!InputSessionIsQuiescent(&fixture.session));

    /* Disconnect the keyboard: keyboard state goes by device, and the mouse is untouched. */
    InputSessionSweepInit(&sweep);
    RoboCheckEqInt(InputSessionDisconnect(&fixture.session, fixture.keyboard, &sweep, &diag), RoboOk);
    RoboCheckEqInt((int)sweep.keysReleased, 2);
    RoboCheckEqInt((int)sweep.touchesRemoved, 1);
    RoboCheckEqInt((int)sweep.pointersRemoved, 0);
    /* The keyboard held no pointer, so nothing of the mouse's was touched -- the half of the claim a
     * careless sweep fails. */
    RoboCheckEqInt((int)sweep.capturesReleased, 0);
    RoboCheckEqInt((int)sweep.hoversCleared, 0);
    RoboCheckEqInt((int)sweep.clicksReset, 0);
    RoboCheckEqInt((int)sweep.focusReleased, 0);
    RoboCheckEqInt((int)InputKeyboardStateHeldCount(&fixture.session.keyboard), 0);
    RoboCheckEqInt((int)fixture.session.pointers.count, 1);
    RoboCheckEqInt((int)fixture.session.captures.count, 1);
    RoboCheckEqInt((int)fixture.session.touches.count, 1);
    RoboCheck(!InputSessionHasDevice(&fixture.session, fixture.keyboard));
    RoboCheck(InputSessionHasDevice(&fixture.session, fixture.mouse));
    RoboCheckEqInt(fixture.session.disconnects, 1);

    /* Disconnect the mouse: its pointer, capture, hover, click and contact all go, and the keyboard's
     * contact that is still down is not touched by a mouse's exit. */
    SessionBeginTouch(&fixture.session, fixture.keyboard, 6u);
    InputSessionSweepInit(&sweep);
    RoboCheckEqInt(InputSessionDisconnect(&fixture.session, fixture.mouse, &sweep, &diag), RoboOk);
    RoboCheckEqInt((int)sweep.pointersRemoved, 1);
    RoboCheckEqInt((int)sweep.capturesReleased, 1);
    RoboCheckEqInt((int)sweep.hoversCleared, 1);
    RoboCheckEqInt((int)sweep.clicksReset, 1);
    RoboCheckEqInt((int)sweep.touchesRemoved, 1);
    RoboCheckEqInt((int)sweep.keysReleased, 0);
    RoboCheckEqInt((int)fixture.session.pointers.count, 0);
    RoboCheckEqInt((int)fixture.session.captures.count, 0);
    RoboCheckEqInt((int)fixture.session.hover.count, 0);
    RoboCheckEqInt((int)fixture.session.touches.count, 1);
    RoboCheckEqInt((int)InputSessionDeviceCount(&fixture.session), 0);

    /* A device the table does not have is refused **without** sweeping: a caller that lost track of its own
     * table hears about it rather than having this layer release a stranger's state. */
    SessionHoldPointer(&fixture.session, (InputDeviceId)9u, (InputPointerId)3u);
    InputSessionSweepInit(&sweep);
    RoboCheckEqInt(InputSessionDisconnect(&fixture.session, (InputDeviceId)9u, &sweep, &diag), RoboNotFound);
    RoboCheck(InputSessionSweepIsEmpty(&sweep));
    RoboCheckEqInt((int)fixture.session.pointers.count, 1);

    /* ... and the sweep-only door releases exactly that state, without a registry entry. */
    RoboCheckEqInt(InputSessionDeviceLost(&fixture.session, (InputDeviceId)9u, &sweep), RoboOk);
    RoboCheckEqInt((int)sweep.pointersRemoved, 1);
    RoboCheckEqInt((int)fixture.session.pointers.count, 0);
    RoboCheckEqInt(InputSessionDeviceLost(&fixture.session, RoboInputDeviceIdNone, &sweep), RoboInvalidArgument);
    RoboCheckEqInt(InputSessionDisconnect(NULL, (InputDeviceId)1u, &sweep, &diag), RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* The transient reset                                                 */
/* ------------------------------------------------------------------ */

static void TestSessionTransient(void)
{
    SessionFixture fixture;
    InputSessionSweep sweep;
    InputTarget window = SessionWindowTarget((WindowId)7u);
    InputTarget target = SessionNodeTarget((WindowId)7u, (LayoutNodeId)31u);
    InputDragPayload payload;
    InputDiag diag;

    InputDiagReset(&diag);
    SessionBuild(&fixture);
    InputSessionInit(NULL);

    SessionPressKey(&fixture.session, fixture.keyboard, RoboPhysicalKeyA, 0u);
    SessionHoldPointer(&fixture.session, fixture.mouse, (InputPointerId)1u);
    RoboCheckEqInt(InputPointerCaptureSet(&fixture.session.captures, (InputPointerId)1u, &target, &diag),
                   RoboOk);
    SessionBeginTouch(&fixture.session, fixture.mouse, 4u);
    {
        InputClickConfig click = InputClickConfigDefault();

        RoboCheck(InputClickStatePress(&fixture.session.clicks, (InputPointerId)1u, RoboMouseButtonLeft,
                                       MakeGfxPoint(10, 20), 7u, &click) >= 0);
    }
    SessionActivate(&fixture, (WindowId)7u);
    RoboCheckEqInt(InputFocusRequest(&fixture.session.focus, &target, RoboInputFocusReasonKeyboard, NULL,
                                     &diag),
                   RoboOk);
    RoboCheck(InputFocusManagerTarget(&fixture.session.focus) != NULL);

    /* A pending accent and a running drag, so the reset has something transient to cancel. */
    {
        InputLayout layout = SessionLayout();

        RoboCheckEqInt(InputTextPipelineSetLayout(&fixture.session.text, &layout, &diag), RoboOk);
    }
    fixture.session.text.dead.id = (InputDeadKeyId)RoboInputDeadKeyAcute;
    InputDragPayloadInit(&payload);
    RoboCheckEqInt(InputDragPayloadSetText(&payload, "row", 3u, NULL, &diag), RoboOk);
    RoboCheckEqInt(InputDragSessionBegin(&fixture.session.drag, &window, RoboInputDragOperationCopy, &payload,
                                         &diag),
                   RoboOk);

    InputSessionSweepInit(&sweep);
    RoboCheckEqInt(InputSessionResetTransient(&fixture.session, &sweep), RoboOk);
    RoboCheckEqInt((int)sweep.keysReleased, 1);
    RoboCheckEqInt((int)sweep.pointersRemoved, 1);
    RoboCheckEqInt((int)sweep.capturesReleased, 1);
    RoboCheckEqInt((int)sweep.touchesRemoved, 1);
    RoboCheckEqInt((int)sweep.clicksReset, 1);
    RoboCheckEqInt((int)sweep.deadKeyCancelled, 1);
    RoboCheckEqInt((int)sweep.dragCancelled, 1);
    RoboCheckEqInt((int)sweep.focusReleased, 0);
    RoboCheckEqInt((int)sweep.hoversCleared, 0);
    /* Focus survives the reset: it is a relationship rather than transient state, and clearing it would be a
     * focus change nobody asked for. That is the deliberate exception this case exists to pin. */
    RoboCheck(InputFocusManagerTarget(&fixture.session.focus) != NULL);
    RoboCheck(InputSessionIsQuiescent(&fixture.session));
    RoboCheckEqInt((int)InputTextPipelinePendingDeadKey(&fixture.session.text), (int)RoboInputDeadKeyIdNone);
    RoboCheckEqInt((int)fixture.session.drag.phase, (int)RoboInputDragIdle);
    RoboCheckEqInt(fixture.session.sweeps, 1);

    /* A reset of an already-clean session releases nothing, and says so. */
    InputSessionSweepInit(&sweep);
    RoboCheckEqInt(InputSessionResetTransient(&fixture.session, &sweep), RoboOk);
    RoboCheck(InputSessionSweepIsEmpty(&sweep));
    RoboCheckEqInt(fixture.session.sweeps, 2);

    RoboCheckEqInt(InputSessionResetTransient(NULL, &sweep), RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* A destroyed target                                                  */
/* ------------------------------------------------------------------ */

static void TestSessionTarget(void)
{
    SessionFixture fixture;
    InputTarget target = SessionNodeTarget((WindowId)7u, (LayoutNodeId)31u);
    InputTarget window = SessionWindowTarget((WindowId)7u);
    InputDiag diag;

    InputDiagReset(&diag);
    SessionBuild(&fixture);

    /* The target is focused **and** captured -- the two places a destroyed target may linger. */
    SessionActivate(&fixture, (WindowId)7u);
    RoboCheckEqInt(InputFocusRequest(&fixture.session.focus, &target, RoboInputFocusReasonProgrammatic, NULL,
                                     &diag),
                   RoboOk);
    SessionHoldPointer(&fixture.session, fixture.mouse, (InputPointerId)1u);
    RoboCheckEqInt(InputPointerCaptureSet(&fixture.session.captures, (InputPointerId)1u, &target, &diag),
                   RoboOk);

    RoboCheckEqInt((int)InputSessionTargetReleased(&fixture.session, &target), 2);
    RoboCheck(InputFocusManagerTarget(&fixture.session.focus) == NULL);
    RoboCheckEqInt((int)fixture.session.captures.count, 0);
    RoboCheckEqInt(fixture.session.targetsReleased, 1);
    /* The pointer itself is untouched: it is still where it is, it just no longer holds anybody. */
    RoboCheckEqInt((int)fixture.session.pointers.count, 1);

    /* A target that held nothing, a target that is not a record, and NULL. */
    RoboCheckEqInt((int)InputSessionTargetReleased(&fixture.session, &window), 0);
    {
        InputTarget invalid;

        InputTargetInit(&invalid);
        RoboCheckEqInt((int)InputSessionTargetReleased(&fixture.session, &invalid), 0);
        RoboCheckEqInt((int)InputSessionTargetReleased(NULL, &invalid), 0);
    }
}

/* ------------------------------------------------------------------ */
/* Window deactivation                                                 */
/* ------------------------------------------------------------------ */

static void TestSessionWindow(void)
{
    SessionFixture fixture;
    InputSessionSweep sweep;
    InputTarget inside = SessionWindowTarget((WindowId)7u);
    InputTarget outside = SessionWindowTarget((WindowId)8u);
    InputDragPayload payload;
    InputDiag diag;

    InputDiagReset(&diag);
    SessionBuild(&fixture);

    SessionActivate(&fixture, (WindowId)7u);
    RoboCheckEqInt(InputFocusRequest(&fixture.session.focus, &inside, RoboInputFocusReasonKeyboard, NULL,
                                     &diag),
                   RoboOk);
    SessionHoldPointer(&fixture.session, fixture.mouse, (InputPointerId)1u);
    SessionHoldPointer(&fixture.session, fixture.mouse, (InputPointerId)2u);
    RoboCheckEqInt(InputPointerCaptureSet(&fixture.session.captures, (InputPointerId)1u, &inside, &diag),
                   RoboOk);
    RoboCheckEqInt(InputPointerCaptureSet(&fixture.session.captures, (InputPointerId)2u, &outside, &diag),
                   RoboOk);
    SessionPressKey(&fixture.session, fixture.keyboard, RoboPhysicalKeyA, 0u);

    /* A drag that began elsewhere and is merely over something in this window: it is *not* this window's,
     * and cancelling it would take a drag away from the window it belongs to. */
    InputDragPayloadInit(&payload);
    RoboCheckEqInt(InputDragPayloadSetText(&payload, "row", 3u, NULL, &diag), RoboOk);
    RoboCheckEqInt(InputDragSessionBegin(&fixture.session.drag, &outside, RoboInputDragOperationCopy, &payload,
                                         &diag),
                   RoboOk);

    InputSessionSweepInit(&sweep);
    RoboCheckEqInt(InputSessionWindowDeactivated(&fixture.session, (WindowId)7u, &sweep), RoboOk);
    RoboCheckEqInt((int)sweep.capturesReleased, 1);
    RoboCheckEqInt((int)sweep.focusReleased, 1);
    RoboCheckEqInt((int)sweep.dragCancelled, 0);
    /* Keys are the device's, not the window's: a user may still be holding them as the window changes. */
    RoboCheckEqInt((int)sweep.keysReleased, 0);
    RoboCheckEqInt((int)InputKeyboardStateHeldCount(&fixture.session.keyboard), 1);
    RoboCheckEqInt((int)fixture.session.captures.count, 1);
    RoboCheck(InputFocusManagerTarget(&fixture.session.focus) == NULL);
    RoboCheckEqInt(fixture.session.deactivations, 1);

    /* Now a drag that genuinely began in the window: that one goes. */
    RoboCheckEqInt(InputDragSessionReset(&fixture.session.drag), (int)RoboInputDragDragging);
    RoboCheckEqInt(InputDragSessionBegin(&fixture.session.drag, &inside, RoboInputDragOperationCopy, &payload,
                                         &diag),
                   RoboOk);
    InputSessionSweepInit(&sweep);
    RoboCheckEqInt(InputSessionWindowDeactivated(&fixture.session, (WindowId)7u, &sweep), RoboOk);
    RoboCheckEqInt((int)sweep.dragCancelled, 1);
    RoboCheckEqInt((int)fixture.session.drag.phase, (int)RoboInputDragIdle);

    /* A zero window and a NULL session are refusals rather than a sweep of everything. */
    RoboCheckEqInt(InputSessionWindowDeactivated(&fixture.session, 0u, &sweep), RoboInvalidArgument);
    RoboCheckEqInt(InputSessionWindowDeactivated(NULL, (WindowId)7u, &sweep), RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Dump                                                                */
/* ------------------------------------------------------------------ */

static void TestSessionDump(void)
{
    SessionFixture fixture;
    InputSession empty;
    InputTarget target = SessionNodeTarget((WindowId)7u, (LayoutNodeId)31u);
    InputDiag diag;
    char line[512];
    char again[512];
    size_t length = 0u;
    size_t againLength = 0u;

    InputDiagReset(&diag);
    SessionBuild(&fixture);
    SessionPressKey(&fixture.session, fixture.keyboard, RoboPhysicalKeyA, 0u);
    SessionHoldPointer(&fixture.session, fixture.mouse, (InputPointerId)1u);
    SessionActivate(&fixture, (WindowId)7u);
    RoboCheckEqInt(InputFocusRequest(&fixture.session.focus, &target, RoboInputFocusReasonKeyboard, NULL,
                                     &diag),
                   RoboOk);

    RoboCheckEqInt(InputSessionWrite(&fixture.session, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckEqInt((int)strlen(line), (int)length);
    RoboCheckStrEq(line,
                   "session devices=2 keys=1 pointers=1 captures=0 hovers=0 touches=0 focus=1 accent=none "
                   "drag=idle connects=2 disconnects=0 sweeps=0 targets=0 deactivations=0\n");

    InputSessionInit(&empty);
    RoboCheckEqInt(InputSessionWrite(&empty, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line,
                   "session devices=0 keys=0 pointers=0 captures=0 hovers=0 touches=0 focus=0 accent=none "
                   "drag=idle connects=0 disconnects=0 sweeps=0 targets=0 deactivations=0\n");
    RoboCheckEqInt(InputSessionWrite(&empty, again, sizeof(again), &againLength, &diag), RoboOk);
    RoboCheckEqInt((int)againLength, (int)length);
    RoboCheck(strcmp(line, again) == 0);

    RoboCheckEqInt(InputSessionWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "session <none>\n");

    /* The survey doors tolerate NULL rather than dereferencing it. */
    RoboCheckEqInt((int)InputSessionDeviceCount(NULL), 0);
    RoboCheckEqInt(InputSessionHasDevice(NULL, (InputDeviceId)1u), 0);
    RoboCheckEqInt(InputSessionIsQuiescent(NULL), 0);
}

/* ------------------------------------------------------------------ */
/* Frame                                                               */
/* ------------------------------------------------------------------ */

void TestRegisterInputSession(void)
{
    TestRegister("input.session.sweep", TestSessionSweep);
    TestRegister("input.session.device", TestSessionDevice);
    TestRegister("input.session.transient", TestSessionTransient);
    TestRegister("input.session.target", TestSessionTarget);
    TestRegister("input.session.window", TestSessionWindow);
    TestRegister("input.session.dump", TestSessionDump);
}
