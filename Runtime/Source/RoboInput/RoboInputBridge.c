/*
 * RoboInput bridge (Phase 12.1) — see RoboInputBridge.h.
 *
 * The one direction this build can travel: a `WindowEvent` -- what RoboWindow's backends already
 * produce -- becomes a RoboInput event. Nothing here reads a device, opens a handle, or remembers a previous
 * event, so the translation is a pure function of the window event and the options a caller passes.
 *
 * Two things are *decided* rather than copied, and both are in the header's comment because a reader needs
 * them before trusting the output:
 *
 *   **which plane a pointer event lands in** -- RoboWindow's payload carries client and screen coordinates
 *   with a flag saying whether the second is real, and this model carries one plane per event, so the bridge
 *   takes the more precise plane that exists and says so in the event's `space`;
 *
 *   **whether a previous position exists** -- RoboWindow's delta is zero both for "did not move" and for
 *   "first sample", so `previous` is filled only where the arithmetic is provably right.
 *
 * The pressed-button mask is the third: it comes from the runtime's tracked button state rather than from
 * the event alone, because a move carries no mask and "nothing is down" would be a lie a consumer could not
 * detect. The event's own button is then applied on top, so the mask is coherent with the event even if the
 * window layer's state changed after the event was queued.
 */

#include "RoboInput/RoboInputBridge.h"

#include "RoboInputInternal.h"

#include "RoboWindow/RoboWindowKey.h"
#include "RoboWindow/RoboWindowMouse.h"

#include <stddef.h>
#include <stdint.h>

void InputBridgeOptionsInit(InputBridgeOptions *options)
{
    if (!options) {
        return;
    }
    options->mouseSource = RoboInputSourceMouse;
    options->keyboardSource = RoboInputSourceKeyboard;
    options->preferScreenCoords = 1;
    InputDeviceInit(&options->mouseDevice);
    InputDeviceInit(&options->keyboardDevice);
}

int InputBridgeCanTranslate(WindowEventType type)
{
    return InputEventTypeForWindowEvent((int)type) != RoboInputEventNone;
}

InputSpace InputBridgeSpaceForWindowMouse(const MouseEvent *source,
                                                            int preferScreenCoords)
{
    if (!source) {
        return RoboInputSpaceUnknown;
    }
    if (preferScreenCoords && source->hasScreenCoords) {
        return RoboInputSpaceScreen;
    }
    /* Client coordinates are this module's *document* plane: the plane the window's content is laid out
     * and painted on, which is the one a hit test wants. */
    return RoboInputSpaceDocument;
}

/* ------------------------------------------------------------------ */
/* Payloads                                                            */
/* ------------------------------------------------------------------ */

Result InputKeyboardDataFromWindowKeyboard(const KeyboardEvent *source,
                                                            InputKeyboardData *out,
                                                            InputDiag *diag)
{
    Result r;

    if (!source || !out) {
        return RoboInputDiag(diag, RoboInputDomainBridge, RoboInvalidArgument,
                               "a keyboard translation needs a payload and somewhere to put it");
    }
    if (source->action != RoboKeyActionDown && source->action != RoboKeyActionUp) {
        return RoboInputDiag(diag, RoboInputDomainBridge, RoboNotSupported,
                               "the window key payload has no action this model can carry");
    }
    /* The text is deliberately absent: RoboWindow's payload has no text at all, and producing text is a
     * later phase's work. A bridged key event therefore says "this key did this", not "this character was
     * typed", which is exactly what it knows. */
    r = InputKeyboardDataSet(out, source->physicalKey, source->logicalKey, source->action,
                                     source->isRepeat, NULL);
    if (r != RoboOk) {
        return RoboInputDiag(diag, RoboInputDomainBridge, r,
                               "the window key payload is not one this model may carry");
    }
    return RoboOk;
}

/** Non-zero when the runtime currently tracks @p button as pressed (RoboWindow's own state, 7.11). */
static int TrackedButton(int button)
{
    return MouseIsButtonPressed((MouseButton)button) != 0;
}

Result InputPointerDataFromWindowMouse(const MouseEvent *source,
                                                        int preferScreenCoords,
                                                        InputPointerData *out,
                                                        InputSpace *outSpace,
                                                        InputDiag *diag)
{
    InputSpace space;
    GfxPoint position;
    MouseButton button = RoboMouseButtonNone;
    uint32_t mask = 0u;
    int index;

    if (!source || !out || !outSpace) {
        return RoboInputDiag(diag, RoboInputDomainBridge, RoboInvalidArgument,
                               "a pointer translation needs a payload and somewhere to put it");
    }
    switch (source->type) {
    case RoboMouseEventMove:
        break;
    case RoboMouseEventButtonDown:
        button = source->button;
        break;
    case RoboMouseEventButtonUp:
        button = source->button;
        break;
    case RoboMouseEventWheel:
        if (!InputScrollUnitIsKnown(source->scrollUnit)) {
            return RoboInputDiag(diag, RoboInputDomainBridge, RoboNotSupported,
                                   "the window wheel payload has no unit this model can carry");
        }
        break;
    default:
        return RoboInputDiag(diag, RoboInputDomainBridge, RoboNotSupported,
                               "the window mouse payload has no type this model can carry");
    }
    if ((source->type == RoboMouseEventButtonDown || source->type == RoboMouseEventButtonUp) &&
        (button == RoboMouseButtonNone || button == RoboMouseButtonUnknown ||
         !MouseButtonIsKnown(button))) {
        return RoboInputDiag(diag, RoboInputDomainBridge, RoboNotSupported,
                               "a button event without a button this model can name");
    }

    space = InputBridgeSpaceForWindowMouse(source, preferScreenCoords);
    if (space == RoboInputSpaceScreen) {
        position = MakeGfxPoint(source->screenX, source->screenY);
    } else {
        position = MakeGfxPoint(source->clientX, source->clientY);
    }

    /* What is down right now, per the window layer's own tracking, then the event's own button applied on
     * top so that the mask cannot contradict the event it is on. */
    for (index = (int)RoboMouseButtonLeft; index <= (int)RoboMouseButtonX2; index++) {
        if (TrackedButton(index)) {
            mask = InputButtonMaskApply(mask, (MouseButton)index, 1);
        }
    }
    if (source->type == RoboMouseEventButtonDown) {
        mask = InputButtonMaskApply(mask, button, 1);
    } else if (source->type == RoboMouseEventButtonUp) {
        mask = InputButtonMaskApply(mask, button, 0);
    }

    InputPointerDataInit(out);
    out->pointer = RoboInputPointerIdPrimary;
    out->clickCount = source->clickCount;
    if (InputPointerDataSet(out, RoboInputPointerIdPrimary, position, button, mask) != RoboOk) {
        return RoboInputDiag(diag, RoboInputDomainBridge, RoboError,
                               "the translated pointer payload is not one this model may carry");
    }
    if (source->type == RoboMouseEventWheel) {
        if (InputPointerDataSetWheel(out, source->scrollX, source->scrollY,
                                              source->scrollUnit) != RoboOk) {
            return RoboInputDiag(diag, RoboInputDomainBridge, RoboNotSupported,
                                   "the window wheel payload has no unit this model can carry");
        }
    }
    if (source->deltaX != 0 || source->deltaY != 0) {
        /* A non-zero delta is proof that a previous position exists, and the previous position is that
         * delta back along the movement. A *zero* delta cannot be told apart from a first sample, so no
         * previous position is claimed. */
        GfxPoint previous = MakeGfxPoint(position.x - source->deltaX, position.y - source->deltaY);

        if (InputPointerDataSetPrevious(out, previous) != RoboOk) {
            return RoboInputDiag(diag, RoboInputDomainBridge, RoboError,
                                   "the translated previous position is not representable");
        }
    }
    if (!InputPointerDataIsValid(out)) {
        return RoboInputDiag(diag, RoboInputDomainBridge, RoboError,
                               "the translated pointer payload is not one this model may carry");
    }
    *outSpace = space;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Whole events                                                        */
/* ------------------------------------------------------------------ */

/** Non-zero when @p device carries anything at all: an id, or a name. */
static int DeviceIsFilled(const InputDevice *device)
{
    return device->id != RoboInputDeviceIdNone || device->name[0] != '\0';
}

Result InputEventFromWindowEventWithOptions(
    const InputBridgeOptions *options,
    const WindowEvent *windowEvent,
    InputEvent *out,
    InputDiag *diag)
{
    InputEventType type;
    InputSource source;
    WindowId window;
    uint64_t timestamp;
    InputModifiers modifiers;
    Result r;

    if (!windowEvent || !out) {
        return RoboInputDiag(diag, RoboInputDomainBridge, RoboInvalidArgument,
                               "a translation needs a window event and somewhere to put the result");
    }
    type = InputEventTypeForWindowEvent((int)windowEvent->type);
    if (type == RoboInputEventNone) {
        return RoboInputDiag(diag, RoboInputDomainBridge, RoboNotSupported,
                               "that window event is not an input event");
    }

    InputEventInit(out);
    out->type = type;
    window = windowEvent->windowId;
    out->display = windowEvent->displayId;

    if (InputEventTypeIsKey(type)) {
        InputKeyboardData data;

        source = options ? options->keyboardSource : RoboInputSourceKeyboard;
        if (window == RoboWindowIdNone) {
            window = windowEvent->key.windowId;
        }
        timestamp = windowEvent->key.timestampMs;
        modifiers = windowEvent->key.modifiers;
        if (InputKeyboardActionForEvent((int)type) != windowEvent->key.action) {
            return RoboInputDiag(diag, RoboInputDomainBridge, RoboNotSupported,
                                   "the window key payload's action disagrees with its event type");
        }
        r = InputKeyboardDataFromWindowKeyboard(&windowEvent->key, &data, diag);
        if (r != RoboOk) {
            return r;
        }
        r = InputEventSetKeyboard(out, type, source, &data, diag);
        if (r != RoboOk) {
            return r;
        }
        if (options && DeviceIsFilled(&options->keyboardDevice)) {
            r = InputEventSetDevice(out, &options->keyboardDevice, diag);
            if (r != RoboOk) {
                return r;
            }
        }
    } else {
        InputPointerData data;
        InputSpace space = RoboInputSpaceUnknown;

        source = options ? options->mouseSource : RoboInputSourceMouse;
        if (window == RoboWindowIdNone) {
            window = windowEvent->mouse.windowId;
        }
        timestamp = windowEvent->mouse.timestampMs;
        modifiers = windowEvent->mouse.modifiers;
        r = InputPointerDataFromWindowMouse(&windowEvent->mouse,
                                                      options ? options->preferScreenCoords : 1, &data,
                                                      &space, diag);
        if (r != RoboOk) {
            return r;
        }
        r = InputEventSetPointer(out, type, source, &data, space, diag);
        if (r != RoboOk) {
            return r;
        }
        if (options && DeviceIsFilled(&options->mouseDevice)) {
            r = InputEventSetDevice(out, &options->mouseDevice, diag);
            if (r != RoboOk) {
                return r;
            }
        }
    }

    out->window = window;
    out->timestampMs = timestamp;
    /* A modifier state the window layer filled with bits this build does not define would make the whole
     * event invalid, so it is checked here rather than left to fail later at the queue. */
    if (!InputModifiersAreKnown(modifiers)) {
        return RoboInputDiag(diag, RoboInputDomainBridge, RoboNotSupported,
                               "the window payload's modifier state has bits this build does not define");
    }
    out->modifiers = modifiers;
    return RoboOk;
}

Result InputEventFromWindowEvent(const WindowEvent *windowEvent,
                                                 InputEvent *out,
                                                 InputDiag *diag)
{
    return InputEventFromWindowEventWithOptions(NULL, windowEvent, out, diag);
}

Result InputQueuePushWindowEvent(InputQueue *queue,
                                                 const InputBridgeOptions *options,
                                                 const WindowEvent *windowEvent,
                                                 int *outTranslated,
                                                 InputDiag *diag)
{
    InputEvent event;
    Result r;

    if (outTranslated) {
        *outTranslated = 0;
    }
    r = InputEventFromWindowEventWithOptions(options, windowEvent, &event, diag);
    if (r != RoboOk) {
        return r;
    }
    r = InputQueuePush(queue, &event, diag);
    if (r != RoboOk) {
        return r;
    }
    if (outTranslated) {
        *outTranslated = 1;
    }
    return RoboOk;
}
