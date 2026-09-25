/*
 * RoboInput new-payload suite (Phase 13.2/13.22/13.37/13.53) — `input.payload.*`.
 *
 * The event model grew twenty types and four payload kinds (RoboInputEvent.h). Thirteen of those types are
 * already covered by the suites beside this one -- keys, pointers, wheels and touches -- and the rest need
 * three new records and five new constructions, which is what this file is:
 *
 *   `pen`          a stylus is a pointer, so its pointer half obeys the *same* rule a pointer event does,
 *                  and its own record adds pressure, tilt and rotation. The two consistency rules are the
 *                  cases that matter: `PRESSURE_UNKNOWN` with a non-zero pressure, and a hover sample whose
 *                  tip is down, are both records that describe two different moments;
 *
 *   `gamepad`      normalized buttons and axes, with exactly one half live -- a record carrying both would
 *                  have to be read twice to know what happened;
 *
 *   `composition`  a preedit with its clause and caret, where the four offsets must agree with the string
 *                  and the derived `EMPTY` flag must agree in both directions;
 *
 *   `device`       a hotplug event that carries the whole descriptor, and fills the event's own identity
 *                  from it -- so "the event and the record it carries cannot name two different devices" is
 *                  a fact about the door rather than a rule a caller keeps;
 *
 *   `types`        every new type constructs, validates, refuses the wrong source, and names itself.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* Pen                                                                 */
/* ------------------------------------------------------------------ */

/** A pen sample at @p x,@p y with nothing pressed, asserting on refusal. */
static InputPenData PenSample(int32_t x, int32_t y, int32_t pressure, uint32_t flags)
{
    InputPointerData pointer = GetRoboInputPointerData(x, y, RoboMouseButtonNone, 0u);
    InputPenData pen;

    InputPenDataInit(&pen);
    if (InputPenDataSet(&pen, &pointer, pressure, 0, 0, 0, flags, NULL) != RoboOk) {
        RoboCheckMsg(0, "the fixture's pen sample was refused");
    }
    return pen;
}

static void TestPayloadPen(void)
{
    InputPointerData pointer = GetRoboInputPointerData(40, 50, RoboMouseButtonNone, 0u);
    InputPenData pen;
    InputPenData other;
    InputDiag diag;

    InputDiagReset(&diag);
    /* A cleared pen record **is valid**, and the reason is worth pinning: it is a *sample*, and its
     * all-zero state is a real one -- the primary pointer at the origin, nothing pressed, no pressure, no
     * tilt. A cleared *descriptor* is invalid (RoboInputDevice.h) because a device that names no kind of
     * hardware is not a device. The two records differ because one is a measurement and the other is an
     * identity, and a suite that asserted "cleared means invalid" for both would be asserting a
     * coincidence. */
    InputPenDataInit(&pen);
    RoboCheck(InputPenDataIsValid(&pen));
    RoboCheck(!InputPenDataIsHover(&pen));
    RoboCheck(!InputPenDataIsValid(NULL));

    RoboCheckEqInt(InputPenDataSet(&pen, &pointer, 512, -30, 12, 180,
                                   RoboInputPenFlagBarrel, &diag),
                   RoboOk);
    RoboCheck(InputPenDataIsValid(&pen));
    RoboCheckEqInt(pen.pressure, 512);
    RoboCheckEqInt(pen.tiltX, -30);
    RoboCheckEqInt(pen.tiltY, 12);
    RoboCheckEqInt(pen.rotation, 180);
    /* The pointer half is the *same* record: a stylus's position is a pointer's position, which is what
     * makes every positioned-event door work on a pen event with no second implementation. */
    RoboCheck(GfxPointEqual(pen.pointer.position, MakeGfxPoint(40, 50)));
    RoboCheck(!InputPenDataIsHover(&pen));
    RoboCheck(!InputPenDataIsEraser(&pen));

    /* Out-of-range values are refused, never clamped: a clamped measurement is a plausible wrong number.
     * The refusal **clears** the record rather than leaving the previous accepted sample in place -- which
     * is why the check below is on the fields, since a cleared sample is a valid one. */
    RoboCheckEqInt(InputPenDataSet(&pen, &pointer, RoboInputPenPressureMax + 1, 12, 30, 90,
                                   RoboInputPenFlagBarrel, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(pen.pressure, 0);
    RoboCheckEqInt(pen.tiltX, 0);
    RoboCheckEqInt(pen.rotation, 0);
    RoboCheckEqInt((int)pen.flags, 0);
    RoboCheckEqInt(InputPenDataSet(&pen, &pointer, -1, 0, 0, 0, 0u, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputPenDataSet(&pen, &pointer, 0, 91, 0, 0, 0u, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputPenDataSet(&pen, &pointer, 0, 0, -91, 0, 0u, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputPenDataSet(&pen, &pointer, 0, 0, 0, 360, 0u, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputPenDataSet(&pen, &pointer, 0, 0, 0, 0, 1u << 4, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputPenDataSet(&pen, NULL, 0, 0, 0, 0, 0u, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputPenDataSet(NULL, &pointer, 0, 0, 0, 0, 0u, &diag), RoboInvalidArgument);

    /* The boundary values are *inside*: full pressure, 90 degrees, 359 degrees. */
    RoboCheckEqInt(InputPenDataSet(&pen, &pointer, RoboInputPenPressureMax, RoboInputPenTiltMax,
                                   RoboInputPenTiltMin, RoboInputPenRotationMax, 0u, &diag),
                   RoboOk);
    RoboCheck(InputPenDataIsValid(&pen));

    /* "There is no measurement" and "there is one, and it is this" cannot both hold. A zero pressure with
     * the flag is ordinary; a non-zero pressure with it is a record saying two things at once. */
    RoboCheckEqInt(InputPenDataSet(&pen, &pointer, 0, 0, 0, 0,
                                   RoboInputPenFlagPressureUnknown, &diag),
                   RoboOk);
    RoboCheck(InputPenDataIsValid(&pen));
    pen.pressure = 7;
    RoboCheck(!InputPenDataIsValid(&pen));
    /* And the same rule read the other way: a record built by hand that carries both is refused too. */
    pen.pressure = 7;
    pen.flags = (uint32_t)(RoboInputPenFlagPressureUnknown | RoboInputPenFlagBarrel);
    RoboCheck(!InputPenDataIsValid(&pen));

    /* Hovering is being in range without touching, so a hover sample holds no contact. */
    {
        InputPointerData pressed = GetRoboInputPointerData(40, 50, RoboMouseButtonLeft,
                                                       InputButtonMask(RoboMouseButtonLeft));

        RoboCheckEqInt(InputPenDataSet(&pen, &pressed, 0, 0, 0, 0, RoboInputPenFlagHover, &diag),
                       RoboOk);
        RoboCheck(!InputPenDataIsValid(&pen));
        RoboCheckEqInt(InputPenDataSet(&pen, &pointer, 0, 0, 0, 0, RoboInputPenFlagHover, &diag),
                       RoboOk);
        RoboCheck(InputPenDataIsValid(&pen));
        RoboCheck(InputPenDataIsHover(&pen));
        RoboCheckEqInt(InputPenDataSet(&pen, &pressed, 0, 0, 0, 0, RoboInputPenFlagEraser, &diag),
                       RoboOk);
        RoboCheck(InputPenDataIsValid(&pen));
        RoboCheck(InputPenDataIsEraser(&pen));
    }

    /* Equality is field by field, and the pointer half is part of it. */
    pen = PenSample(10, 20, 300, 0u);
    other = PenSample(10, 20, 300, 0u);
    RoboCheck(InputPenDataEqual(&pen, &other));
    other.pressure = 301;
    RoboCheck(!InputPenDataEqual(&pen, &other));
    other = PenSample(10, 21, 300, 0u);
    RoboCheck(!InputPenDataEqual(&pen, &other));
    RoboCheck(InputPenDataEqual(NULL, NULL));
    RoboCheck(!InputPenDataEqual(&pen, NULL));
    RoboCheck(!InputPenDataIsHover(NULL));
    RoboCheck(!InputPenDataIsEraser(NULL));
}

/* ------------------------------------------------------------------ */
/* Gamepad                                                             */
/* ------------------------------------------------------------------ */

static void TestPayloadGamepad(void)
{
    InputGamepadData data;
    InputGamepadData other;
    InputDiag diag;
    uint32_t index;

    InputDiagReset(&diag);
    InputGamepadDataInit(&data);
    /* A cleared record carries neither half, which is what a discarded event looks like -- and it is
     * *valid*, so a consumer does not have to special-case it. */
    RoboCheck(InputGamepadDataIsValid(&data));
    RoboCheck(!InputGamepadDataIsButton(&data));
    RoboCheck(!InputGamepadDataIsAxis(&data));

    RoboCheckEqInt(InputGamepadDataSetButton(&data, RoboGamepadButtonA, 1, &diag), RoboOk);
    RoboCheck(InputGamepadDataIsValid(&data));
    RoboCheck(InputGamepadDataIsButton(&data));
    RoboCheck(!InputGamepadDataIsAxis(&data));
    RoboCheckEqInt(data.value, 1);
    RoboCheckEqInt(InputGamepadDataSetButton(&data, RoboGamepadButtonA, 0, &diag), RoboOk);
    RoboCheckEqInt(data.value, 0);

    RoboCheckEqInt(InputGamepadDataSetAxis(&data, RoboGamepadAxisLeftX, -32768, &diag), RoboOk);
    RoboCheck(InputGamepadDataIsAxis(&data));
    RoboCheckEqInt(data.value, RoboInputGamepadAxisMin);
    RoboCheckEqInt(InputGamepadDataSetAxis(&data, RoboGamepadAxisRightTrigger,
                                           RoboInputGamepadAxisMax, &diag),
                   RoboOk);
    /* Centred is *derived*: a caller cannot hand over a zero sample that claims otherwise. */
    RoboCheckEqInt(InputGamepadDataSetAxis(&data, RoboGamepadAxisLeftY, 0, &diag), RoboOk);
    RoboCheck((data.flags & (uint32_t)RoboInputGamepadFlagCentered) != 0u);
    RoboCheck(InputGamepadDataIsValid(&data));
    data.flags &= ~(uint32_t)RoboInputGamepadFlagCentered;
    RoboCheck(!InputGamepadDataIsValid(&data));
    data.flags |= (uint32_t)RoboInputGamepadFlagCentered;
    data.value = 5;
    RoboCheck(!InputGamepadDataIsValid(&data));

    /* Refusals: no button, no axis, out of range, an undefined flag, a button value that is not 0 or 1. */
    RoboCheckEqInt(InputGamepadDataSetButton(&data, RoboGamepadButtonNone, 1, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputGamepadDataSetButton(&data, (InputGamepadButton)99, 1, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputGamepadDataSetAxis(&data, RoboGamepadAxisNone, 0, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputGamepadDataSetAxis(&data, RoboGamepadAxisLeftX,
                                           RoboInputGamepadAxisMax + 1, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputGamepadDataSetAxis(&data, RoboGamepadAxisLeftX,
                                           RoboInputGamepadAxisMin - 1, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputGamepadDataSetButton(NULL, RoboGamepadButtonA, 1, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputGamepadDataSetAxis(NULL, RoboGamepadAxisLeftX, 0, &diag),
                   RoboInvalidArgument);

    /* Exactly one half, never both: a record that answered both questions would have to be read twice. */
    InputGamepadDataInit(&data);
    data.button = RoboGamepadButtonA;
    data.axis = RoboGamepadAxisLeftX;
    data.value = 1;
    RoboCheck(!InputGamepadDataIsValid(&data));
    data.flags = 1u << 2;
    RoboCheck(!InputGamepadDataIsValid(&data));

    /* The vocabulary: every value has a distinct name, and its own index. */
    RoboCheckEqInt(RoboGamepadButtonCount, 16);
    RoboCheckEqInt(RoboGamepadAxisCount, 9);
    RoboCheckStrEq(InputGamepadButtonName(RoboGamepadButtonNone), "none");
    RoboCheckStrEq(InputGamepadButtonName(RoboGamepadButtonLeftBumper), "left-bumper");
    RoboCheckStrEq(InputGamepadButtonName(RoboGamepadButtonDpadUp), "dpad-up");
    RoboCheckStrEq(InputGamepadButtonName((InputGamepadButton)16), "unknown");
    RoboCheckStrEq(InputGamepadAxisName(RoboGamepadAxisRightTrigger), "right-trigger");
    RoboCheckStrEq(InputGamepadAxisName((InputGamepadAxis)9), "unknown");
    RoboCheck(InputGamepadButtonIsKnown(RoboGamepadButtonNone));
    RoboCheck(!InputGamepadButtonIsKnown((InputGamepadButton)16));
    RoboCheck(InputGamepadAxisIsKnown(RoboGamepadAxisNone));
    RoboCheck(!InputGamepadAxisIsKnown((InputGamepadAxis)9));
    for (index = 1u; index < RoboGamepadButtonCount; index++) {
        RoboCheck(strcmp(InputGamepadButtonName((InputGamepadButton)index), "unknown") != 0);
        RoboCheck(strcmp(InputGamepadButtonName((InputGamepadButton)index), "none") != 0);
    }

    InputGamepadDataInit(&data);
    InputGamepadDataInit(&other);
    RoboCheck(InputGamepadDataEqual(&data, &other));
    InputGamepadDataSetButton(&data, RoboGamepadButtonB, 1, &diag);
    RoboCheck(!InputGamepadDataEqual(&data, &other));
    RoboCheck(InputGamepadDataEqual(NULL, NULL));
    RoboCheck(!InputGamepadDataEqual(&data, NULL));
}

/* ------------------------------------------------------------------ */
/* Composition                                                         */
/* ------------------------------------------------------------------ */

static void TestPayloadComposition(void)
{
    InputCompositionData data;
    InputCompositionData other;
    InputDiag diag;
    char text[RoboInputCompositionTextMax];

    InputDiagReset(&diag);
    InputCompositionDataInit(&data);
    /* A cleared record is *empty*, and says so through the derived flag, so the two answers a consumer
     * could compute from it agree from the first byte. */
    RoboCheck(InputCompositionDataIsValid(&data));
    RoboCheck(InputCompositionDataIsEmpty(&data));
    RoboCheck((data.flags & (uint32_t)RoboInputCompositionFlagEmpty) != 0u);
    RoboCheckEqInt((int)InputCompositionDataLength(&data), 0);

    /* A preedit with a clause and a caret inside it. */
    RoboCheckEqInt(InputCompositionDataSet(&data, "nihon", 0u, 5u, 3u, 0u, &diag), RoboOk);
    RoboCheck(InputCompositionDataIsValid(&data));
    RoboCheck(!InputCompositionDataIsEmpty(&data));
    RoboCheck((data.flags & (uint32_t)RoboInputCompositionFlagEmpty) == 0u);
    RoboCheckEqInt((int)InputCompositionDataLength(&data), 5);
    RoboCheckEqInt((int)InputCompositionDataText(&data, text, sizeof(text)), 5);
    RoboCheckStrEq(text, "nihon");
    RoboCheckEqInt((int)data.clauseStart, 0);
    RoboCheckEqInt((int)data.clauseEnd, 5);
    RoboCheckEqInt((int)data.caret, 3);

    /* The `EMPTY` flag is derived in **both** directions: a caller cannot say empty while carrying text,
     * nor carry nothing while denying it. */
    RoboCheckEqInt(InputCompositionDataSet(&data, "x", 0u, 0u, 0u,
                                           RoboInputCompositionFlagEmpty, &diag),
                   RoboOk);
    RoboCheck((data.flags & (uint32_t)RoboInputCompositionFlagEmpty) == 0u);
    RoboCheck(InputCompositionDataIsValid(&data));
    RoboCheckEqInt(InputCompositionDataSet(&data, "", 0u, 0u, 0u, 0u, &diag), RoboOk);
    RoboCheck((data.flags & (uint32_t)RoboInputCompositionFlagEmpty) != 0u);
    RoboCheck(InputCompositionDataIsValid(&data));

    /* A NULL preedit is an empty one, not a refusal. */
    RoboCheckEqInt(InputCompositionDataSet(&data, NULL, 0u, 0u, 0u, 0u, &diag), RoboOk);
    RoboCheck(InputCompositionDataIsEmpty(&data));

    /* The four offsets agree with the string, or the record is refused. */
    RoboCheckEqInt(InputCompositionDataSet(&data, "abc", 0u, 3u, 4u, 0u, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputCompositionDataSet(&data, "abc", 0u, 4u, 0u, 0u, &diag),
                   RoboInvalidArgument);
    /* An inverted clause is refused rather than swapped: a caller that passed them the wrong way round has
     * a bug, and repairing it would hide the bug. */
    RoboCheckEqInt(InputCompositionDataSet(&data, "abc", 2u, 1u, 0u, 0u, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputCompositionDataSet(&data, "abc", 0u, 0u, 0u, 1u << 3, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputCompositionDataSet(NULL, "abc", 0u, 0u, 0u, 0u, &diag),
                   RoboInvalidArgument);

    /* The bound: a preedit one byte longer than the event can carry is refused, never truncated, for the
     * reason this module refuses an over-long keystroke -- a truncated preedit is different text. */
    {
        char longPreedit[RoboInputCompositionTextMax + 8];
        size_t index;

        for (index = 0u; index + 1u < sizeof(longPreedit); index++) {
            longPreedit[index] = 'a';
        }
        longPreedit[sizeof(longPreedit) - 1u] = '\0';
        RoboCheckEqInt(InputCompositionDataSet(&data, longPreedit, 0u, 0u, 0u, 0u, &diag),
                       RoboInvalidArgument);
        RoboCheck(InputCompositionDataIsValid(&data));

        /* One byte shorter fits, and its length is exactly the bound's floor. */
        longPreedit[RoboInputCompositionTextMax - 1u] = '\0';
        RoboCheckEqInt(InputCompositionDataSet(&data, longPreedit, 0u, 0u, 0u, 0u, &diag), RoboOk);
        RoboCheckEqInt((int)InputCompositionDataLength(&data),
                       (int)(RoboInputCompositionTextMax - 1u));
        RoboCheck(InputCompositionDataIsValid(&data));
    }

    /* A null clause (`clauseStart == clauseEnd` with text) is legitimate: it is what a method reports
     * between clauses, and refusing it would make a real state unrepresentable. */
    RoboCheckEqInt(InputCompositionDataSet(&data, "ab", 1u, 1u, 1u, 0u, &diag), RoboOk);
    RoboCheck(InputCompositionDataIsValid(&data));

    /* Equality, and the copy door's termination guarantee. */
    RoboCheckEqInt(InputCompositionDataSet(&data, "same", 0u, 4u, 2u,
                                           RoboInputCompositionFlagResolved, &diag),
                   RoboOk);
    RoboCheckEqInt(InputCompositionDataSet(&other, "same", 0u, 4u, 2u,
                                           RoboInputCompositionFlagResolved, &diag),
                   RoboOk);
    RoboCheck(InputCompositionDataEqual(&data, &other));
    other.caret = 3u;
    RoboCheck(!InputCompositionDataEqual(&data, &other));
    other = data;
    other.preedit[0] = 'z';
    RoboCheck(!InputCompositionDataEqual(&data, &other));
    RoboCheck(InputCompositionDataEqual(NULL, NULL));
    RoboCheck(!InputCompositionDataEqual(&data, NULL));
    {
        char small[4];

        RoboCheckEqInt((int)InputCompositionDataText(&data, small, sizeof(small)), 3);
        RoboCheckStrEq(small, "sam");
        RoboCheckEqInt((int)InputCompositionDataText(NULL, small, sizeof(small)), 0);
        RoboCheckEqInt((int)InputCompositionDataText(&data, NULL, sizeof(small)), 0);
        RoboCheckEqInt((int)InputCompositionDataText(&data, small, 0u), 0);
    }
}

/* ------------------------------------------------------------------ */
/* The device payload                                                  */
/* ------------------------------------------------------------------ */

static void TestPayloadDevice(void)
{
    InputDeviceData data;
    InputDeviceData other;
    InputDeviceDescriptor descriptor =
        InputFixtureDescriptor(RoboInputDeviceTypeKeyboard, 1u, "AT keyboard",
                               RoboInputDeviceCapabilityKeyboardKeys);
    InputDiag diag;

    InputDiagReset(&diag);
    InputDeviceDataInit(&data);
    RoboCheck(!InputDeviceDataIsValid(&data));
    RoboCheck(!InputDeviceDataIsValid(NULL));

    data.descriptor = descriptor;
    RoboCheck(InputDeviceDataIsValid(&data));
    other.descriptor = descriptor;
    RoboCheck(InputDeviceDataEqual(&data, &other));
    other.descriptor.type = RoboInputDeviceTypeMouse;
    RoboCheck(!InputDeviceDataEqual(&data, &other));
    RoboCheck(InputDeviceDataEqual(NULL, NULL));
    RoboCheck(!InputDeviceDataEqual(&data, NULL));
}

/* ------------------------------------------------------------------ */
/* The new event types                                                 */
/* ------------------------------------------------------------------ */

static void TestPayloadEventTypes(void)
{
    InputEvent event;
    InputDiag diag;

    InputDiagReset(&diag);

    /* Hotplug: the event's own identity is filled from the descriptor, so the two cannot disagree. */
    {
        InputDeviceDescriptor descriptor =
            InputFixtureDescriptor(RoboInputDeviceTypeKeyboard, 9u, "k",
                                   RoboInputDeviceCapabilityKeyboardKeys);

        InputEventInit(&event);
        RoboCheckEqInt(InputEventSetDeviceHotplug(&event, RoboInputEventDeviceConnected,
                                                  RoboInputSourceKeyboard, &descriptor, &diag),
                       RoboOk);
        RoboCheck(InputEventIsValid(&event));
        RoboCheck(InputDeviceEqual(&event.device, &descriptor.device));
        RoboCheckEqInt(InputEventPayloadKind(event.type), RoboInputPayloadDevice);

        /* A hand-built event whose identity and descriptor disagree is refused by the validator, which is
         * the property the door gives and this check proves the validator is what enforces it. */
        event.device.id = 77u;
        RoboCheck(!InputEventIsValid(&event));

        RoboCheckEqInt(InputEventSetDeviceHotplug(&event, RoboInputEventKeyDown,
                                                  RoboInputSourceKeyboard, &descriptor, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputEventSetDeviceHotplug(&event, RoboInputEventDeviceDisconnected,
                                                  RoboInputSourceKeyboard, &descriptor, &diag),
                       RoboOk);
        RoboCheck(InputEventIsValid(&event));
    }

    /* Repeat: the type and the payload's flag must agree, in both directions, because one of the two
     * answers decides whether a character is inserted. */
    {
        InputKeyboardData down = InputKeyData(RoboPhysicalKeyA, RoboLogicalKeyA,
                                                        RoboKeyActionDown, 0, "a");
        InputKeyboardData repeat = InputKeyData(RoboPhysicalKeyA, RoboLogicalKeyA,
                                                          RoboKeyActionDown, 1, "a");

        InputEventInit(&event);
        RoboCheckEqInt(InputEventSetKeyRepeat(&event, RoboInputSourceKeyboard, &down, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputEventSetKeyRepeat(&event, RoboInputSourceKeyboard, &repeat, &diag),
                       RoboOk);
        RoboCheck(InputEventIsValid(&event));
        RoboCheckEqInt(event.type, RoboInputEventKeyRepeat);
        RoboCheck(InputEventTypeIsKey(event.type));
        RoboCheckEqInt(InputKeyboardActionForEvent(RoboInputEventKeyRepeat), RoboKeyActionDown);
        RoboCheck(InputKeyboardDataRepeatMatchesEvent(&repeat, RoboInputEventKeyRepeat));
        RoboCheck(!InputKeyboardDataRepeatMatchesEvent(&repeat, RoboInputEventKeyDown));

        /* A plain `KeyDown` carrying a repeat payload is refused by the keyboard door itself. */
        RoboCheckEqInt(InputEventSetKeyboard(&event, RoboInputEventKeyDown,
                                             RoboInputSourceKeyboard, &repeat, &diag),
                       RoboInvalidArgument);
    }

    /* Pen: a positioned event, on the same planes as any pointer. */
    {
        InputPenData pen = PenSample(100, 200, 400, 0u);
        InputPointerData pressed = GetRoboInputPointerData(100, 200, RoboMouseButtonLeft,
                                                         InputButtonMask(RoboMouseButtonLeft));
        InputPenData down;

        InputPenDataInit(&down);
        RoboCheckEqInt(InputPenDataSet(&down, &pressed, 400, 0, 0, 0, 0u, &diag), RoboOk);

        InputEventInit(&event);
        RoboCheckEqInt(InputEventSetPen(&event, RoboInputEventPenDown, RoboInputSourceTablet,
                                        &down, RoboInputSpaceScreen, &diag),
                       RoboOk);
        RoboCheck(InputEventIsValid(&event));
        RoboCheck(InputEventTypeIsPen(event.type));
        RoboCheck(InputEventTypeHasPosition(event.type));
        RoboCheck(InputEventTypeIsRelease(RoboInputEventPenUp));
        RoboCheck(InputEventTypeIsRelease(RoboInputEventPenCancel));
        RoboCheck(!InputEventTypeIsRelease(RoboInputEventPenDown));

        /* The obvious mistake: a pen *move* whose payload carries a press. A stylus is a pointer, so the
         * rule is not relaxed -- the pen door and the pointer door refuse it identically. */
        RoboCheckEqInt(InputEventSetPen(&event, RoboInputEventPenMove, RoboInputSourceTablet,
                                        &down, RoboInputSpaceScreen, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputEventSetPen(&event, RoboInputEventPenMove, RoboInputSourceTablet,
                                        &pen, RoboInputSpaceScreen, &diag),
                       RoboOk);
        RoboCheck(InputEventIsValid(&event));

        /* No plane, no positioned event: the rule of §9 applies to a stylus as much as to a mouse. */
        RoboCheckEqInt(InputEventSetPen(&event, RoboInputEventPenMove, RoboInputSourceTablet,
                                        &pen, RoboInputSpaceUnknown, &diag),
                       RoboInvalidArgument);
        /* And a touch digitizer may not report a pen: a finger cannot report pressure or tilt. */
        RoboCheckEqInt(InputEventSetPen(&event, RoboInputEventPenMove, RoboInputSourceTouch,
                                        &pen, RoboInputSpaceScreen, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputEventSetPen(&event, RoboInputEventWheel, RoboInputSourceTablet,
                                        &pen, RoboInputSpaceScreen, &diag),
                       RoboInvalidArgument);
    }

    /* Gamepad: a button event carries a button, an axis event carries an axis. */
    {
        InputGamepadData button;
        InputGamepadData axis;

        InputGamepadDataInit(&button);
        InputGamepadDataInit(&axis);
        RoboCheckEqInt(InputGamepadDataSetButton(&button, RoboGamepadButtonA, 1, &diag), RoboOk);
        RoboCheckEqInt(InputGamepadDataSetAxis(&axis, RoboGamepadAxisLeftX, -1000, &diag), RoboOk);

        InputEventInit(&event);
        RoboCheckEqInt(InputEventSetGamepad(&event, RoboInputEventGamepadButtonDown,
                                            RoboInputSourceGameController, &button, &diag),
                       RoboOk);
        RoboCheck(InputEventIsValid(&event));
        RoboCheck(InputEventTypeIsGamepad(event.type));
        RoboCheck(!InputEventTypeHasPosition(event.type));
        RoboCheckEqInt(event.space, RoboInputSpaceUnknown);

        RoboCheckEqInt(InputEventSetGamepad(&event, RoboInputEventGamepadAxisChanged,
                                            RoboInputSourceGameController, &axis, &diag),
                       RoboOk);
        RoboCheck(InputEventIsValid(&event));
        /* The crossing pair, refused in both directions. */
        RoboCheckEqInt(InputEventSetGamepad(&event, RoboInputEventGamepadAxisChanged,
                                            RoboInputSourceGameController, &button, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputEventSetGamepad(&event, RoboInputEventGamepadButtonUp,
                                            RoboInputSourceGameController, &axis, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputEventSetGamepad(&event, RoboInputEventPointerMove,
                                            RoboInputSourceGameController, &axis, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputEventSetGamepad(&event, RoboInputEventGamepadButtonDown,
                                            RoboInputSourceMouse, &button, &diag),
                       RoboInvalidArgument);
    }

    /* Composition: from where keystrokes come from, with no plane of its own. */
    {
        InputCompositionData preedit;

        InputCompositionDataInit(&preedit);
        RoboCheckEqInt(InputCompositionDataSet(&preedit, "ka", 0u, 2u, 2u, 0u, &diag), RoboOk);

        InputEventInit(&event);
        RoboCheckEqInt(InputEventSetComposition(&event, RoboInputEventCompositionUpdate,
                                                RoboInputSourceKeyboard, &preedit, &diag),
                       RoboOk);
        RoboCheck(InputEventIsValid(&event));
        RoboCheck(InputEventTypeIsComposition(event.type));
        RoboCheck(!InputEventTypeHasPosition(event.type));
        RoboCheckEqInt(event.space, RoboInputSpaceUnknown);
        /* A composition is not text: only `TEXT_INPUT` carries the text that should be *inserted*. */
        RoboCheckEqInt(InputEventPayloadKind(event.type), RoboInputPayloadComposition);
        RoboCheckEqInt(InputEventSetComposition(&event, RoboInputEventTextInput,
                                                RoboInputSourceKeyboard, &preedit, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputEventSetComposition(&event, RoboInputEventCompositionStart,
                                                RoboInputSourceMouse, &preedit, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputEventSetComposition(&event, RoboInputEventCompositionCommit,
                                                RoboInputSourceKeyboard, &preedit, &diag),
                       RoboOk);
    }

    /* Focus and window: no payload, any defined source, and what they are about is the target. */
    {
        InputTarget target;

        InputEventInit(&event);
        RoboCheckEqInt(InputEventSetFocusOrWindow(&event, RoboInputEventFocusIn,
                                                  RoboInputSourceKeyboard, &diag),
                       RoboOk);
        RoboCheck(InputEventIsValid(&event));
        RoboCheck(InputEventTypeIsFocus(event.type));
        RoboCheckEqInt(InputEventPayloadKind(event.type), RoboInputPayloadNone);
        RoboCheckEqInt(event.space, RoboInputSpaceUnknown);

        InputTargetInit(&target);
        RoboCheckEqInt(InputTargetSetWindow(&target, 4u, RoboInputSpaceDocument, &diag), RoboOk);
        RoboCheckEqInt(InputEventSetTarget(&event, &target, &diag), RoboOk);
        /* A focus event has no plane of its own, so it may carry a target concluded in any -- which is the
         * rule that makes a focused node routeable at all. */
        RoboCheck(InputEventIsValid(&event));

        RoboCheckEqInt(InputEventSetFocusOrWindow(&event, RoboInputEventWindowDeactivate,
                                                  RoboInputSourceMouse, &diag),
                       RoboOk);
        RoboCheck(InputEventIsValid(&event));
        RoboCheck(InputEventTypeIsWindow(event.type));
        RoboCheckEqInt(InputEventSetFocusOrWindow(&event, RoboInputEventPointerMove,
                                                  RoboInputSourceMouse, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputEventSetFocusOrWindow(&event, RoboInputEventFocusOut,
                                                  RoboInputSourceUnknown, &diag),
                       RoboInvalidArgument);
    }

    /* Pointer enter and leave are pointer events: a plane, a position, and no button -- and the door
     * refuses a key type, which is what keeps "every positioned event has the same shape" checkable. */
    {
        InputPointerData move = GetRoboInputPointerData(1, 2, RoboMouseButtonNone, 0u);
        InputKeyboardData key = InputKeyData(RoboPhysicalKeyA, RoboLogicalKeyA,
                                                        RoboKeyActionDown, 0, "a");

        InputEventInit(&event);
        RoboCheckEqInt(InputEventSetPointer(&event, RoboInputEventPointerEnter,
                                            RoboInputSourceMouse, &move,
                                            RoboInputSpaceScreen, &diag),
                       RoboOk);
        RoboCheck(InputEventIsValid(&event));
        RoboCheck(InputEventTypeHasPosition(event.type));
        RoboCheckEqInt(InputEventSetPointer(&event, RoboInputEventPointerLeave,
                                            RoboInputSourceMouse, &move,
                                            RoboInputSpaceScreen, &diag),
                       RoboOk);
        RoboCheck(InputEventIsValid(&event));
        /* An enter with no plane is refused, exactly as a move would be. */
        RoboCheckEqInt(InputEventSetPointer(&event, RoboInputEventPointerEnter,
                                            RoboInputSourceMouse, &move,
                                            RoboInputSpaceUnknown, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputEventSetKeyboard(&event, RoboInputEventPointerEnter,
                                            RoboInputSourceMouse, &key, &diag),
                       RoboInvalidArgument);
    }

    /* Every appended type names itself, is deliverable, and has a payload kind this build defines. */
    {
        InputEventType type;
        uint32_t index;
        uint32_t documentable = 0u;

        for (index = (uint32_t)RoboInputEventDeviceConnected;
             index < (uint32_t)RoboInputEventTypeCount; index++) {
            type = (InputEventType)index;
            RoboCheck(strcmp(InputEventTypeName(type), "unknown") != 0);
            RoboCheck(InputEventTypeIsDeliverable(type));
            RoboCheck((uint32_t)InputEventPayloadKind(type) < RoboInputPayloadKindCount);
            documentable++;
        }
        RoboCheckEqInt((int)documentable, 20);
        /* Each of the seven new kinds is reachable from a type, which is what makes the union's members
         * "absent rather than meaningless" a fact about the model rather than about a comment. */
        RoboCheckEqInt(InputEventPayloadKind(RoboInputEventDeviceConnected), RoboInputPayloadDevice);
        RoboCheckEqInt(InputEventPayloadKind(RoboInputEventPenDown), RoboInputPayloadPen);
        RoboCheckEqInt(InputEventPayloadKind(RoboInputEventPenCancel), RoboInputPayloadPen);
        RoboCheckEqInt(InputEventPayloadKind(RoboInputEventGamepadAxisChanged),
                       RoboInputPayloadGamepad);
        RoboCheckEqInt(InputEventPayloadKind(RoboInputEventCompositionCommit),
                       RoboInputPayloadComposition);
        RoboCheckEqInt(InputEventPayloadKind(RoboInputEventFocusOut), RoboInputPayloadNone);
        RoboCheckEqInt(InputEventPayloadKind(RoboInputEventWindowActivate), RoboInputPayloadNone);
        RoboCheckEqInt(InputEventPayloadKind(RoboInputEventKeyRepeat), RoboInputPayloadKeyboard);
        /* No two types share a name. */
        for (index = 0u; index < (uint32_t)RoboInputEventTypeCount; index++) {
            uint32_t other;

            for (other = index + 1u; other < (uint32_t)RoboInputEventTypeCount; other++) {
                RoboCheck(strcmp(InputEventTypeName((InputEventType)index),
                                   InputEventTypeName((InputEventType)other)) != 0);
            }
        }
    }

    /* The bounds are not accidental: the event record is a *value* every queue entry copies, so a payload
     * that quietly grew would cost the queue its memory budget. Asserted rather than assumed, so a future
     * payload has to make the decision deliberately. */
    {
        /* The biggest member is what the union is: a focus-free event carrying a descriptor. */
        RoboCheck(sizeof(InputEvent) <= 320u);
        RoboCheck(sizeof(InputPayload) <= 224u);
    }
}

void TestRegisterInputPayloads(void)
{
    TestRegister("input.payload.pen", TestPayloadPen);
    TestRegister("input.payload.gamepad", TestPayloadGamepad);
    TestRegister("input.payload.composition", TestPayloadComposition);
    TestRegister("input.payload.device", TestPayloadDevice);
    TestRegister("input.payload.event_types", TestPayloadEventTypes);
}
