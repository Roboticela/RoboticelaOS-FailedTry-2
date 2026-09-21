/*
 * RoboInput bridge suite (Phase 12.1) — `input.bridge.*`.
 *
 * The bridge is the one thing in this module that turns *today's* events into the model, so its cases are
 * about the three decisions its header states rather than about the plumbing:
 *
 *   **which plane** a bridged pointer event lands in, which is a choice between RoboWindow's two coordinate
 *   pairs and depends on whether the payload's screen coordinates are real;
 *
 *   **whether a previous position exists**, which is knowable only from a non-zero delta;
 *
 *   **what is left empty**, because the window layer has no text and no device: an empty text and an unset
 *   device are *valid* records, and a case asserts both rather than trusting that they are.
 *
 * The last case is the refusal behaviour: a lifecycle window event (a window being closed, focused or
 * resized) is not an input event, and the bridge says so **without writing the caller's output**, so a loop
 * over a window queue cannot mistake a skipped event for a translated one.
 */

#include "RoboInputTestSupport.h"

#include "RoboInput/RoboInputBridge.h"

#include "RoboWindow/RoboWindowMouse.h"

/* ------------------------------------------------------------------ */
/* Which events translate                                              */
/* ------------------------------------------------------------------ */

static void TestBridgeReach(void)
{
    RoboCheckEqInt(InputBridgeCanTranslate(RoboWindowEventKeyDown), 1);
    RoboCheckEqInt(InputBridgeCanTranslate(RoboWindowEventKeyUp), 1);
    RoboCheckEqInt(InputBridgeCanTranslate(RoboWindowEventMouseMove), 1);
    RoboCheckEqInt(InputBridgeCanTranslate(RoboWindowEventMouseButtonDown), 1);
    RoboCheckEqInt(InputBridgeCanTranslate(RoboWindowEventMouseButtonUp), 1);
    RoboCheckEqInt(InputBridgeCanTranslate(RoboWindowEventMouseWheel), 1);

    /* Lifecycle events are the window system's business, and there is no input event for them: focus in
     * particular is a later phase's, by the brief's own §22. */
    RoboCheckEqInt(InputBridgeCanTranslate(RoboWindowEventCreated), 0);
    RoboCheckEqInt(InputBridgeCanTranslate(RoboWindowEventClosed), 0);
    RoboCheckEqInt(InputBridgeCanTranslate(RoboWindowEventFocusGained), 0);
    RoboCheckEqInt(InputBridgeCanTranslate(RoboWindowEventResized), 0);
    RoboCheckEqInt(InputBridgeCanTranslate(RoboWindowEventNone), 0);

    /* The options: defaults first, then a caller's own. */
    {
        InputBridgeOptions options;

        InputBridgeOptionsInit(&options);
        RoboCheckEqInt(options.mouseSource, RoboInputSourceMouse);
        RoboCheckEqInt(options.keyboardSource, RoboInputSourceKeyboard);
        RoboCheckEqInt(options.preferScreenCoords, 1);
        RoboCheckEqInt(options.mouseDevice.id, RoboInputDeviceIdNone);
        RoboCheckEqInt(options.keyboardDevice.id, RoboInputDeviceIdNone);
        InputBridgeOptionsInit(NULL);
    }
}

/* ------------------------------------------------------------------ */
/* Keys                                                                */
/* ------------------------------------------------------------------ */

static void TestBridgeKeyboard(void)
{
    WindowEvent windowEvent;
    InputEvent event;
    InputDiag diag;

    WindowEventInit(&windowEvent);
    windowEvent.type = RoboWindowEventKeyDown;
    windowEvent.windowId = 3u;
    windowEvent.key.action = RoboKeyActionDown;
    windowEvent.key.physicalKey = RoboPhysicalKeyA;
    windowEvent.key.logicalKey = RoboLogicalKeyA;
    windowEvent.key.modifiers = RoboKeyModShift;
    windowEvent.key.timestampMs = 512u;
    windowEvent.key.windowId = 3u;

    InputEventInit(&event);
    RoboCheckEqInt(InputEventFromWindowEvent(&windowEvent, &event, NULL), RoboOk);
    RoboCheck(InputEventIsValid(&event));
    RoboCheckEqInt(event.type, RoboInputEventKeyDown);
    RoboCheckEqInt(event.source, RoboInputSourceKeyboard);
    RoboCheckEqInt(event.window, 3u);
    RoboCheckEqInt(event.timestampMs, 512u);
    RoboCheckEqInt(event.modifiers, RoboKeyModShift);
    /* A keystroke has no position, so it names no plane -- rule 6 of the event model, visible here. */
    RoboCheckEqInt(event.space, RoboInputSpaceUnknown);
    RoboCheckEqInt(event.payload.keyboard.physical, RoboPhysicalKeyA);
    RoboCheckEqInt(event.payload.keyboard.logical, RoboLogicalKeyA);
    RoboCheckEqInt(event.payload.keyboard.action, RoboKeyActionDown);
    /* Nothing told the bridge which device this came from, so the record is empty -- and empty is valid. */
    RoboCheckEqInt(event.device.id, RoboInputDeviceIdNone);
    RoboCheckStrEq(event.payload.keyboard.text, "");

    /* A key up, with the window event's own id unset so the payload's id is what identifies the window. */
    windowEvent.type = RoboWindowEventKeyUp;
    windowEvent.windowId = RoboWindowIdNone;
    windowEvent.key.action = RoboKeyActionUp;
    windowEvent.key.isRepeat = 0;
    InputEventInit(&event);
    RoboCheckEqInt(InputEventFromWindowEvent(&windowEvent, &event, NULL), RoboOk);
    RoboCheckEqInt(event.type, RoboInputEventKeyUp);
    RoboCheckEqInt(event.window, 3u);
    RoboCheckEqInt(event.payload.keyboard.action, RoboKeyActionUp);

    /* A payload whose action disagrees with the window event's type is refused rather than guessed: the
     * bridge will not decide which of the two the producer meant. */
    windowEvent.key.action = RoboKeyActionDown;
    diag = (InputDiag){ 0 };
    RoboCheckEqInt(InputEventFromWindowEvent(&windowEvent, &event, &diag), RoboNotSupported);
    RoboCheckEqInt(diag.domain, RoboInputDomainBridge);
    RoboCheck(diag.message != NULL);

    /* ... and a payload with no action at all is refused by the payload translator directly. */
    {
        KeyboardEvent bare;
        InputKeyboardData data;

        KeyboardEventInit(&bare);
        bare.physicalKey = RoboPhysicalKeyA;
        bare.logicalKey = RoboLogicalKeyA;
        RoboCheckEqInt(InputKeyboardDataFromWindowKeyboard(&bare, &data, &diag),
                          RoboNotSupported);
        RoboCheckEqInt(diag.domain, RoboInputDomainBridge);
        RoboCheckEqInt(InputKeyboardDataFromWindowKeyboard(NULL, &data, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(InputKeyboardDataFromWindowKeyboard(&bare, NULL, NULL),
                          RoboInvalidArgument);

        /* The translator itself: identities and the repeat copied, the text deliberately empty. */
        bare.action = RoboKeyActionDown;
        bare.isRepeat = 3;
        RoboCheckEqInt(InputKeyboardDataFromWindowKeyboard(&bare, &data, NULL), RoboOk);
        RoboCheckEqInt(data.repeat, 1);
        RoboCheckEqInt(data.scanCode, 0u);
        RoboCheckStrEq(data.text, "");
        RoboCheck(InputKeyboardDataIsValid(&data));
    }

    /* A caller's own source and device are stamped on the event, which is how a real backend will report a
     * device identity through this door before an adapter exists. */
    {
        InputBridgeOptions options;

        InputBridgeOptionsInit(&options);
        options.keyboardSource = RoboInputSourceRemote;
        options.keyboardDevice = GetRoboInputDevice(9u, RoboInputSourceRemote, "Remote keyboard");
        windowEvent.type = RoboWindowEventKeyDown;
        windowEvent.key.action = RoboKeyActionDown;
        InputEventInit(&event);
        RoboCheckEqInt(InputEventFromWindowEventWithOptions(&options, &windowEvent, &event,
                                                                        NULL),
                          RoboOk);
        RoboCheckEqInt(event.source, RoboInputSourceRemote);
        RoboCheckEqInt(event.device.id, 9u);
        RoboCheckStrEq(event.device.name, "Remote keyboard");
        RoboCheck(InputEventIsValid(&event));

        /* A device whose source disagrees with the event's is refused rather than stamped. */
        options.keyboardDevice = GetRoboInputDevice(9u, RoboInputSourceMouse, "Mouse");
        InputEventInit(&event);
        RoboCheckEqInt(InputEventFromWindowEventWithOptions(&options, &windowEvent, &event,
                                                                        &diag),
                          RoboInvalidArgument);
        RoboCheckEqInt(diag.domain, RoboInputDomainEvent);

        /* A modifier state with bits the runtime does not define is refused: the record would be invalid
         * everywhere above, and the bridge is where it can still be named. */
        InputBridgeOptionsInit(&options);
        windowEvent.key.modifiers = (KeyModifierFlags)(1u << 25);
        InputEventInit(&event);
        RoboCheckEqInt(InputEventFromWindowEventWithOptions(&options, &windowEvent, &event,
                                                                        &diag),
                          RoboNotSupported);
        RoboCheckEqInt(diag.domain, RoboInputDomainBridge);
    }

    /* NULL and an unknown type. */
    RoboCheckEqInt(InputEventFromWindowEvent(NULL, &event, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputEventFromWindowEvent(&windowEvent, NULL, NULL), RoboInvalidArgument);
    windowEvent.type = RoboWindowEventNone;
    RoboCheckEqInt(InputEventFromWindowEvent(&windowEvent, &event, NULL),
                      RoboNotSupported);
}

/* ------------------------------------------------------------------ */
/* Pointers                                                            */
/* ------------------------------------------------------------------ */

static void TestBridgePointer(void)
{
    WindowEvent windowEvent;
    InputEvent event;
    InputDiag diag;

    WindowEventInit(&windowEvent);
    windowEvent.type = RoboWindowEventMouseMove;
    windowEvent.windowId = 3u;
    windowEvent.mouse.type = RoboMouseEventMove;
    windowEvent.mouse.clientX = 120;
    windowEvent.mouse.clientY = 84;
    windowEvent.mouse.screenX = 130;
    windowEvent.mouse.screenY = 104;
    windowEvent.mouse.hasScreenCoords = 1;
    windowEvent.mouse.windowId = 3u;
    windowEvent.mouse.modifiers = RoboKeyModCtrl;
    windowEvent.mouse.timestampMs = 1841u;

    /* With real screen coordinates the event is in **screen** space, and the position is the screen one. */
    InputEventInit(&event);
    RoboCheckEqInt(InputEventFromWindowEvent(&windowEvent, &event, NULL), RoboOk);
    RoboCheck(InputEventIsValid(&event));
    RoboCheckEqInt(event.type, RoboInputEventPointerMove);
    RoboCheckEqInt(event.source, RoboInputSourceMouse);
    RoboCheckEqInt(event.space, RoboInputSpaceScreen);
    RoboCheckEqInt(event.payload.pointer.position.x, 130);
    RoboCheckEqInt(event.payload.pointer.position.y, 104);
    RoboCheckEqInt(event.modifiers, RoboKeyModCtrl);
    RoboCheckEqInt(event.payload.pointer.pointer, RoboInputPointerIdPrimary);
    /* A zero delta cannot be told apart from a first sample, so no previous position is claimed -- and the
     * dump says `previous=none` rather than inventing a point. */
    RoboCheckEqInt(event.payload.pointer.hasPrevious, 0);
    RoboCheckEqInt(event.payload.pointer.deltaX, 0);

    /* Without them the event is in **document** space, and the position is the client one; a caller that
     * needs screen space converts with geometry it supplies. */
    windowEvent.mouse.hasScreenCoords = 0;
    InputEventInit(&event);
    RoboCheckEqInt(InputEventFromWindowEvent(&windowEvent, &event, NULL), RoboOk);
    RoboCheckEqInt(event.space, RoboInputSpaceDocument);
    RoboCheckEqInt(event.payload.pointer.position.x, 120);
    RoboCheckEqInt(event.payload.pointer.position.y, 84);
    RoboCheckEqInt(InputBridgeSpaceForWindowMouse(&windowEvent.mouse, 1),
                      RoboInputSpaceDocument);

    /* A caller may refuse the screen pair even when it is there. */
    {
        InputBridgeOptions options;

        InputBridgeOptionsInit(&options);
        options.preferScreenCoords = 0;
        windowEvent.mouse.hasScreenCoords = 1;
        InputEventInit(&event);
        RoboCheckEqInt(InputEventFromWindowEventWithOptions(&options, &windowEvent, &event,
                                                                        NULL),
                          RoboOk);
        RoboCheckEqInt(event.space, RoboInputSpaceDocument);
        RoboCheckEqInt(event.payload.pointer.position.x, 120);
        RoboCheckEqInt(InputBridgeSpaceForWindowMouse(&windowEvent.mouse, 0),
                          RoboInputSpaceDocument);
        RoboCheckEqInt(InputBridgeSpaceForWindowMouse(NULL, 1), RoboInputSpaceUnknown);
    }

    /* A non-zero delta *is* proof of a previous position, and the previous point is the delta back along
     * the movement -- in the plane the event landed in, which is why the client pair is the one asked for
     * here. */
    windowEvent.mouse.hasScreenCoords = 0;
    windowEvent.mouse.deltaX = 2;
    windowEvent.mouse.deltaY = -4;
    InputEventInit(&event);
    RoboCheckEqInt(InputEventFromWindowEvent(&windowEvent, &event, NULL), RoboOk);
    RoboCheckEqInt(event.payload.pointer.hasPrevious, 1);
    RoboCheckEqInt(event.payload.pointer.previous.x, 118);
    RoboCheckEqInt(event.payload.pointer.previous.y, 88);
    RoboCheckEqInt(event.payload.pointer.deltaX, 2);
    RoboCheckEqInt(event.payload.pointer.deltaY, -4);
    windowEvent.mouse.deltaX = 0;
    windowEvent.mouse.deltaY = 0;

    /* A button event carries the button, and its mask follows the runtime's tracked state plus the event
     * itself -- so a press has its bit and a release does not. */
    windowEvent.type = RoboWindowEventMouseButtonDown;
    windowEvent.mouse.type = RoboMouseEventButtonDown;
    windowEvent.mouse.button = RoboMouseButtonRight;
    InputEventInit(&event);
    RoboCheckEqInt(InputEventFromWindowEvent(&windowEvent, &event, NULL), RoboOk);
    RoboCheckEqInt(event.type, RoboInputEventPointerDown);
    RoboCheckEqInt(event.payload.pointer.button, RoboMouseButtonRight);
    RoboCheck(InputButtonMaskHas(event.payload.pointer.buttons, RoboMouseButtonRight));
    RoboCheckEqInt(event.payload.pointer.clickCount, 0);

    windowEvent.type = RoboWindowEventMouseButtonUp;
    windowEvent.mouse.type = RoboMouseEventButtonUp;
    InputEventInit(&event);
    RoboCheckEqInt(InputEventFromWindowEvent(&windowEvent, &event, NULL), RoboOk);
    RoboCheckEqInt(event.type, RoboInputEventPointerUp);
    RoboCheck(!InputButtonMaskHas(event.payload.pointer.buttons, RoboMouseButtonRight));

    /* A button event with no button this build can name is refused: "something was pressed, unknown what"
     * is not a record the model may carry. */
    windowEvent.mouse.button = RoboMouseButtonUnknown;
    diag = (InputDiag){ 0 };
    RoboCheckEqInt(InputEventFromWindowEvent(&windowEvent, &event, &diag), RoboNotSupported);
    RoboCheckEqInt(diag.domain, RoboInputDomainBridge);
    windowEvent.mouse.button = RoboMouseButtonNone;
    RoboCheckEqInt(InputEventFromWindowEvent(&windowEvent, &event, &diag), RoboNotSupported);

    /* The wheel: deltas and their unit, and a unit this build does not define refused. */
    windowEvent.type = RoboWindowEventMouseWheel;
    windowEvent.mouse.type = RoboMouseEventWheel;
    windowEvent.mouse.scrollX = 0;
    windowEvent.mouse.scrollY = 120;
    windowEvent.mouse.scrollUnit = RoboMouseScrollUnitLine;
    InputEventInit(&event);
    RoboCheckEqInt(InputEventFromWindowEvent(&windowEvent, &event, NULL), RoboOk);
    RoboCheckEqInt(event.type, RoboInputEventWheel);
    RoboCheckEqInt(event.payload.pointer.wheel.deltaY, 120);
    RoboCheckEqInt(event.payload.pointer.wheel.unit, RoboMouseScrollUnitLine);
    RoboCheckEqInt(event.payload.pointer.button, RoboMouseButtonNone);

    windowEvent.mouse.scrollUnit = (MouseScrollUnit)7;
    diag = (InputDiag){ 0 };
    RoboCheckEqInt(InputEventFromWindowEvent(&windowEvent, &event, &diag), RoboNotSupported);
    RoboCheckEqInt(diag.domain, RoboInputDomainBridge);

    /* A mouse payload with no type at all, and the translator's own argument checks. */
    {
        MouseEvent bare;
        InputPointerData data;
        InputSpace space = RoboInputSpaceUnknown;

        MouseEventInit(&bare);
        RoboCheckEqInt(InputPointerDataFromWindowMouse(&bare, 1, &data, &space, &diag),
                          RoboNotSupported);
        RoboCheckEqInt(diag.domain, RoboInputDomainBridge);
        RoboCheckEqInt(InputPointerDataFromWindowMouse(NULL, 1, &data, &space, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(InputPointerDataFromWindowMouse(&bare, 1, NULL, &space, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(InputPointerDataFromWindowMouse(&bare, 1, &data, NULL, NULL),
                          RoboInvalidArgument);
    }
}

/* ------------------------------------------------------------------ */
/* Into a queue, and leaving lifecycle events alone                    */
/* ------------------------------------------------------------------ */

static void TestBridgeQueue(void)
{
    InputQueue *queue = GetRoboInputQueue(8u);
    WindowEvent windowEvent;
    InputEvent out;
    int translated = -1;
    InputDiag diag;

    WindowEventInit(&windowEvent);
    windowEvent.type = RoboWindowEventMouseMove;
    windowEvent.windowId = 3u;
    windowEvent.mouse.type = RoboMouseEventMove;
    windowEvent.mouse.clientX = 5;
    windowEvent.mouse.clientY = 6;
    windowEvent.mouse.windowId = 3u;

    RoboCheckEqInt(InputQueuePushWindowEvent(queue, NULL, &windowEvent, &translated, NULL),
                      RoboOk);
    RoboCheckEqInt(translated, 1);
    RoboCheckEqInt(InputQueueCount(queue), 1u);
    RoboCheckEqInt(InputQueuePop(queue, &out, NULL), RoboOk);
    RoboCheckEqInt(out.type, RoboInputEventPointerMove);
    RoboCheckEqInt(out.payload.pointer.position.x, 5);

    /* A lifecycle event translates to nothing, pushes nothing, and says so through the flag a caller
     * looping over a window queue actually reads. */
    windowEvent.type = RoboWindowEventClosed;
    translated = -1;
    diag = (InputDiag){ 0 };
    RoboCheckEqInt(InputQueuePushWindowEvent(queue, NULL, &windowEvent, &translated, &diag),
                      RoboNotSupported);
    RoboCheckEqInt(translated, 0);
    RoboCheckEqInt(diag.domain, RoboInputDomainBridge);
    RoboCheckEqInt(InputQueueCount(queue), 0u);

    /* A full queue's refusal is the *queue's*, and the caller can tell the two apart by the domain. */
    {
        InputQueue *small = GetRoboInputQueue(1u);
        InputEvent event = InputPointerMove(1, 1, 0u, RoboInputSpaceScreen, 3u);

        RoboInputPush(small, &event);
        windowEvent.type = RoboWindowEventMouseMove;
        translated = -1;
        diag = (InputDiag){ 0 };
        RoboCheckEqInt(InputQueuePushWindowEvent(small, NULL, &windowEvent, &translated,
                                                            &diag),
                          RoboBusy);
        RoboCheckEqInt(translated, 0);
        RoboCheckEqInt(diag.domain, RoboInputDomainQueue);
        InputQueueDestroy(small);
    }

    /* The whole traversal a consumer writes: drain a window queue through the bridge, and see exactly the
     * input events -- in order -- with the lifecycle traffic filtered out. */
    {
        InputQueue *sink = GetRoboInputQueue(8u);
        WindowEvent sequence[4];
        int index;
        int pushed = 0;

        WindowEventInit(&sequence[0]);
        sequence[0].type = RoboWindowEventFocusGained;
        sequence[0].windowId = 3u;
        WindowEventInit(&sequence[1]);
        sequence[1].type = RoboWindowEventKeyDown;
        sequence[1].windowId = 3u;
        sequence[1].key.action = RoboKeyActionDown;
        sequence[1].key.physicalKey = RoboPhysicalKeyA;
        sequence[1].key.logicalKey = RoboLogicalKeyA;
        sequence[1].key.windowId = 3u;
        WindowEventInit(&sequence[2]);
        sequence[2].type = RoboWindowEventResized;
        sequence[2].windowId = 3u;
        WindowEventInit(&sequence[3]);
        sequence[3].type = RoboWindowEventKeyUp;
        sequence[3].windowId = 3u;
        sequence[3].key.action = RoboKeyActionUp;
        sequence[3].key.physicalKey = RoboPhysicalKeyA;
        sequence[3].key.logicalKey = RoboLogicalKeyA;
        sequence[3].key.windowId = 3u;

        for (index = 0; index < 4; index++) {
            int wasTranslated = 0;

            if (InputQueuePushWindowEvent(sink, NULL, &sequence[index], &wasTranslated, NULL) ==
                RoboOk) {
                pushed++;
                RoboCheckEqInt(wasTranslated, 1);
            }
        }
        RoboCheckEqInt(pushed, 2);
        RoboCheckEqInt(InputQueueCount(sink), 2u);

        RoboCheckEqInt(InputQueuePop(sink, &out, NULL), RoboOk);
        RoboCheckEqInt(out.type, RoboInputEventKeyDown);
        RoboCheckEqInt(out.payload.keyboard.action, RoboKeyActionDown);
        RoboCheckEqInt(InputQueuePop(sink, &out, NULL), RoboOk);
        RoboCheckEqInt(out.type, RoboInputEventKeyUp);
        RoboCheckEqInt(InputQueuePeek(sink, &out, NULL), RoboNotFound);

        InputQueueDestroy(sink);
    }

    windowEvent.type = RoboWindowEventMouseMove;
    windowEvent.mouse.type = RoboMouseEventMove;
    RoboCheckEqInt(InputQueuePushWindowEvent(NULL, NULL, &windowEvent, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputQueuePushWindowEvent(queue, NULL, NULL, NULL, NULL),
                      RoboInvalidArgument);

    InputQueueDestroy(queue);
}

void TestRegisterInputBridge(void)
{
    TestRegister("input.bridge.reach", TestBridgeReach);
    TestRegister("input.bridge.keyboard", TestBridgeKeyboard);
    TestRegister("input.bridge.pointer", TestBridgePointer);
    TestRegister("input.bridge.queue", TestBridgeQueue);
}
