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

void InputDeviceDataInit(InputDeviceData *data)
{
    if (!data) {
        return;
    }
    InputDeviceDescriptorInit(&data->descriptor);
}

int InputDeviceDataIsValid(const InputDeviceData *data)
{
    return data != NULL && InputDeviceDescriptorIsValid(&data->descriptor);
}

int InputDeviceDataEqual(const InputDeviceData *a, const InputDeviceData *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    return InputDeviceDescriptorEqual(&a->descriptor, &b->descriptor);
}

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
    case RoboInputEventDeviceConnected:
        return "DeviceConnected";
    case RoboInputEventDeviceDisconnected:
        return "DeviceDisconnected";
    case RoboInputEventKeyRepeat:
        return "KeyRepeat";
    case RoboInputEventPointerEnter:
        return "PointerEnter";
    case RoboInputEventPointerLeave:
        return "PointerLeave";
    case RoboInputEventPenDown:
        return "PenDown";
    case RoboInputEventPenMove:
        return "PenMove";
    case RoboInputEventPenUp:
        return "PenUp";
    case RoboInputEventPenCancel:
        return "PenCancel";
    case RoboInputEventGamepadButtonDown:
        return "GamepadButtonDown";
    case RoboInputEventGamepadButtonUp:
        return "GamepadButtonUp";
    case RoboInputEventGamepadAxisChanged:
        return "GamepadAxisChanged";
    case RoboInputEventCompositionStart:
        return "CompositionStart";
    case RoboInputEventCompositionUpdate:
        return "CompositionUpdate";
    case RoboInputEventCompositionCommit:
        return "CompositionCommit";
    case RoboInputEventCompositionCancel:
        return "CompositionCancel";
    case RoboInputEventFocusIn:
        return "FocusIn";
    case RoboInputEventFocusOut:
        return "FocusOut";
    case RoboInputEventWindowActivate:
        return "WindowActivate";
    case RoboInputEventWindowDeactivate:
        return "WindowDeactivate";
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
    case RoboInputEventKeyRepeat:
        return RoboInputPayloadKeyboard;
    case RoboInputEventPointerMove:
    case RoboInputEventPointerDown:
    case RoboInputEventPointerUp:
    case RoboInputEventPointerCancel:
    case RoboInputEventPointerEnter:
    case RoboInputEventPointerLeave:
    case RoboInputEventWheel:
    case RoboInputEventTouchBegin:
    case RoboInputEventTouchMove:
    case RoboInputEventTouchEnd:
    case RoboInputEventTouchCancel:
        return RoboInputPayloadPointer;
    case RoboInputEventTextInput:
        return RoboInputPayloadText;
    case RoboInputEventDeviceConnected:
    case RoboInputEventDeviceDisconnected:
        return RoboInputPayloadDevice;
    case RoboInputEventPenDown:
    case RoboInputEventPenMove:
    case RoboInputEventPenUp:
    case RoboInputEventPenCancel:
        return RoboInputPayloadPen;
    case RoboInputEventGamepadButtonDown:
    case RoboInputEventGamepadButtonUp:
    case RoboInputEventGamepadAxisChanged:
        return RoboInputPayloadGamepad;
    case RoboInputEventCompositionStart:
    case RoboInputEventCompositionUpdate:
    case RoboInputEventCompositionCommit:
    case RoboInputEventCompositionCancel:
        return RoboInputPayloadComposition;
    default:
        break;
    }
    /* Focus and window activation own no payload on purpose: what they are about is the event's own target
     * and window (see `InputEventSetFocusOrWindow`). */
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
    case RoboInputPayloadDevice:
        return "device";
    case RoboInputPayloadPen:
        return "pen";
    case RoboInputPayloadGamepad:
        return "gamepad";
    case RoboInputPayloadComposition:
        return "composition";
    default:
        break;
    }
    return "unknown";
}

int InputEventTypeIsKey(InputEventType type)
{
    /* An auto-repeat is a key event: it is a *down* of a key that is already held, and every consumer that
     * asks this question wants `KEY_REPEAT` in the answer -- the ones that want to tell the two apart ask
     * `type == KEY_REPEAT`, which is a narrower question with its own answer. */
    return type == RoboInputEventKeyDown || type == RoboInputEventKeyUp ||
           type == RoboInputEventKeyRepeat;
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

int InputEventTypeIsPen(InputEventType type)
{
    return type == RoboInputEventPenDown || type == RoboInputEventPenMove ||
           type == RoboInputEventPenUp || type == RoboInputEventPenCancel;
}

int InputEventTypeIsGamepad(InputEventType type)
{
    return type == RoboInputEventGamepadButtonDown || type == RoboInputEventGamepadButtonUp ||
           type == RoboInputEventGamepadAxisChanged;
}

int InputEventTypeIsComposition(InputEventType type)
{
    return type == RoboInputEventCompositionStart || type == RoboInputEventCompositionUpdate ||
           type == RoboInputEventCompositionCommit || type == RoboInputEventCompositionCancel;
}

int InputEventTypeIsDevice(InputEventType type)
{
    return type == RoboInputEventDeviceConnected || type == RoboInputEventDeviceDisconnected;
}

int InputEventTypeIsFocus(InputEventType type)
{
    return type == RoboInputEventFocusIn || type == RoboInputEventFocusOut;
}

int InputEventTypeIsWindow(InputEventType type)
{
    return type == RoboInputEventWindowActivate || type == RoboInputEventWindowDeactivate;
}

int InputEventTypeHasPosition(InputEventType type)
{
    /* A pen is a pointer: its payload nests the pointer sample, and every positioned-event question -- which
     * plane, convert to what, what was under it -- has one answer for both kinds. */
    InputPayloadKind kind = InputEventPayloadKind(type);

    return kind == RoboInputPayloadPointer || kind == RoboInputPayloadPen;
}

int InputEventTypeIsRelease(InputEventType type)
{
    return type == RoboInputEventPointerUp || type == RoboInputEventPointerCancel ||
           type == RoboInputEventTouchEnd || type == RoboInputEventTouchCancel ||
           type == RoboInputEventPenUp || type == RoboInputEventPenCancel;
}

int InputEventTypeIsTerminal(InputEventType type)
{
    return InputEventTypeIsRelease(type);
}

int InputEventTypeAllowsSource(InputEventType type, InputSource source)
{
    if (!InputSourceIsKnown(source)) {
        return 0;
    }
    switch (type) {
    case RoboInputEventKeyDown:
    case RoboInputEventKeyUp:
    case RoboInputEventKeyRepeat:
    case RoboInputEventTextInput:
    /* A composition is what an input method does *with* keystrokes, so it comes from where keystrokes
     * come from. */
    case RoboInputEventCompositionStart:
    case RoboInputEventCompositionUpdate:
    case RoboInputEventCompositionCommit:
    case RoboInputEventCompositionCancel:
        return source == RoboInputSourceKeyboard || source == RoboInputSourceRemote ||
               source == RoboInputSourceVirtual;
    case RoboInputEventPointerMove:
    case RoboInputEventPointerDown:
    case RoboInputEventPointerUp:
    case RoboInputEventPointerCancel:
    case RoboInputEventPointerEnter:
    case RoboInputEventPointerLeave:
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
    case RoboInputEventPenDown:
    case RoboInputEventPenMove:
    case RoboInputEventPenUp:
    case RoboInputEventPenCancel:
        /* A tablet, and not a touch digitizer: a finger cannot report pressure or tilt, so a touch source
         * claiming one is a mislabelled stylus the same way a touch from a mouse is a mislabelled finger. */
        return source == RoboInputSourceTablet || source == RoboInputSourceVirtual ||
               source == RoboInputSourceRemote;
    case RoboInputEventGamepadButtonDown:
    case RoboInputEventGamepadButtonUp:
    case RoboInputEventGamepadAxisChanged:
        return source == RoboInputSourceGameController || source == RoboInputSourceRemote ||
               source == RoboInputSourceVirtual;
    default:
        break;
    }
    /* Hotplug, focus and window activation may come from any defined source: they describe the system
     * rather than a device category, and the source on them names which category the *subject* belongs to
     * -- the keyboard that arrived, the pointer that moved focus. @p source was checked above. */
    if (InputEventTypeIsDevice(type) || InputEventTypeIsFocus(type) ||
        InputEventTypeIsWindow(type)) {
        return 1;
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
    /* Every member of the union is cleared, not just the two 12.1 had: a dump or a comparison that reached
     * for a member a previous event left behind would otherwise read a stale record -- which is the reason
     * the payload is a union rather than three fields in the first place. */
    InputKeyboardDataInit(&event->payload.keyboard);
    InputPointerDataInit(&event->payload.pointer);
    InputTextDataInit(&event->payload.text);
    InputDeviceDataInit(&event->payload.device);
    InputPenDataInit(&event->payload.pen);
    InputGamepadDataInit(&event->payload.gamepad);
    InputCompositionDataInit(&event->payload.composition);
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
                               "only the three key types carry a keyboard payload");
    }
    if (!InputKeyboardDataIsValid(keyboard) ||
        !InputKeyboardDataActionMatches(keyboard, (int)type) ||
        !InputKeyboardDataRepeatMatchesEvent(keyboard, (int)type)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "the keyboard payload is invalid, or its action or repeat flag disagrees "
                               "with the event");
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

Result InputEventSetKeyRepeat(InputEvent *event,
                              InputSource source,
                              const InputKeyboardData *keyboard,
                              InputDiag *diag)
{
    if (!event || !keyboard) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "a repeat needs an event and a payload");
    }
    if (!keyboard->repeat) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "a KEY_REPEAT event needs a payload that says it is one");
    }
    return InputEventSetKeyboard(event, RoboInputEventKeyRepeat, source, keyboard, diag);
}

/* Defined in the validation section below; the pen door needs it above, because "a stylus is a pointer"
 * means the pen setters apply the *same* rule a pointer setter does rather than a second one. */
static int PointerPayloadMatchesType(const InputPointerData *data, InputEventType type);

Result InputEventSetPen(InputEvent *event,
                        InputEventType type,
                        InputSource source,
                        const InputPenData *pen,
                        InputSpace space,
                        InputDiag *diag)
{
    if (!event || !pen) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "a pen event needs an event and a payload");
    }
    if (!InputEventTypeIsPen(type)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "that type does not carry a pen payload");
    }
    if (!InputPenDataIsValid(pen)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "the pen payload is not a record this build may carry");
    }
    /* A stylus is a pointer, so its pointer half must suit the type exactly as a pointer event's would:
     * the rule is *not* relaxed because the hardware has more to say. */
    if (!PointerPayloadMatchesType(&pen->pointer, type)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "the pen's pointer half disagrees with the event type");
    }
    if (!InputSpaceIsKnown(space)) {
        return RoboInputDiag(diag, RoboInputDomainSpace, RoboInvalidArgument,
                               "a positioned event must say which plane its coordinates are on");
    }
    if (!InputEventTypeAllowsSource(type, source)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "that source cannot report a pen event");
    }
    event->type = type;
    event->source = source;
    event->space = space;
    event->payload.pen = *pen;
    return RoboOk;
}

Result InputEventSetGamepad(InputEvent *event,
                            InputEventType type,
                            InputSource source,
                            const InputGamepadData *data,
                            InputDiag *diag)
{
    if (!event || !data) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "a gamepad event needs an event and a payload");
    }
    if (!InputEventTypeIsGamepad(type)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "that type does not carry a gamepad payload");
    }
    if (!InputGamepadDataIsValid(data)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "the gamepad payload is not a record this build may carry");
    }
    if (type == RoboInputEventGamepadAxisChanged) {
        if (!InputGamepadDataIsAxis(data)) {
            return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                                   "an axis event needs a payload that carries an axis");
        }
    } else if (!InputGamepadDataIsButton(data)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "a button event needs a payload that carries a button");
    }
    if (!InputEventTypeAllowsSource(type, source)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "that source cannot report a gamepad event");
    }
    event->type = type;
    event->source = source;
    event->payload.gamepad = *data;
    return RoboOk;
}

Result InputEventSetComposition(InputEvent *event,
                                InputEventType type,
                                InputSource source,
                                const InputCompositionData *preedit,
                                InputDiag *diag)
{
    if (!event || !preedit) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "a composition event needs an event and a record");
    }
    if (!InputEventTypeIsComposition(type)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "that type does not carry a composition payload");
    }
    if (!InputCompositionDataIsValid(preedit)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "the composition record is not one this build may carry");
    }
    if (!InputEventTypeAllowsSource(type, source)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "that source cannot report a composition");
    }
    event->type = type;
    event->source = source;
    event->payload.composition = *preedit;
    return RoboOk;
}

Result InputEventSetDeviceHotplug(InputEvent *event,
                                  InputEventType type,
                                  InputSource source,
                                  const InputDeviceDescriptor *descriptor,
                                  InputDiag *diag)
{
    if (!event || !descriptor) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "a hotplug event needs an event and a descriptor");
    }
    if (!InputEventTypeIsDevice(type)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "that type is not a hotplug event");
    }
    if (!InputDeviceDescriptorIsValid(descriptor)) {
        return RoboInputDiag(diag, RoboInputDomainDevice, RoboInvalidArgument,
                               "the descriptor is not one this build may carry");
    }
    if (!InputEventTypeAllowsSource(type, source)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "that source cannot report a hotplug event");
    }
    event->type = type;
    event->source = source;
    event->payload.device.descriptor = *descriptor;
    /* Filled from the *same* record, which is what makes "the event and the descriptor it carries cannot
     * name two different devices" a fact about the door rather than a rule a caller has to keep. */
    event->device = descriptor->device;
    return RoboOk;
}

Result InputEventSetFocusOrWindow(InputEvent *event,
                                  InputEventType type,
                                  InputSource source,
                                  InputDiag *diag)
{
    if (!event) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "no event to fill");
    }
    if (!InputEventTypeIsFocus(type) && !InputEventTypeIsWindow(type)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "that type is not a focus or window event");
    }
    if (!InputEventTypeAllowsSource(type, source)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "that is not a defined source");
    }
    event->type = type;
    event->source = source;
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
                      type == RoboInputEventTouchCancel ||
                      type == RoboInputEventPenCancel)) {
        return 0;
    }
    if (InputEventTypeIsTouch(type)) {
        /* A touch contact is a finger: it has no buttons, and an event claiming one would be a stylus
         * mislabelled as a finger (or the reverse). */
        return data->button == RoboMouseButtonNone && data->buttons == 0u;
    }
    switch (type) {
    /* Enter and leave are moves in every way that matters here: the pointer is somewhere and no button
     * changed state. What they add is *when* the runtime began or stopped tracking it, which is a routing
     * fact and not a field. */
    case RoboInputEventPointerEnter:
    case RoboInputEventPointerLeave:
    case RoboInputEventPenMove:
    case RoboInputEventPointerMove:
        return data->button == RoboMouseButtonNone;
    case RoboInputEventPointerDown:
    case RoboInputEventPenDown:
        return data->button != RoboMouseButtonNone &&
               InputButtonMaskHas(data->buttons, data->button);
    case RoboInputEventPointerUp:
    case RoboInputEventPenUp:
        return data->button != RoboMouseButtonNone &&
               !InputButtonMaskHas(data->buttons, data->button);
    case RoboInputEventPointerCancel:
    case RoboInputEventPenCancel:
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
            !InputKeyboardDataActionMatches(&event->payload.keyboard, (int)event->type) ||
            !InputKeyboardDataRepeatMatchesEvent(&event->payload.keyboard, (int)event->type)) {
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
    case RoboInputPayloadDevice:
        if (!InputDeviceDataIsValid(&event->payload.device)) {
            return 0;
        }
        /* The identity on the event *is* the descriptor's: two answers to "which device" in one record is
         * the class of mistake this model refuses everywhere, and the door that fills both would hide it. */
        if (!InputDeviceEqual(&event->payload.device.descriptor.device, &event->device)) {
            return 0;
        }
        break;
    case RoboInputPayloadPen:
        if (!InputPenDataIsValid(&event->payload.pen) ||
            !PointerPayloadMatchesType(&event->payload.pen.pointer, event->type)) {
            return 0;
        }
        break;
    case RoboInputPayloadGamepad:
        if (!InputGamepadDataIsValid(&event->payload.gamepad)) {
            return 0;
        }
        if (event->type == RoboInputEventGamepadAxisChanged) {
            if (!InputGamepadDataIsAxis(&event->payload.gamepad)) {
                return 0;
            }
        } else if (!InputGamepadDataIsButton(&event->payload.gamepad)) {
            return 0;
        }
        break;
    case RoboInputPayloadComposition:
        /* Every composition type accepts any valid record, including an empty preedit -- a
         * `COMPOSITION_START` that begins with nothing and fills in as the user types is ordinary, and a
         * `COMMIT` may carry the resolved text or leave it to the editor that already has it. */
        if (!InputCompositionDataIsValid(&event->payload.composition)) {
            return 0;
        }
        break;
    case RoboInputPayloadNone:
        /* Focus and window activation own no payload on purpose (see `InputEventSetFocusOrWindow`); no
         * other deliverable type reaches here, because a type's kind is a total function of its type. */
        if (!InputEventTypeIsFocus(event->type) && !InputEventTypeIsWindow(event->type)) {
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
    case RoboInputPayloadDevice:
        return InputDeviceDataEqual(&a->payload.device, &b->payload.device);
    case RoboInputPayloadPen:
        return InputPenDataEqual(&a->payload.pen, &b->payload.pen);
    case RoboInputPayloadGamepad:
        return InputGamepadDataEqual(&a->payload.gamepad, &b->payload.gamepad);
    case RoboInputPayloadComposition:
        return InputCompositionDataEqual(&a->payload.composition, &b->payload.composition);
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
    /* One answer for both positioned kinds: a pen's position is its pointer half's, because a stylus is a
     * pointer (see `RoboInputPen.h`). */
    if (InputEventPayloadKind(event->type) == RoboInputPayloadPen) {
        *out = event->payload.pen.pointer.position;
    } else {
        *out = event->payload.pointer.position;
    }
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
