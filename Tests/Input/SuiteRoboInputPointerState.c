/*
 * RoboInput pointer-state suite (Phase 13.14--13.19) — `input.pointer.*`.
 *
 * Seven cases, and each one's subject is a *rule* rather than a field:
 *
 *   `wheel`       the conversion the module refuses to guess: pixel deltas pass through, line deltas
 *                 multiply by the caller's line height, and a `PAGE` unit is not defined by this build so
 *                 it is *not* silently treated as one. A zero page height is not a crash;
 *
 *   `table`       the live pointer table: the first event creates the track, the second updates it in
 *                 place, the mask is **taken from the event** rather than merged with what was there (so a
 *                 release the event spells in its mask really releases), the table fills, and `overflowed`
 *                 counts the refusal rather than losing it;
 *
 *   `clicks`      the four conditions click counting has, each asserted on its own: same pointer, same
 *                 button, within the time, within the distance. A right press between two left ones does
 *                 not join them, a long wait starts over, a far click starts over, and a zero time window
 *                 disables continuation entirely;
 *
 *   `capture`     capture: replacing a holder is a statement rather than a conflict; a *stale* holder
 *                 cannot release the newer holder's capture (the rule the whole API exists for); a
 *                 destroyed target is released by identity, and a deactivating window by window;
 *
 *   `capture_all` the per-pointer and per-window sweeps, including the copy-out a caller needs to know
 *                 *which* target to hand the terminal event to, with the count reporting more releases
 *                 than the array held;
 *
 *   `hover`       enter/leave: a move from A to B is leave-then-enter in that order, a move inside A is
 *                 nothing, re-reporting A is nothing **and is counted**, and leaving everything is a leave;
 *
 *   `hotplug`     the disconnect cascade a caller runs: every pointer of one device goes, and the hover and
 *                 capture it held go with it, so no button is left down and no caret is left dragging.
 */

#include "RoboInputTestSupport.h"

#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Wheel                                                               */
/* ------------------------------------------------------------------ */

static void TestPointerWheel(void)
{
    InputWheelData wheel;
    InputWheelPixels pixels;

    InputWheelDataInit(&wheel);

    /* A cleared wheel is pixels-of-nothing, and its unit is the one RoboWindow spells first. */
    pixels = InputWheelToPixels(&wheel, 0, 0);
    RoboCheckEqInt((int)pixels.deltaX, 0);
    RoboCheckEqInt((int)pixels.deltaY, 0);
    RoboCheckEqInt((int)pixels.unit, (int)RoboMouseScrollUnitLine);

    /* Pixels pass through: a high-resolution wheel's fractional notch is the caller's pixels already. */
    wheel.deltaX = -3;
    wheel.deltaY = 120;
    wheel.unit = RoboMouseScrollUnitPixel;
    pixels = InputWheelToPixels(&wheel, 40, 800);
    RoboCheckEqInt((int)pixels.deltaX, -3);
    RoboCheckEqInt((int)pixels.deltaY, 120);

    /* Lines multiply by the line height the caller knows and this module does not. */
    wheel.deltaX = 0;
    wheel.deltaY = 3;
    wheel.unit = RoboMouseScrollUnitLine;
    pixels = InputWheelToPixels(&wheel, 40, 800);
    RoboCheckEqInt((int)pixels.deltaY, 120);
    RoboCheckEqInt((int)pixels.unit, (int)RoboMouseScrollUnitLine);

    /* A line height of zero is a document with no lines: the honest answer is no distance, not a guess. */
    pixels = InputWheelToPixels(&wheel, 0, 800);
    RoboCheckEqInt((int)pixels.deltaY, 0);

    /* A negative height is treated as absent rather than as a backwards scroll. */
    pixels = InputWheelToPixels(&wheel, -40, 800);
    RoboCheckEqInt((int)pixels.deltaY, 0);

    /* No wheel at all. */
    pixels = InputWheelToPixels(NULL, 40, 800);
    RoboCheckEqInt((int)pixels.deltaY, 0);

    /* The unit vocabulary itself: two are defined, and `PAGE` is not one of them -- which is why the
     * conversion above does not pretend to scale it. */
    RoboCheck(InputScrollUnitIsKnown(RoboMouseScrollUnitLine));
    RoboCheck(InputScrollUnitIsKnown(RoboMouseScrollUnitPixel));
    RoboCheckStrEq(InputScrollUnitName(RoboMouseScrollUnitLine), "line");
    RoboCheckStrEq(InputScrollUnitName(RoboMouseScrollUnitPixel), "pixel");
    RoboCheckStrEq(InputScrollUnitName((MouseScrollUnit)77), "unknown");
}

/* ------------------------------------------------------------------ */
/* The live pointer table                                              */
/* ------------------------------------------------------------------ */

static void TestPointerTable(void)
{
    InputPointerState state;
    InputPointerTrack track;
    InputPointerData data;
    InputDiag diag;
    uint32_t index;

    InputDiagReset(&diag);
    InputPointerStateInit(&state);
    RoboCheckEqInt((int)state.count, 0);

    /* The first event creates the track. */
    data = GetRoboInputPointerData(10, 20, RoboMouseButtonNone, 0u);
    RoboCheckEqInt(InputPointerStateApply(&state, 7u, RoboInputDeviceTypeMouse, &data,
                                          RoboInputSpaceScreen, RoboInputModifiersNone, &track, &diag),
                   RoboOk);
    RoboCheckEqInt((int)state.count, 1);
    RoboCheck(track.active);
    RoboCheckEqInt((int)track.pointer, (int)RoboInputPointerIdPrimary);
    RoboCheckEqInt((int)track.device, 7);
    RoboCheckEqInt((int)track.type, (int)RoboInputDeviceTypeMouse);

    /* The second updates in place: the count does not grow. */
    data = GetRoboInputPointerData(45, 55, RoboMouseButtonLeft, InputButtonMask(RoboMouseButtonLeft));
    RoboCheckEqInt(InputPointerStateApply(&state, 7u, RoboInputDeviceTypeMouse, &data,
                                          RoboInputSpaceScreen, RoboInputModifiersNone, &track, &diag),
                   RoboOk);
    RoboCheckEqInt((int)state.count, 1);
    RoboCheckEqInt((int)track.position.x, 45);
    RoboCheckEqInt((int)track.position.y, 55);
    RoboCheck(InputPointerStateIsButtonDown(&state, RoboInputPointerIdPrimary, RoboMouseButtonLeft));

    /* The mask is the event's, not a merge: a release whose mask is empty really releases. */
    data = GetRoboInputPointerData(45, 55, RoboMouseButtonLeft, 0u);
    RoboCheckEqInt(InputPointerStateApply(&state, 7u, RoboInputDeviceTypeMouse, &data,
                                          RoboInputSpaceScreen, RoboInputModifiersNone, NULL, &diag),
                   RoboOk);
    RoboCheck(!InputPointerStateIsButtonDown(&state, RoboInputPointerIdPrimary, RoboMouseButtonLeft));

    /* A second pointer is a second track; its own mask is its own. */
    InputPointerDataInit(&data);
    RoboCheckEqInt(InputPointerDataSet(&data, 3u, MakeGfxPoint(70, 80), RoboMouseButtonNone,
                                       InputButtonMask(RoboMouseButtonRight)),
                   RoboOk);
    RoboCheckEqInt(InputPointerStateApply(&state, 9u, RoboInputDeviceTypeTouchscreen, &data,
                                          RoboInputSpaceScreen, RoboKeyModShift, NULL, &diag),
                   RoboOk);
    RoboCheckEqInt((int)state.count, 2);
    RoboCheck(!InputPointerStateIsButtonDown(&state, 3u, RoboMouseButtonLeft));
    RoboCheck(InputPointerStateIsButtonDown(&state, 3u, RoboMouseButtonRight));
    RoboCheckEqInt((int)InputPointerStateModifiers(&state, 3u), (int)RoboKeyModShift);
    RoboCheckEqInt((int)InputPointerStateModifiers(&state, 99u), 0);

    /* A space this build does not define is refused, and changes nothing. */
    RoboCheckEqInt(InputPointerStateApply(&state, 7u, RoboInputDeviceTypeMouse, &data,
                                          (InputSpace)42, RoboInputModifiersNone, NULL, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboInputDomainPointer);
    RoboCheckEqInt((int)state.count, 2);

    /* NULL arguments. */
    RoboCheckEqInt(InputPointerStateApply(NULL, 7u, RoboInputDeviceTypeMouse, &data,
                                          RoboInputSpaceScreen, RoboInputModifiersNone, NULL, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputPointerStateApply(&state, 7u, RoboInputDeviceTypeMouse, NULL,
                                          RoboInputSpaceScreen, RoboInputModifiersNone, NULL, &diag),
                   RoboInvalidArgument);

    /* The table fills, and the refusal is *counted*: a pointer nobody tracked is something a caller must
     * be able to find out, not a silent drop. */
    InputPointerStateInit(&state);
    for (index = 0u; index < RoboInputMaxPointers; index++) {
        InputPointerDataInit(&data);
        RoboCheckEqInt(InputPointerDataSet(&data, index + 1u, MakeGfxPoint((int32_t)index, 0),
                                           RoboMouseButtonNone, 0u),
                       RoboOk);
        RoboCheckEqInt(InputPointerStateApply(&state, 1u, RoboInputDeviceTypeTouchscreen, &data,
                                              RoboInputSpaceScreen, RoboInputModifiersNone, NULL, &diag),
                       RoboOk);
    }
    RoboCheckEqInt((int)state.count, (int)RoboInputMaxPointers);
    RoboCheckEqInt((int)state.overflowed, 0);

    InputPointerDataInit(&data);
    RoboCheckEqInt(InputPointerDataSet(&data, 200u, MakeGfxPoint(1, 1), RoboMouseButtonNone, 0u),
                   RoboOk);
    RoboCheckEqInt(InputPointerStateApply(&state, 1u, RoboInputDeviceTypeTouchscreen, &data,
                                          RoboInputSpaceScreen, RoboInputModifiersNone, NULL, &diag),
                   RoboOutOfMemory);
    RoboCheckEqInt((int)state.overflowed, 1);
    RoboCheckEqInt((int)state.count, (int)RoboInputMaxPointers);
    RoboCheckEqInt(diag.domain, RoboInputDomainPointer);

    /* Removal frees a slot, and the pointer that was refused fits now. */
    RoboCheck(InputPointerStateRemove(&state, 1u));
    RoboCheck(!InputPointerStateRemove(&state, 1u));
    RoboCheckEqInt((int)state.count, (int)(RoboInputMaxPointers - 1u));
    RoboCheckEqInt(InputPointerStateApply(&state, 1u, RoboInputDeviceTypeTouchscreen, &data,
                                          RoboInputSpaceScreen, RoboInputModifiersNone, NULL, &diag),
                   RoboOk);
    RoboCheckEqInt((int)state.count, (int)RoboInputMaxPointers);

    RoboCheckEqInt((int)InputPointerStateClear(&state), (int)RoboInputMaxPointers);
    RoboCheckEqInt((int)state.count, 0);
    RoboCheckEqInt((int)InputPointerStateClear(&state), 0);
}

/* ------------------------------------------------------------------ */
/* Click counting                                                      */
/* ------------------------------------------------------------------ */

static void TestPointerClicks(void)
{
    InputClickState state;
    InputClickConfig config = InputClickConfigDefault();
    GfxPoint start = MakeGfxPoint(100, 100);

    RoboCheckEqInt((int)config.doubleClickTimeMs, 500);
    RoboCheck(config.doubleClickDistance > 3.9f && config.doubleClickDistance < 4.1f);

    InputClickStateInit(&state);

    /* A first press is one click. */
    RoboCheckEqInt(InputClickStatePress(&state, RoboInputPointerIdPrimary, RoboMouseButtonLeft,
                                        start, 1000u, &config),
                   1);

    /* Soon enough, near enough: two. */
    RoboCheckEqInt(InputClickStatePress(&state, RoboInputPointerIdPrimary, RoboMouseButtonLeft,
                                        MakeGfxPoint(102, 101), 1200u, &config),
                   2);

    /* A third inside the window is three. */
    RoboCheckEqInt(InputClickStatePress(&state, RoboInputPointerIdPrimary, RoboMouseButtonLeft,
                                        start, 1300u, &config),
                   3);

    /* Too late: back to one. */
    RoboCheckEqInt(InputClickStatePress(&state, RoboInputPointerIdPrimary, RoboMouseButtonLeft,
                                        start, 1300u + 501u, &config),
                   1);

    /* Five units right, *immediately* after: the distance broke the sequence, not the time. The
     * distance is the sum of the two axis gaps, so five units is outside a window of four. */
    RoboCheckEqInt(InputClickStatePress(&state, RoboInputPointerIdPrimary, RoboMouseButtonLeft,
                                        MakeGfxPoint(105, 100), 1851u, &config),
                   1);

    /* ...and near *that* one it continues: proximity is measured against the last press, not the
     * sequence's first. */
    RoboCheckEqInt(InputClickStatePress(&state, RoboInputPointerIdPrimary, RoboMouseButtonLeft,
                                        MakeGfxPoint(107, 100), 1901u, &config),
                   2);

    /* A different button does not join: the sequence is keyed by both halves of the pair. */
    RoboCheckEqInt(InputClickStatePress(&state, RoboInputPointerIdPrimary, RoboMouseButtonRight,
                                        start, 2000u, &config),
                   1);
    RoboCheckEqInt(InputClickStateCount(&state, RoboInputPointerIdPrimary, RoboMouseButtonRight), 1);
    RoboCheckEqInt(InputClickStateCount(&state, RoboInputPointerIdPrimary, RoboMouseButtonLeft), 2);

    /* A different pointer does not join either. */
    RoboCheckEqInt(InputClickStatePress(&state, 4u, RoboMouseButtonLeft, start, 2300u, &config), 1);
    RoboCheckEqInt(InputClickStatePress(&state, 4u, RoboMouseButtonLeft, start, 2400u, &config), 2);

    /* A move is not a press and leaves every sequence alone. */
    RoboCheckEqInt(InputClickStatePress(&state, RoboInputPointerIdPrimary, RoboMouseButtonNone,
                                        MakeGfxPoint(500, 500), 2500u, &config),
                   0);
    RoboCheckEqInt(InputClickStateCount(&state, 4u, RoboMouseButtonLeft), 2);

    /* A zero window disables continuation: every press is a single click, which is how a platform that
     * counts its own is expressed. */
    config.doubleClickTimeMs = 0u;
    RoboCheckEqInt(InputClickStatePress(&state, 4u, RoboMouseButtonLeft, start, 2600u, &config), 1);
    RoboCheckEqInt(InputClickStatePress(&state, 4u, RoboMouseButtonLeft, start, 2601u, &config), 1);

    /* Resets. */
    RoboCheck(InputClickStateReset(&state, 4u, RoboMouseButtonLeft));
    RoboCheck(!InputClickStateReset(&state, 4u, RoboMouseButtonLeft));
    RoboCheckEqInt(InputClickStateCount(&state, 4u, RoboMouseButtonLeft), 0);
    RoboCheckEqInt((int)InputClickStateResetPointer(&state, RoboInputPointerIdPrimary), 2);
    RoboCheckEqInt((int)InputClickStateResetPointer(&state, RoboInputPointerIdPrimary), 0);
    RoboCheckEqInt(InputClickStateCount(&state, RoboInputPointerIdPrimary, RoboMouseButtonLeft), 0);

    /* A time that went backwards does not continue a sequence: a non-monotonic stamp is a caller's bug and
     * must not read as a very fast double click. */
    InputClickStateInit(&state);
    RoboCheckEqInt(InputClickStatePress(&state, RoboInputPointerIdPrimary, RoboMouseButtonLeft,
                                        start, 5000u, &config),
                   1);
    RoboCheckEqInt(InputClickStatePress(&state, RoboInputPointerIdPrimary, RoboMouseButtonLeft,
                                        start, 10u, &config),
                   1);
}

/* ------------------------------------------------------------------ */
/* Capture                                                             */
/* ------------------------------------------------------------------ */

static WindowId PointerFixtureWindow(void)
{
    return 7u;
}

static InputTarget PointerWindowTarget(WindowId window)
{
    InputTarget target;
    InputDiag diag;

    InputTargetInit(&target);
    if (InputTargetSetWindow(&target, window, RoboInputSpaceWindow, &diag) != RoboOk) {
        RoboCheckMsg(0, "the fixture's window target was refused");
    }
    return target;
}

static InputTarget PointerApplicationTarget(InputApplicationId application)
{
    InputTarget target;
    InputDiag diag;

    InputTargetInit(&target);
    if (InputTargetSetApplication(&target, application, RoboInputSpaceWindow, &diag) != RoboOk) {
        RoboCheckMsg(0, "the fixture's application target was refused");
    }
    return target;
}

static void TestPointerCapture(void)
{
    InputPointerCapture capture;
    InputTarget a = PointerWindowTarget(7u);
    InputTarget b = PointerWindowTarget(9u);
    InputTarget app = PointerApplicationTarget(1u);
    const InputTarget *held;
    InputDiag diag;

    InputDiagReset(&diag);
    InputPointerCaptureInit(&capture);
    RoboCheckEqInt((int)capture.count, 0);
    RoboCheck(InputPointerCaptureTarget(&capture, RoboInputPointerIdPrimary) == NULL);

    RoboCheckEqInt(InputPointerCaptureSet(&capture, RoboInputPointerIdPrimary, &a, &diag), RoboOk);
    RoboCheckEqInt((int)capture.count, 1);
    held = InputPointerCaptureTarget(&capture, RoboInputPointerIdPrimary);
    RoboCheck(held != NULL && InputTargetEqual(held, &a));
    RoboCheck(InputPointerCaptureHolds(&capture, RoboInputPointerIdPrimary, &a));
    RoboCheck(!InputPointerCaptureHolds(&capture, RoboInputPointerIdPrimary, &b));

    /* A stale holder cannot release the newer holder's capture. This is the rule the API exists for: the
     * old target's `up` arrives after the pointer was handed on, and it must be a no-op. */
    RoboCheckEqInt(InputPointerCaptureSet(&capture, RoboInputPointerIdPrimary, &b, &diag), RoboOk);
    RoboCheckEqInt((int)capture.count, 1);
    RoboCheck(!InputPointerCaptureRelease(&capture, RoboInputPointerIdPrimary, &a));
    RoboCheck(InputPointerCaptureHolds(&capture, RoboInputPointerIdPrimary, &b));

    /* The holder releases. */
    RoboCheck(InputPointerCaptureRelease(&capture, RoboInputPointerIdPrimary, &b));
    RoboCheckEqInt((int)capture.count, 0);
    RoboCheck(!InputPointerCaptureRelease(&capture, RoboInputPointerIdPrimary, &b));

    /* A NULL target is the layer releasing on the pointer's behalf, and it always wins. */
    RoboCheckEqInt(InputPointerCaptureSet(&capture, RoboInputPointerIdPrimary, &a, &diag), RoboOk);
    RoboCheck(InputPointerCaptureRelease(&capture, RoboInputPointerIdPrimary, NULL));

    /* A target that is not a target is refused, and counted. */
    {
        InputTarget empty;

        InputTargetInit(&empty);
        RoboCheckEqInt(InputPointerCaptureSet(&capture, RoboInputPointerIdPrimary, &empty, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(diag.domain, RoboInputDomainPointer);
        RoboCheckEqInt((int)capture.refused, 1);
        RoboCheckEqInt(InputPointerCaptureSet(&capture, RoboInputPointerIdPrimary, NULL, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputPointerCaptureSet(NULL, RoboInputPointerIdPrimary, &a, &diag),
                       RoboInvalidArgument);
    }

    /* A destroyed target releases every pointer it held, by identity. */
    RoboCheckEqInt(InputPointerCaptureSet(&capture, 1u, &a, &diag), RoboOk);
    RoboCheckEqInt(InputPointerCaptureSet(&capture, 2u, &a, &diag), RoboOk);
    RoboCheckEqInt(InputPointerCaptureSet(&capture, 3u, &b, &diag), RoboOk);
    RoboCheckEqInt((int)InputPointerCaptureReleaseTarget(&capture, &a), 2);
    RoboCheckEqInt((int)capture.count, 1);
    RoboCheck(InputPointerCaptureHolds(&capture, 3u, &b));

    /* A window deactivation releases what names *that window* -- and an application target names no window,
     * so it survives: the rule is what the target records, not where its content happens to live. */
    RoboCheckEqInt(InputPointerCaptureSet(&capture, 1u, &app, &diag), RoboOk);
    RoboCheckEqInt(InputPointerCaptureSet(&capture, 2u, &a, &diag), RoboOk);
    RoboCheckEqInt((int)InputPointerCaptureReleaseWindow(&capture, PointerFixtureWindow()), 1);
    RoboCheckEqInt((int)capture.count, 2);
    RoboCheck(InputPointerCaptureHolds(&capture, 2u, NULL) == 0);
    RoboCheck(InputPointerCaptureTarget(&capture, 2u) == NULL);
    RoboCheck(InputPointerCaptureHolds(&capture, 1u, &app));
    RoboCheck(InputPointerCaptureHolds(&capture, 3u, &b));
    RoboCheckEqInt((int)InputPointerCaptureReleaseWindow(&capture, PointerFixtureWindow()), 0);
    RoboCheckEqInt((int)InputPointerCaptureReleaseWindow(&capture, 0u), 0);

    /* The sweeps: everything of one pointer, with the target copied out so the caller knows where the
     * terminal event goes. A pointer holds one capture by construction, so the count is 1 and the array
     * needs one slot -- the capacity argument is there for the day a pointer may hold more. */
    {
        InputTarget out[2];
        uint32_t released;

        RoboCheckEqInt(InputPointerCaptureSet(&capture, 5u, &a, &diag), RoboOk);
        /* A second capture of the same pointer by another target is a *statement*: the newer holder wins,
         * which is what a slider taking the pointer from a button underneath it is. */
        RoboCheckEqInt(InputPointerCaptureSet(&capture, 5u, &app, &diag), RoboOk);

        memset(out, 0, sizeof(out));
        released = InputPointerCaptureReleaseAll(&capture, 5u, out, 1u);
        RoboCheckEqInt((int)released, 1);
        RoboCheck(InputTargetEqual(&out[0], &app));
        RoboCheckEqInt((int)capture.count, 2);

        RoboCheckEqInt((int)InputPointerCaptureReleaseAll(&capture, 999u, NULL, 0u), 0);
    }

    RoboCheckEqInt((int)InputPointerCaptureClear(&capture), 2);
    RoboCheckEqInt((int)capture.count, 0);
    RoboCheckEqInt((int)InputPointerCaptureClear(&capture), 0);
}

/* ------------------------------------------------------------------ */
/* Hover                                                               */
/* ------------------------------------------------------------------ */

static void TestPointerHover(void)
{
    InputHoverState state;
    InputTarget a = PointerWindowTarget(7u);
    InputTarget b = PointerWindowTarget(9u);
    InputTarget leave;
    InputTarget enter;
    const InputTarget *over;

    InputHoverStateInit(&state);

    /* Entering from nothing is one event, and it is the enter. */
    memset(&leave, 0, sizeof(leave));
    memset(&enter, 0, sizeof(enter));
    RoboCheckEqInt(InputHoverSet(&state, RoboInputPointerIdPrimary, &a, &leave, &enter), 1);
    RoboCheck(InputTargetEqual(&enter, &a));
    RoboCheckEqInt((int)state.count, 1);
    RoboCheck(InputHoverIsOver(&state, RoboInputPointerIdPrimary, &a));

    /* Already there: nothing at all, and the redundancy is *counted* rather than hidden. */
    RoboCheckEqInt(InputHoverSet(&state, RoboInputPointerIdPrimary, &a, NULL, NULL), 0);
    RoboCheckEqInt((int)state.deduplicated, 1);

    /* A move to B is leave-then-enter, in that order, and both halves are reported. */
    memset(&leave, 0, sizeof(leave));
    memset(&enter, 0, sizeof(enter));
    RoboCheckEqInt(InputHoverSet(&state, RoboInputPointerIdPrimary, &b, &leave, &enter), 2);
    RoboCheck(InputTargetEqual(&leave, &a));
    RoboCheck(InputTargetEqual(&enter, &b));
    over = InputHoverTarget(&state, RoboInputPointerIdPrimary);
    RoboCheck(over != NULL && InputTargetEqual(over, &b));

    /* Leaving everything is a leave and nothing else. */
    memset(&leave, 0, sizeof(leave));
    memset(&enter, 0, sizeof(enter));
    RoboCheckEqInt(InputHoverSet(&state, RoboInputPointerIdPrimary, NULL, &leave, &enter), 1);
    RoboCheck(InputTargetEqual(&leave, &b));
    RoboCheck(InputHoverTarget(&state, RoboInputPointerIdPrimary) == NULL);
    RoboCheckEqInt((int)state.count, 0);

    /* Leaving when already over nothing is nothing. */
    RoboCheckEqInt(InputHoverSet(&state, RoboInputPointerIdPrimary, NULL, NULL, NULL), 0);

    /* A second pointer has its own hover: the two do not share a slot. */
    RoboCheckEqInt(InputHoverSet(&state, 3u, &a, NULL, NULL), 1);
    RoboCheckEqInt((int)state.count, 1);
    RoboCheck(InputHoverIsOver(&state, 3u, &a));
    RoboCheck(!InputHoverIsOver(&state, RoboInputPointerIdPrimary, &a));

    /* A target that is not a target never becomes a hover. */
    {
        InputTarget empty;

        InputTargetInit(&empty);
        RoboCheckEqInt(InputHoverSet(&state, 4u, &empty, NULL, NULL), 0);
        RoboCheck(InputHoverTarget(&state, 4u) == NULL);
    }

    /* Removing a hover emits nothing: a disconnect has no target left to tell. */
    RoboCheck(InputHoverStateRemove(&state, 3u));
    RoboCheck(!InputHoverStateRemove(&state, 3u));
    RoboCheckEqInt((int)InputHoverStateClear(&state), 0);

    RoboCheckEqInt(InputHoverSet(NULL, 1u, &a, NULL, NULL), -1);
}

/* ------------------------------------------------------------------ */
/* Disconnect cascade                                                  */
/* ------------------------------------------------------------------ */

static void TestPointerHotplug(void)
{
    InputPointerState pointers;
    InputPointerCapture capture;
    InputHoverState hover;
    InputPointerData data;
    InputDiag diag;
    InputTarget target = PointerWindowTarget(7u);

    InputDiagReset(&diag);
    InputPointerStateInit(&pointers);
    InputPointerCaptureInit(&capture);
    InputHoverStateInit(&hover);

    /* A mouse at 7, and a finger at 9. */
    data = GetRoboInputPointerData(10, 10, RoboMouseButtonLeft, InputButtonMask(RoboMouseButtonLeft));
    RoboCheckEqInt(InputPointerStateApply(&pointers, 7u, RoboInputDeviceTypeMouse, &data,
                                          RoboInputSpaceScreen, RoboInputModifiersNone, NULL, &diag),
                   RoboOk);
    RoboCheckEqInt(InputPointerCaptureSet(&capture, RoboInputPointerIdPrimary, &target, &diag), RoboOk);
    RoboCheckEqInt(InputHoverSet(&hover, RoboInputPointerIdPrimary, &target, NULL, NULL), 1);

    InputPointerDataInit(&data);
    RoboCheckEqInt(InputPointerDataSet(&data, 2u, MakeGfxPoint(50, 50), RoboMouseButtonNone, 0u),
                   RoboOk);
    RoboCheckEqInt(InputPointerStateApply(&pointers, 9u, RoboInputDeviceTypeTouchscreen, &data,
                                          RoboInputSpaceScreen, RoboInputModifiersNone, NULL, &diag),
                   RoboOk);
    RoboCheckEqInt(InputPointerStateApply(&pointers, 9u, RoboInputDeviceTypeTouchscreen, &data,
                                          RoboInputSpaceScreen, RoboInputModifiersNone, NULL, &diag),
                   RoboOk);
    RoboCheckEqInt((int)pointers.count, 2);

    /* Unplug the mouse. Its pointer goes -- so no button is left down -- and nothing about the finger
     * changed. The capture and the hover do not go *by themselves*: the caller releases them, which is why
     * the release doors report what they released and the caller can hand the terminal event to the target
     * that held the pointer. */
    RoboCheckEqInt((int)InputPointerStateRemoveDevice(&pointers, 7u), 1);
    RoboCheckEqInt((int)pointers.count, 1);
    RoboCheck(!InputPointerStateIsButtonDown(&pointers, RoboInputPointerIdPrimary, RoboMouseButtonLeft));
    RoboCheck(InputPointerStateFind(&pointers, RoboInputPointerIdPrimary) == NULL);
    RoboCheck(InputPointerStateFind(&pointers, 2u) != NULL);

    {
        InputTarget out[2];
        uint32_t released;

        memset(out, 0, sizeof(out));
        released = InputPointerCaptureReleaseAll(&capture, RoboInputPointerIdPrimary, out, 2u);
        RoboCheckEqInt((int)released, 1);
        RoboCheck(InputTargetEqual(&out[0], &target));
    }
    RoboCheck(InputHoverStateRemove(&hover, RoboInputPointerIdPrimary));
    RoboCheckEqInt((int)capture.count, 0);
    RoboCheckEqInt((int)hover.count, 0);

    /* A second unplug of the same device releases nothing, which is the count a caller logs. */
    RoboCheckEqInt((int)InputPointerStateRemoveDevice(&pointers, 7u), 0);
    RoboCheckEqInt((int)InputPointerStateRemoveDevice(&pointers, RoboInputDeviceIdNone), 0);
    RoboCheckEqInt((int)InputPointerStateRemoveDevice(NULL, 7u), 0);
}

/* ------------------------------------------------------------------ */
/* Frame                                                               */
/* ------------------------------------------------------------------ */

void TestRegisterInputPointerState(void)
{
    TestRegister("input.pointer.wheel", TestPointerWheel);
    TestRegister("input.pointer.table", TestPointerTable);
    TestRegister("input.pointer.clicks", TestPointerClicks);
    TestRegister("input.pointer.capture", TestPointerCapture);
    TestRegister("input.pointer.hover", TestPointerHover);
    TestRegister("input.pointer.hotplug", TestPointerHotplug);
}
