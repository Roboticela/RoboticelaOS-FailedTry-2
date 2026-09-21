/*
 * RoboInput event-model suite (Phase 12.1) — `input.event.*`.
 *
 * Four cases, and the third is the one that matters: `validation` walks the seven rules of
 * `InputEventIsValid` and proves each one *refuses*, by building a good event through the public
 * doors and then breaking exactly one thing about it. A validator whose refusals are untested is a validator
 * that quietly accepts, and what it accepts is the record a consumer trusts.
 *
 * The rules are tested against hand-edited records rather than through the setters, deliberately: the setters
 * are the *politeness* layer (they refuse before an event is malformed), and the validator is the *trust*
 * layer, which is what protects a caller that received an event from somewhere else. Both exist, and this
 * case is about the second.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* Creation                                                            */
/* ------------------------------------------------------------------ */

static void TestEventCreation(void)
{
    InputEvent event;

    InputEventInit(&event);
    RoboCheckEqInt(event.type, RoboInputEventNone);
    RoboCheckEqInt(event.source, RoboInputSourceUnknown);
    RoboCheckEqInt(event.device.id, RoboInputDeviceIdNone);
    RoboCheckEqInt(event.device.source, RoboInputSourceUnknown);
    RoboCheckEqInt(event.timestampMs, 0u);
    RoboCheckEqInt(event.modifiers, RoboInputModifiersNone);
    RoboCheckEqInt(event.space, RoboInputSpaceUnknown);
    RoboCheckEqInt(event.window, RoboWindowIdNone);
    RoboCheckEqInt(event.display, RoboDisplayIdNone);
    RoboCheckEqInt(event.hasTarget, 0);
    RoboCheckEqInt(event.target.kind, RoboInputTargetNone);
    RoboCheckEqInt(event.payload.pointer.pointer, RoboInputPointerIdPrimary);

    /* A cleared event is *not* a deliverable event: `NONE` is the honest cleared value and is refused, which
     * is what stops an uninitialized record from being queued as if it were something. */
    RoboCheck(!InputEventIsValid(&event));
    RoboCheck(!InputEventIsValid(NULL));

    /* Init is safe on NULL and never writes to a record it was not given. */
    InputEventInit(NULL);
    InputEventClearTarget(NULL);
}

static void TestEventTypeQueries(void)
{
    /* The table, asserted rather than inferred: one row per type, and the four facts about each. */
    struct {
        InputEventType type;
        InputPayloadKind payload;
        int position;
        int release;
        int key;
        int touch;
    } rows[] = {
        { RoboInputEventNone, RoboInputPayloadNone, 0, 0, 0, 0 },
        { RoboInputEventKeyDown, RoboInputPayloadKeyboard, 0, 0, 1, 0 },
        { RoboInputEventKeyUp, RoboInputPayloadKeyboard, 0, 0, 1, 0 },
        { RoboInputEventPointerMove, RoboInputPayloadPointer, 1, 0, 0, 0 },
        { RoboInputEventPointerDown, RoboInputPayloadPointer, 1, 0, 0, 0 },
        { RoboInputEventPointerUp, RoboInputPayloadPointer, 1, 1, 0, 0 },
        { RoboInputEventPointerCancel, RoboInputPayloadPointer, 1, 1, 0, 0 },
        { RoboInputEventWheel, RoboInputPayloadPointer, 1, 0, 0, 0 },
        { RoboInputEventTextInput, RoboInputPayloadText, 0, 0, 0, 0 },
        { RoboInputEventTouchBegin, RoboInputPayloadPointer, 1, 0, 0, 1 },
        { RoboInputEventTouchMove, RoboInputPayloadPointer, 1, 0, 0, 1 },
        { RoboInputEventTouchEnd, RoboInputPayloadPointer, 1, 1, 0, 1 },
        { RoboInputEventTouchCancel, RoboInputPayloadPointer, 1, 1, 0, 1 }
    };
    size_t index;

    for (index = 0u; index < (sizeof(rows) / sizeof(rows[0])); index++) {
        RoboCheckEqInt(InputEventPayloadKind(rows[index].type), rows[index].payload);
        RoboCheckEqInt(InputEventTypeHasPosition(rows[index].type), rows[index].position);
        RoboCheckEqInt(InputEventTypeIsRelease(rows[index].type), rows[index].release);
        RoboCheckEqInt(InputEventTypeIsKey(rows[index].type), rows[index].key);
        RoboCheckEqInt(InputEventTypeIsTouch(rows[index].type), rows[index].touch);
    }

    /* A pointer *button* type excludes the touch phases and the wheel: the predicate is about a pointer with
     * buttons, which is what a dispatcher asking "is this a click-ish event?" wants. */
    RoboCheck(InputEventTypeIsPointerButton(RoboInputEventPointerDown));
    RoboCheck(InputEventTypeIsPointerButton(RoboInputEventPointerCancel));
    RoboCheck(!InputEventTypeIsPointerButton(RoboInputEventWheel));
    RoboCheck(!InputEventTypeIsPointerButton(RoboInputEventTouchBegin));

    /* Which sources may report which types: the table that stops a touch event from a mouse. */
    RoboCheck(InputEventTypeAllowsSource(RoboInputEventKeyDown, RoboInputSourceKeyboard));
    RoboCheck(InputEventTypeAllowsSource(RoboInputEventKeyDown, RoboInputSourceVirtual));
    RoboCheck(!InputEventTypeAllowsSource(RoboInputEventKeyDown, RoboInputSourceMouse));
    RoboCheck(!InputEventTypeAllowsSource(RoboInputEventKeyDown, RoboInputSourceUnknown));
    RoboCheck(InputEventTypeAllowsSource(RoboInputEventPointerMove, RoboInputSourceTouch));
    RoboCheck(InputEventTypeAllowsSource(RoboInputEventWheel, RoboInputSourceTablet));
    RoboCheck(InputEventTypeAllowsSource(RoboInputEventTouchBegin, RoboInputSourceTouch));
    RoboCheck(InputEventTypeAllowsSource(RoboInputEventTouchBegin, RoboInputSourceTablet));
    RoboCheck(!InputEventTypeAllowsSource(RoboInputEventTouchBegin, RoboInputSourceMouse));
    RoboCheck(!InputEventTypeAllowsSource(RoboInputEventTouchBegin, RoboInputSourceKeyboard));
    RoboCheck(!InputEventTypeAllowsSource(RoboInputEventNone, RoboInputSourceMouse));

    /* The window-layer vocabulary mapping the bridge uses, exposed as a query. */
    RoboCheckEqInt(InputEventTypeForWindowEvent(RoboWindowEventKeyDown),
                      RoboInputEventKeyDown);
    RoboCheckEqInt(InputEventTypeForWindowEvent(RoboWindowEventMouseWheel),
                      RoboInputEventWheel);
    RoboCheckEqInt(InputEventTypeForWindowEvent(RoboWindowEventClosed),
                      RoboInputEventNone);
    RoboCheckEqInt(InputSourceForWindowEvent(RoboWindowEventMouseButtonDown),
                      RoboInputSourceMouse);
    RoboCheckEqInt(InputSourceForWindowEvent(RoboWindowEventKeyUp),
                      RoboInputSourceKeyboard);
    RoboCheckEqInt(InputSourceForWindowEvent(RoboWindowEventResized),
                      RoboInputSourceUnknown);
}

/* ------------------------------------------------------------------ */
/* Construction through the doors                                      */
/* ------------------------------------------------------------------ */

static void TestEventConstruction(void)
{
    InputDiag diag;
    InputEvent event;
    InputPointerData pointer = GetRoboInputPointerData(10, 20, RoboMouseButtonNone, 0u);
    InputKeyboardData key = InputKeyData(RoboPhysicalKeyA, RoboLogicalKeyA,
                                                    RoboKeyActionDown, 0, "a");
    InputDevice device;

    /* A key event: the payload, the type it implies, and a valid event at the end. */
    InputEventInit(&event);
    RoboCheckEqInt(InputEventSetKeyboard(&event, RoboInputEventKeyDown,
                                                   RoboInputSourceKeyboard, &key, NULL),
                      RoboOk);
    RoboCheck(InputEventIsValid(&event));
    RoboCheckEqInt(event.payload.keyboard.logical, RoboLogicalKeyA);

    /* A key *up* with a down payload is refused by the door, and the event keeps what it had. */
    diag = (InputDiag){ 0 };
    RoboCheckEqInt(InputEventSetKeyboard(&event, RoboInputEventKeyUp,
                                                   RoboInputSourceKeyboard, &key, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboInputDomainEvent);
    RoboCheck(diag.message != NULL);
    RoboCheckEqInt(event.type, RoboInputEventKeyDown);

    /* A pointer event needs a plane, and the door says so in the *space* domain rather than the event's. */
    InputEventInit(&event);
    diag = (InputDiag){ 0 };
    RoboCheckEqInt(InputEventSetPointer(&event, RoboInputEventPointerMove,
                                                  RoboInputSourceMouse, &pointer,
                                                  RoboInputSpaceUnknown, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboInputDomainSpace);
    RoboCheckEqInt(event.type, RoboInputEventNone);

    RoboCheckEqInt(InputEventSetPointer(&event, RoboInputEventPointerMove,
                                                  RoboInputSourceMouse, &pointer,
                                                  RoboInputSpaceScreen, NULL),
                      RoboOk);
    RoboCheck(InputEventIsValid(&event));
    RoboCheckEqInt(event.space, RoboInputSpaceScreen);
    RoboCheckEqInt(event.payload.pointer.position.x, 10);

    /* The wrong payload for the type, in both directions. */
    RoboCheckEqInt(InputEventSetKeyboard(&event, RoboInputEventPointerMove,
                                                   RoboInputSourceKeyboard, &key, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputEventSetPointer(&event, RoboInputEventKeyDown,
                                                  RoboInputSourceMouse, &pointer,
                                                  RoboInputSpaceScreen, NULL),
                      RoboInvalidArgument);

    /* Text: a door that refuses an empty string and one that refuses text longer than one event may carry. */
    InputEventInit(&event);
    RoboCheckEqInt(InputEventSetText(&event, RoboInputSourceKeyboard, "", NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputEventSetText(&event, RoboInputSourceKeyboard, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputEventSetText(&event, RoboInputSourceKeyboard,
                                               "0123456789012345678901234567890123", NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputEventSetText(&event, RoboInputSourceKeyboard, "A", NULL), RoboOk);
    RoboCheck(InputEventIsValid(&event));
    RoboCheckStrEq(event.payload.text.text, "A");

    /* Common fields: a device that agrees with the source is accepted, one that disagrees is not. */
    device = GetRoboInputDevice(1u, RoboInputSourceMouse, "Mouse");
    RoboCheckEqInt(InputEventSetDevice(&event, &device, NULL), RoboInvalidArgument);
    device = GetRoboInputDevice(1u, RoboInputSourceKeyboard, "Keyboard");
    RoboCheckEqInt(InputEventSetDevice(&event, &device, NULL), RoboOk);
    RoboCheckEqInt(event.device.id, 1u);
    RoboCheck(InputEventIsValid(&event));

    /* Modifiers, timestamps, window and display accept what they are given except for undefined bits. */
    RoboCheckEqInt(InputEventSetModifiers(&event, RoboKeyModShift, NULL), RoboOk);
    RoboCheckEqInt(InputEventSetModifiers(&event, (InputModifiers)(1u << 20), NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(event.modifiers, RoboKeyModShift);
    RoboCheckEqInt(InputEventSetTimestamp(&event, 1841u), RoboOk);
    RoboCheckEqInt(InputEventSetWindow(&event, 3u), RoboOk);
    RoboCheckEqInt(InputEventSetDisplay(&event, 1u), RoboOk);
    RoboCheck(InputEventIsValid(&event));

    /* Every door tolerates NULL by refusing. */
    RoboCheckEqInt(InputEventSetKeyboard(NULL, RoboInputEventKeyDown,
                                                   RoboInputSourceKeyboard, &key, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputEventSetKeyboard(&event, RoboInputEventKeyDown,
                                                   RoboInputSourceKeyboard, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputEventSetPointer(NULL, RoboInputEventPointerMove,
                                                  RoboInputSourceMouse, &pointer,
                                                  RoboInputSpaceScreen, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputEventSetText(NULL, RoboInputSourceKeyboard, "A", NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputEventSetTimestamp(NULL, 1u), RoboInvalidArgument);
    RoboCheckEqInt(InputEventSetWindow(NULL, 1u), RoboInvalidArgument);
    RoboCheckEqInt(InputEventSetDisplay(NULL, 1u), RoboInvalidArgument);
    RoboCheckEqInt(InputEventSetDevice(NULL, &device, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputEventSetTarget(NULL, NULL, NULL), RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Validation: the seven rules, each refusing                                 */
/* ------------------------------------------------------------------ */

static void TestEventValidation(void)
{
    InputEvent event = InputPointerMove(120, 84, 0u, RoboInputSpaceScreen, 3u);
    InputDiag diag;

    RoboCheck(InputEventIsValid(&event));

    /* 1. a deliverable type. */
    event.type = RoboInputEventNone;
    RoboCheck(!InputEventIsValid(&event));
    event.type = (InputEventType)99;
    RoboCheck(!InputEventIsValid(&event));
    event.type = RoboInputEventPointerMove;

    /* 2. a source that may report it: a pointer move from a keyboard is refused. */
    event.source = RoboInputSourceKeyboard;
    RoboCheck(!InputEventIsValid(&event));
    event.source = RoboInputSourceUnknown;
    RoboCheck(!InputEventIsValid(&event));
    event.source = RoboInputSourceMouse;
    RoboCheck(InputEventIsValid(&event));

    /* 3. a device record, agreeing with the source when it names one. */
    event.device.flags = (uint32_t)(1u << 20);
    RoboCheck(!InputEventIsValid(&event));
    event.device.flags = 0u;
    event.device.source = RoboInputSourceTouch;
    RoboCheck(!InputEventIsValid(&event));
    event.device = GetRoboInputDevice(4u, RoboInputSourceMouse, "Mouse");
    RoboCheck(InputEventIsValid(&event));
    event.device = GetRoboInputDevice(0u, RoboInputSourceUnknown, "");
    RoboCheck(InputEventIsValid(&event));

    /* 4. defined modifier bits. */
    event.modifiers = (InputModifiers)(1u << 20);
    RoboCheck(!InputEventIsValid(&event));
    event.modifiers = RoboKeyModShift | RoboKeyModCtrl;
    RoboCheck(InputEventIsValid(&event));
    event.modifiers = RoboInputModifiersNone;

    /* 5. payload coherence, one rule at a time. */
    event.payload.pointer.flags |= RoboInputPointerFlagCancelled;
    RoboCheck(!InputEventIsValid(&event));
    event.payload.pointer.flags &= ~RoboInputPointerFlagCancelled;
    RoboCheck(InputEventIsValid(&event));

    /* ... a move that names a button. */
    event.payload.pointer.button = RoboMouseButtonLeft;
    RoboCheck(!InputEventIsValid(&event));
    event.payload.pointer.button = RoboMouseButtonNone;

    /* ... a press whose own button is not in its mask. */
    event.type = RoboInputEventPointerDown;
    event.payload.pointer.button = RoboMouseButtonLeft;
    RoboCheck(!InputEventIsValid(&event));
    event.payload.pointer.buttons = InputButtonMaskApply(0u, RoboMouseButtonLeft, 1);
    RoboCheck(InputEventIsValid(&event));

    /* ... a release whose own button is still down. */
    event.type = RoboInputEventPointerUp;
    RoboCheck(!InputEventIsValid(&event));
    event.payload.pointer.buttons = 0u;
    RoboCheck(InputEventIsValid(&event));

    /* ... a wheel that names a button, and one whose unit this build does not define. */
    event.type = RoboInputEventWheel;
    RoboCheck(!InputEventIsValid(&event));
    event.payload.pointer.button = RoboMouseButtonNone;
    RoboCheck(InputEventIsValid(&event));
    event.payload.pointer.wheel.unit = (MouseScrollUnit)7;
    RoboCheck(!InputEventIsValid(&event));
    event.payload.pointer.wheel.unit = RoboMouseScrollUnitLine;

    /* ... a cancel that does not say it is one, and a non-cancel that does. */
    event.type = RoboInputEventPointerCancel;
    RoboCheck(!InputEventIsValid(&event));
    event.payload.pointer.flags |= RoboInputPointerFlagCancelled;
    RoboCheck(InputEventIsValid(&event));
    event.type = RoboInputEventPointerMove;
    RoboCheck(!InputEventIsValid(&event));
    event.payload.pointer.flags &= ~RoboInputPointerFlagCancelled;

    /* ... a touch contact holding a button, and a touch event from a mouse. */
    event.type = RoboInputEventTouchBegin;
    event.source = RoboInputSourceTouch;
    RoboCheck(InputEventIsValid(&event));
    event.payload.pointer.button = RoboMouseButtonLeft;
    event.payload.pointer.buttons = InputButtonMaskApply(0u, RoboMouseButtonLeft, 1);
    RoboCheck(!InputEventIsValid(&event));
    event.payload.pointer.button = RoboMouseButtonNone;
    event.payload.pointer.buttons = 0u;
    event.source = RoboInputSourceMouse;
    RoboCheck(!InputEventIsValid(&event));
    event.source = RoboInputSourceMouse;
    event.type = RoboInputEventPointerMove;

    /* ... a key event whose payload's action disagrees with its type, and a wordless text event. */
    event = InputKeyEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 1, RoboInputModifiersNone);
    RoboCheck(InputEventIsValid(&event));
    event.payload.keyboard.action = RoboKeyActionUp;
    RoboCheck(!InputEventIsValid(&event));
    event.payload.keyboard.action = RoboKeyActionDown;
    RoboCheck(InputEventIsValid(&event));
    {
        /* A text event built through its own door, then emptied: the *type* may not be wordless, even
         * though the payload may (a key that produced no text is ordinary). Hand-editing a key event's type
         * would not test this: the union would hand the text payload the keyboard record's own bytes. */
        InputEvent textEvent;

        InputEventInit(&textEvent);
        RoboCheckEqInt(InputEventSetText(&textEvent, RoboInputSourceKeyboard, "A", NULL),
                          RoboOk);
        RoboCheck(InputEventIsValid(&textEvent));
        textEvent.payload.text.text[0] = '\0';
        RoboCheck(!InputEventIsValid(&textEvent));
    }

    /* 6. a plane exactly when there is a position to be on it, both ways. */
    event = InputPointerMove(4, 5, 0u, RoboInputSpaceScreen, 3u);
    event.space = RoboInputSpaceUnknown;
    RoboCheck(!InputEventIsValid(&event));
    event.space = (InputSpace)9;
    RoboCheck(!InputEventIsValid(&event));
    event = InputKeyEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 1, RoboInputModifiersNone);
    event.space = RoboInputSpaceScreen;
    RoboCheck(!InputEventIsValid(&event));

    /* 7. a target that agrees with the event. */
    event = InputPointerMove(4, 5, 0u, RoboInputSpaceScreen, 3u);
    RoboCheckEqInt(InputEventSetTarget(&event,
                                                 &(InputTarget){
                                                     RoboInputTargetWindow, 3u, 0u, RoboDisplayIdNone,
                                                     { 0u, 0u, 0u }, RoboLayoutNodeIdNone, 0u,
                                                     RoboInputSpaceDocument
                                                 },
                                                 &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboInputDomainSpace);
    RoboCheck(!event.hasTarget);
    {
        InputTarget target;

        RoboCheckEqInt(InputTargetSetWindow(&target, 3u, RoboInputSpaceScreen, NULL), RoboOk);
        RoboCheckEqInt(InputEventSetTarget(&event, &target, NULL), RoboOk);
        RoboCheckEqInt(event.hasTarget, 1);
        RoboCheck(InputEventIsValid(&event));

        /* A target for another window is refused, and the event keeps the one it had. */
        RoboCheckEqInt(InputTargetSetWindow(&target, 9u, RoboInputSpaceScreen, NULL), RoboOk);
        RoboCheckEqInt(InputEventSetTarget(&event, &target, &diag), RoboInvalidArgument);
        RoboCheckEqInt(diag.domain, RoboInputDomainTarget);
        RoboCheckEqInt(event.target.window, 3u);

        /* Hand-editing the target into another plane leaves an event the validator refuses. */
        event.target.space = RoboInputSpaceDocument;
        RoboCheck(!InputEventIsValid(&event));
        event.target.space = RoboInputSpaceScreen;
        RoboCheck(InputEventIsValid(&event));

        InputEventClearTarget(&event);
        RoboCheckEqInt(event.hasTarget, 0);
        RoboCheckEqInt(event.target.kind, RoboInputTargetNone);
        RoboCheck(InputEventIsValid(&event));
    }

    /* A key event *may* carry a target: the plane is the one its routing was concluded in, and a keystroke
     * has no plane of its own to disagree with. */
    event = InputKeyEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 1, RoboInputModifiersNone);
    {
        InputTarget target;

        RoboCheckEqInt(InputTargetSetWindow(&target, 3u, RoboInputSpaceWindow, NULL), RoboOk);
        RoboCheckEqInt(InputEventSetTarget(&event, &target, NULL), RoboOk);
        RoboCheck(InputEventIsValid(&event));
        /* ... but not one naming a window the event is not about. */
        RoboCheckEqInt(InputTargetSetWindow(&target, 8u, RoboInputSpaceWindow, NULL), RoboOk);
        RoboCheckEqInt(InputEventSetTarget(&event, &target, NULL), RoboInvalidArgument);
    }
}

/* ------------------------------------------------------------------ */
/* Copies and equality                                                 */
/* ------------------------------------------------------------------ */

static void TestEventCopyEqual(void)
{
    InputEvent original = InputButtonEvent(30, 40, RoboMouseButtonLeft, 1, 3u);
    InputEvent copy;
    InputEvent other;

    RoboCheckEqInt(InputEventCopy(&copy, &original), RoboOk);
    RoboCheck(InputEventEqual(&original, &copy));
    RoboCheck(InputEventEqual(&original, &original));
    RoboCheck(InputEventEqual(NULL, NULL));
    RoboCheck(!InputEventEqual(&original, NULL));
    RoboCheckEqInt(InputEventCopy(NULL, &original), RoboInvalidArgument);
    RoboCheckEqInt(InputEventCopy(&copy, NULL), RoboInvalidArgument);

    /* Every field an event carries is part of its identity: changing any one of them makes them unequal. */
    other = original;
    other.timestampMs = 1u;
    RoboCheck(!InputEventEqual(&original, &other));
    other = original;
    other.modifiers = RoboKeyModCtrl;
    RoboCheck(!InputEventEqual(&original, &other));
    other = original;
    other.display = 2u;
    RoboCheck(!InputEventEqual(&original, &other));
    other = original;
    other.payload.pointer.position.x = 31;
    RoboCheck(!InputEventEqual(&original, &other));
    other = original;
    other.payload.pointer.clickCount = 2;
    RoboCheck(!InputEventEqual(&original, &other));
    other = original;
    other.payload.pointer.buttons = 0u;
    RoboCheck(!InputEventEqual(&original, &other));
    other = original;
    other.device = GetRoboInputDevice(2u, RoboInputSourceMouse, "Mouse");
    RoboCheck(!InputEventEqual(&original, &other));
    other = original;
    RoboCheckEqInt(InputTargetSetWindow(&other.target, 3u, RoboInputSpaceScreen, NULL),
                      RoboOk);
    other.hasTarget = 1;
    RoboCheck(!InputEventEqual(&original, &other));

    /* Two key events compare their text, which is the payload field most likely to differ by one byte. */
    original = InputKeyEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 1, RoboInputModifiersNone);
    copy = original;
    RoboCheck(InputEventEqual(&original, &copy));
    copy.payload.keyboard.text[0] = 'b';
    RoboCheck(!InputEventEqual(&original, &copy));
    copy = original;
    copy.payload.keyboard.repeat = 1;
    RoboCheck(!InputEventEqual(&original, &copy));
}

/* ------------------------------------------------------------------ */
/* Positions                                                           */
/* ------------------------------------------------------------------ */

static void TestEventPosition(void)
{
    InputEvent event = InputPointerMove(120, 84, 0u, RoboInputSpaceWindow, 3u);
    InputRoute route = GetRoboInputRoute(10, 20, 400, 300, 1, 25, 398, 274);
    GfxPoint point = { 0, 0 };
    InputDiag diag;

    RoboCheckEqInt(InputEventPosition(&event, &point, NULL), RoboOk);
    RoboCheckEqInt(point.x, 120);
    RoboCheckEqInt(point.y, 84);

    /* A type with no position says so rather than answering with a zeroed point. */
    {
        InputEvent key = InputKeyEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 1,
                                                 RoboInputModifiersNone);

        RoboCheckEqInt(InputEventPosition(&key, &point, &diag), RoboNotSupported);
        RoboCheckEqInt(diag.domain, RoboInputDomainSpace);
        RoboCheckEqInt(InputEventPosition(NULL, &point, NULL), RoboInvalidArgument);
        RoboCheckEqInt(InputEventPosition(&event, NULL, NULL), RoboInvalidArgument);
        RoboCheckEqInt(InputEventPositionIn(&key, RoboInputSpaceDocument, &route, &point,
                                                      NULL),
                          RoboNotSupported);
    }

    /* Window → document, the conversion a hit test needs; the frame's borders and title band leave the
     * position, and the client inset is what leaves it. */
    RoboCheckEqInt(InputEventPositionIn(&event, RoboInputSpaceDocument, &route, &point, NULL),
                      RoboOk);
    RoboCheckEqInt(point.x, 119);
    RoboCheckEqInt(point.y, 59);

    /* Window → screen: the frame's origin is added. */
    RoboCheckEqInt(InputEventPositionIn(&event, RoboInputSpaceScreen, &route, &point, NULL),
                      RoboOk);
    RoboCheckEqInt(point.x, 130);
    RoboCheckEqInt(point.y, 104);

    /* The event is not modified: where it was is where it stays. */
    RoboCheckEqInt(event.space, RoboInputSpaceWindow);
    RoboCheckEqInt(event.payload.pointer.position.x, 120);

    /* A conversion that needs geometry the caller did not supply is refused, and says which geometry. */
    {
        InputRoute frameOnly = InputRouteFrameOnly(10, 20, 400, 300);
        InputDiag spaceDiag;

        RoboCheckEqInt(InputEventPositionIn(&event, RoboInputSpaceDocument, &frameOnly,
                                                      &point, &spaceDiag),
                          RoboNotSupported);
        RoboCheckEqInt(spaceDiag.domain, RoboInputDomainSpace);
        RoboCheck(spaceDiag.message != NULL);
    }
}

void TestRegisterInputEvent(void)
{
    TestRegister("input.event.creation", TestEventCreation);
    TestRegister("input.event.type_queries", TestEventTypeQueries);
    TestRegister("input.event.construction", TestEventConstruction);
    TestRegister("input.event.validation", TestEventValidation);
    TestRegister("input.event.copy_equal", TestEventCopyEqual);
    TestRegister("input.event.position", TestEventPosition);
}
