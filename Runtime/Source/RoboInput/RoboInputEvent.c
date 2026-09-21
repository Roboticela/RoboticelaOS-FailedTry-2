/*
 * RoboInput event model (Phase 12.1) — see RoboInputEvent.h.
 *
 * Two things live here and nothing else: the **type vocabulary** (what a type is, which payload it owns,
 * which sources may report it, whether it has a position) and the **validator** that refuses a record whose
 * fields disagree about what it is. Every `set_*` door is a guarded assignment; the guards are the point.
 *
 * The validator is deliberately the strictest thing in the module, because an input event is the one record
 * in this runtime that is produced by hardware-adjacent code, crosses a boundary, and is then *trusted* by
 * everything above it. A rule that is not enforced here is a rule every consumer would have to re-check, and
 * the one that would be forgotten is always the same one: which plane a coordinate is on.
 */

#include "RoboInput/RoboInputEvent.h"

#include "RoboInputInternal.h"

#include "RoboWindow/RoboWindowEvent.h"

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Text payload                                                        */
/* ------------------------------------------------------------------ */

void InputTextDataInit(InputTextData *data)
{
    if (!data) {
        return;
    }
    data->text[0] = '\0';
}

Result InputTextDataSet(InputTextData *data, const char *text)
{
    if (!data) {
        return RoboInvalidArgument;
    }
    if (!RiTextFits(text, RoboInputTextMax)) {
        InputTextDataInit(data);
        return RoboInvalidArgument;
    }
    RiTextCopy(data->text, RoboInputTextMax, text);
    return RoboOk;
}

int InputTextDataIsValid(const InputTextData *data)
{
    return data != NULL && RiBoundedStrlen(data->text, RoboInputTextMax) < RoboInputTextMax;
}

size_t InputTextDataLength(const InputTextData *data)
{
    return data ? RiBoundedStrlen(data->text, RoboInputTextMax) : 0u;
}

int InputTextDataEqual(const InputTextData *a, const InputTextData *b)
{
    size_t index;

    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    for (index = 0u; index < RoboInputTextMax; index++) {
        if (a->text[index] != b->text[index]) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The type vocabulary                                                 */
/* ------------------------------------------------------------------ */

int InputEventTypeIsKnown(InputEventType type)
{
    return type >= RoboInputEventNone && type < (InputEventType)RoboInputEventTypeCount;
}

int InputEventTypeIsDeliverable(InputEventType type)
{
    return type > RoboInputEventNone && type < (InputEventType)RoboInputEventTypeCount;
}

const char *InputEventTypeName(InputEventType type)
{
    switch (type) {
    case RoboInputEventNone:
        return "None";
    case RoboInputEventKeyDown:
        return "KeyDown";
    case RoboInputEventKeyUp:
        return "KeyUp";
    case RoboInputEventPointerMove:
        return "PointerMove";
    case RoboInputEventPointerDown:
        return "PointerDown";
    case RoboInputEventPointerUp:
        return "PointerUp";
    case RoboInputEventPointerCancel:
        return "PointerCancel";
    case RoboInputEventWheel:
        return "Wheel";
    case RoboInputEventTextInput:
        return "TextInput";
    case RoboInputEventTouchBegin:
        return "TouchBegin";
    case RoboInputEventTouchMove:
        return "TouchMove";
    case RoboInputEventTouchEnd:
        return "TouchEnd";
    case RoboInputEventTouchCancel:
        return "TouchCancel";
    default:
        break;
    }
    return "unknown";
}

InputPayloadKind InputEventPayloadKind(InputEventType type)
{
    switch (type) {
    case RoboInputEventKeyDown:
    case RoboInputEventKeyUp:
        return RoboInputPayloadKeyboard;
    case RoboInputEventPointerMove:
    case RoboInputEventPointerDown:
    case RoboInputEventPointerUp:
    case RoboInputEventPointerCancel:
    case RoboInputEventWheel:
    case RoboInputEventTouchBegin:
    case RoboInputEventTouchMove:
    case RoboInputEventTouchEnd:
    case RoboInputEventTouchCancel:
        return RoboInputPayloadPointer;
    case RoboInputEventTextInput:
        return RoboInputPayloadText;
    default:
        break;
    }
    return RoboInputPayloadNone;
}

const char *InputPayloadKindName(InputPayloadKind kind)
{
    switch (kind) {
    case RoboInputPayloadNone:
        return "none";
    case RoboInputPayloadKeyboard:
        return "keyboard";
    case RoboInputPayloadPointer:
        return "pointer";
    case RoboInputPayloadText:
        return "text";
    default:
        break;
    }
    return "unknown";
}

int InputEventTypeIsKey(InputEventType type)
{
    return type == RoboInputEventKeyDown || type == RoboInputEventKeyUp;
}

int InputEventTypeIsPointerButton(InputEventType type)
{
    return type == RoboInputEventPointerMove || type == RoboInputEventPointerDown ||
           type == RoboInputEventPointerUp || type == RoboInputEventPointerCancel;
}

int InputEventTypeIsTouch(InputEventType type)
{
    return type == RoboInputEventTouchBegin || type == RoboInputEventTouchMove ||
           type == RoboInputEventTouchEnd || type == RoboInputEventTouchCancel;
}

int InputEventTypeHasPosition(InputEventType type)
{
    return InputEventPayloadKind(type) == RoboInputPayloadPointer;
}

int InputEventTypeIsRelease(InputEventType type)
{
    return type == RoboInputEventPointerUp || type == RoboInputEventPointerCancel ||
           type == RoboInputEventTouchEnd || type == RoboInputEventTouchCancel;
}

int InputEventTypeAllowsSource(InputEventType type, InputSource source)
{
    if (!InputSourceIsKnown(source)) {
        return 0;
    }
    switch (type) {
    case RoboInputEventKeyDown:
    case RoboInputEventKeyUp:
    case RoboInputEventTextInput:
        return source == RoboInputSourceKeyboard || source == RoboInputSourceRemote ||
               source == RoboInputSourceVirtual;
    case RoboInputEventPointerMove:
    case RoboInputEventPointerDown:
    case RoboInputEventPointerUp:
    case RoboInputEventPointerCancel:
    case RoboInputEventWheel:
        return InputSourceIsPositioning(source) != 0;
    case RoboInputEventTouchBegin:
    case RoboInputEventTouchMove:
    case RoboInputEventTouchEnd:
    case RoboInputEventTouchCancel:
        /* A positioning source that is not a mouse: a backend reporting `MOUSE` for a touch contact has
         * mislabelled itself, and accepting that would hide the mistake from every layer above. */
        return source == RoboInputSourceTouch || source == RoboInputSourceTablet ||
               source == RoboInputSourcePointer || source == RoboInputSourceVirtual ||
               source == RoboInputSourceRemote;
    default:
        break;
    }
    return 0;
}

InputEventType InputEventTypeForWindowEvent(int windowEventType)
{
    switch ((WindowEventType)windowEventType) {
    case RoboWindowEventKeyDown:
        return RoboInputEventKeyDown;
    case RoboWindowEventKeyUp:
        return RoboInputEventKeyUp;
    case RoboWindowEventMouseMove:
        return RoboInputEventPointerMove;
    case RoboWindowEventMouseButtonDown:
        return RoboInputEventPointerDown;
    case RoboWindowEventMouseButtonUp:
        return RoboInputEventPointerUp;
    case RoboWindowEventMouseWheel:
        return RoboInputEventWheel;
    default:
        break;
    }
    return RoboInputEventNone;
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

void InputEventInit(InputEvent *event)
{
    if (!event) {
        return;
    }
    event->type = RoboInputEventNone;
    event->source = RoboInputSourceUnknown;
    InputDeviceInit(&event->device);
    event->timestampMs = 0u;
    event->modifiers = RoboInputModifiersNone;
    event->space = RoboInputSpaceUnknown;
    event->window = RoboWindowIdNone;
    event->display = RoboDisplayIdNone;
    InputTargetInit(&event->target);
    event->hasTarget = 0;
    InputKeyboardDataInit(&event->payload.keyboard);
    InputPointerDataInit(&event->payload.pointer);
}

Result InputEventSetKeyboard(InputEvent *event,
                                            InputEventType type,
                                            InputSource source,
                                            const InputKeyboardData *keyboard,
                                            InputDiag *diag)
{
    if (!event || !keyboard) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "a key event needs an event and a payload");
    }
    if (!InputEventTypeIsKey(type)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "only KeyDown and KeyUp carry a keyboard payload");
    }
    if (!InputKeyboardDataIsValid(keyboard) ||
        !InputKeyboardDataActionMatches(keyboard, (int)type)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "the keyboard payload is invalid or its action disagrees with the event");
    }
    if (!InputEventTypeAllowsSource(type, source)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "that source cannot report a key event");
    }
    event->type = type;
    event->source = source;
    event->payload.keyboard = *keyboard;
    return RoboOk;
}

Result InputEventSetPointer(InputEvent *event,
                                           InputEventType type,
                                           InputSource source,
                                           const InputPointerData *pointer,
                                           InputSpace space,
                                           InputDiag *diag)
{
    if (!event || !pointer) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "a positioned event needs an event and a payload");
    }
    if (InputEventPayloadKind(type) != RoboInputPayloadPointer) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "that type does not carry a pointer payload");
    }
    if (!InputPointerDataIsValid(pointer)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "the pointer payload is not a payload this build may carry");
    }
    if (!InputSpaceIsKnown(space)) {
        return RoboInputDiag(diag, RoboInputDomainSpace, RoboInvalidArgument,
                               "a positioned event must say which plane its coordinates are on");
    }
    if (!InputEventTypeAllowsSource(type, source)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "that source cannot report that kind of positioning event");
    }
    event->type = type;
    event->source = source;
    event->space = space;
    event->payload.pointer = *pointer;
    return RoboOk;
}

Result InputEventSetText(InputEvent *event,
                                        InputSource source,
                                        const char *text,
                                        InputDiag *diag)
{
    if (!event) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "a text event needs an event to fill");
    }
    if (!InputEventTypeAllowsSource(RoboInputEventTextInput, source)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "that source cannot report text");
    }
    if (!RiTextFits(text, RoboInputTextMax)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "the text is longer than one event may carry");
    }
    if (text == NULL || text[0] == '\0') {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "a TextInput event must carry at least one byte");
    }
    event->type = RoboInputEventTextInput;
    event->source = source;
    InputTextDataSet(&event->payload.text, text);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Common fields                                                       */
/* ------------------------------------------------------------------ */

Result InputEventSetDevice(InputEvent *event,
                                          const InputDevice *device,
                                          InputDiag *diag)
{
    if (!event || !device) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "a device needs an event and a record");
    }
    if (!InputDeviceIsValid(device)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "the device record is not one this build may carry");
    }
    if (device->source != RoboInputSourceUnknown && device->source != event->source) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "the device's source disagrees with the event's");
    }
    event->device = *device;
    return RoboOk;
}

Result InputEventSetTimestamp(InputEvent *event, uint64_t timestampMs)
{
    if (!event) {
        return RoboInvalidArgument;
    }
    event->timestampMs = timestampMs;
    return RoboOk;
}

Result InputEventSetModifiers(InputEvent *event,
                                             InputModifiers modifiers,
                                             InputDiag *diag)
{
    if (!event) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "a modifier state needs an event");
    }
    if (!InputModifiersAreKnown(modifiers)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "the modifier state carries bits this build does not define");
    }
    event->modifiers = modifiers;
    return RoboOk;
}

Result InputEventSetWindow(InputEvent *event, WindowId window)
{
    if (!event) {
        return RoboInvalidArgument;
    }
    event->window = window;
    return RoboOk;
}

Result InputEventSetDisplay(InputEvent *event, DisplayId display)
{
    if (!event) {
        return RoboInvalidArgument;
    }
    event->display = display;
    return RoboOk;
}

Result InputEventSetTarget(InputEvent *event,
                                          const InputTarget *target,
                                          InputDiag *diag)
{
    if (!event || !target) {
        return RoboInputDiag(diag, RoboInputDomainTarget, RoboInvalidArgument,
                               "a target needs an event and a record");
    }
    if (!InputTargetIsValid(target)) {
        return RoboInputDiag(diag, RoboInputDomainTarget, RoboInvalidArgument,
                               "the target record is not one this build may carry");
    }
    if (InputEventTypeHasPosition(event->type) && target->space != event->space) {
        return RoboInputDiag(diag, RoboInputDomainSpace, RoboInvalidArgument,
                               "the target was concluded in another plane than the event's");
    }
    if (event->window != RoboWindowIdNone && target->window != RoboWindowIdNone &&
        target->window != event->window) {
        return RoboInputDiag(diag, RoboInputDomainTarget, RoboInvalidArgument,
                               "the target names a window the event is not about");
    }
    event->target = *target;
    event->hasTarget = 1;
    return RoboOk;
}

void InputEventClearTarget(InputEvent *event)
{
    if (!event) {
        return;
    }
    InputTargetInit(&event->target);
    event->hasTarget = 0;
}

/* ------------------------------------------------------------------ */
/* Validation                                                          */
/* ------------------------------------------------------------------ */

/**
 * Non-zero when @p data is coherent with a positioned event of @p type.
 *
 * The rules are about *agreement between the event and its payload*, which is the class of mistake that
 * produces an event a consumer acts on wrongly rather than one it can detect: a move whose mask claims a
 * press, a release that still has its own button down, a cancel that does not say it is one, or a touch
 * contact with a mouse button held.
 */
static int PointerPayloadMatchesType(const InputPointerData *data,
                                        InputEventType type)
{
    int cancelled = (data->flags & RoboInputPointerFlagCancelled) != 0u;

    if (cancelled != (type == RoboInputEventPointerCancel ||
                      type == RoboInputEventTouchCancel)) {
        return 0;
    }
    if (InputEventTypeIsTouch(type)) {
        /* A touch contact is a finger: it has no buttons, and an event claiming one would be a stylus
         * mislabelled as a finger (or the reverse). */
        return data->button == RoboMouseButtonNone && data->buttons == 0u;
    }
    switch (type) {
    case RoboInputEventPointerMove:
        return data->button == RoboMouseButtonNone;
    case RoboInputEventPointerDown:
        return data->button != RoboMouseButtonNone &&
               InputButtonMaskHas(data->buttons, data->button);
    case RoboInputEventPointerUp:
        return data->button != RoboMouseButtonNone &&
               !InputButtonMaskHas(data->buttons, data->button);
    case RoboInputEventPointerCancel:
        return data->button == RoboMouseButtonNone ||
               !InputButtonMaskHas(data->buttons, data->button);
    case RoboInputEventWheel:
        return data->button == RoboMouseButtonNone;
    default:
        break;
    }
    return 0;
}

int InputEventIsValid(const InputEvent *event)
{
    InputPayloadKind kind;

    if (!event) {
        return 0;
    }
    /* 1. a type that could be delivered */
    if (!InputEventTypeIsDeliverable(event->type)) {
        return 0;
    }
    /* 2. a source that may report it */
    if (!InputEventTypeAllowsSource(event->type, event->source)) {
        return 0;
    }
    /* 3. a device record, agreeing with the source when it names one */
    if (!InputDeviceIsValid(&event->device)) {
        return 0;
    }
    if (event->device.source != RoboInputSourceUnknown && event->device.source != event->source) {
        return 0;
    }
    /* 4. defined modifier bits */
    if (!InputModifiersAreKnown(event->modifiers)) {
        return 0;
    }
    /* 5. the payload the type owns, present and coherent with the type */
    kind = InputEventPayloadKind(event->type);
    switch (kind) {
    case RoboInputPayloadKeyboard:
        if (!InputKeyboardDataIsValid(&event->payload.keyboard) ||
            !InputKeyboardDataActionMatches(&event->payload.keyboard, (int)event->type)) {
            return 0;
        }
        break;
    case RoboInputPayloadPointer:
        if (!InputPointerDataIsValid(&event->payload.pointer) ||
            !PointerPayloadMatchesType(&event->payload.pointer, event->type)) {
            return 0;
        }
        break;
    case RoboInputPayloadText:
        /* A text event that produced no text is not an event; the *payload* may be empty, and the rule that
         * says so lives here rather than there because it is a fact about the type, not about the field. */
        if (!InputTextDataIsValid(&event->payload.text) ||
            event->payload.text.text[0] == '\0') {
            return 0;
        }
        break;
    default:
        return 0;
    }
    /* 6. a plane exactly when there is a position to be on it */
    if (InputEventTypeHasPosition(event->type)) {
        if (!InputSpaceIsKnown(event->space)) {
            return 0;
        }
    } else if (event->space != RoboInputSpaceUnknown) {
        return 0;
    }
    /* 7. a target that agrees with the event about the plane and the window */
    if (event->hasTarget != 0 && event->hasTarget != 1) {
        return 0;
    }
    if (event->hasTarget) {
        if (!InputTargetIsValid(&event->target)) {
            return 0;
        }
        if (InputEventTypeHasPosition(event->type) && event->target.space != event->space) {
            return 0;
        }
        if (event->window != RoboWindowIdNone && event->target.window != RoboWindowIdNone &&
            event->target.window != event->window) {
            return 0;
        }
    }
    return 1;
}

Result InputEventCopy(InputEvent *destination, const InputEvent *source)
{
    if (!destination || !source) {
        return RoboInvalidArgument;
    }
    *destination = *source;
    return RoboOk;
}

int InputEventEqual(const InputEvent *a, const InputEvent *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    if (a->type != b->type || a->source != b->source || a->timestampMs != b->timestampMs ||
        a->modifiers != b->modifiers || a->space != b->space || a->window != b->window ||
        a->display != b->display || a->hasTarget != b->hasTarget) {
        return 0;
    }
    if (!InputDeviceEqual(&a->device, &b->device)) {
        return 0;
    }
    if (a->hasTarget && !InputTargetEqual(&a->target, &b->target)) {
        return 0;
    }
    switch (InputEventPayloadKind(a->type)) {
    case RoboInputPayloadKeyboard:
        return InputKeyboardDataEqual(&a->payload.keyboard, &b->payload.keyboard);
    case RoboInputPayloadPointer:
        return a->payload.pointer.pointer == b->payload.pointer.pointer &&
               a->payload.pointer.button == b->payload.pointer.button &&
               a->payload.pointer.buttons == b->payload.pointer.buttons &&
               GfxPointEqual(a->payload.pointer.position, b->payload.pointer.position) &&
               GfxPointEqual(a->payload.pointer.previous, b->payload.pointer.previous) &&
               a->payload.pointer.hasPrevious == b->payload.pointer.hasPrevious &&
               a->payload.pointer.deltaX == b->payload.pointer.deltaX &&
               a->payload.pointer.deltaY == b->payload.pointer.deltaY &&
               a->payload.pointer.clickCount == b->payload.pointer.clickCount &&
               a->payload.pointer.wheel.deltaX == b->payload.pointer.wheel.deltaX &&
               a->payload.pointer.wheel.deltaY == b->payload.pointer.wheel.deltaY &&
               a->payload.pointer.wheel.unit == b->payload.pointer.wheel.unit &&
               a->payload.pointer.flags == b->payload.pointer.flags;
    case RoboInputPayloadText:
        return InputTextDataEqual(&a->payload.text, &b->payload.text);
    default:
        break;
    }
    /* No payload: two events of an undefined type carry no record that could differ, so what was compared
     * above is the whole comparison. */
    return 1;
}

/* ------------------------------------------------------------------ */
/* Positions                                                           */
/* ------------------------------------------------------------------ */

Result InputEventPosition(const InputEvent *event,
                                        GfxPoint *out,
                                        InputDiag *diag)
{
    if (!event || !out) {
        return RoboInputDiag(diag, RoboInputDomainSpace, RoboInvalidArgument,
                               "a position needs an event and somewhere to put it");
    }
    if (!InputEventTypeHasPosition(event->type)) {
        return RoboInputDiag(diag, RoboInputDomainSpace, RoboNotSupported,
                               "that event type carries no position");
    }
    *out = event->payload.pointer.position;
    return RoboOk;
}

Result InputEventPositionIn(const InputEvent *event,
                                           InputSpace to,
                                           const InputRoute *route,
                                           GfxPoint *out,
                                           InputDiag *diag)
{
    GfxPoint point;
    Result r = InputEventPosition(event, &point, diag);

    if (r != RoboOk) {
        return r;
    }
    return InputConvertPoint(point, event->space, to, route, out, diag);
}
