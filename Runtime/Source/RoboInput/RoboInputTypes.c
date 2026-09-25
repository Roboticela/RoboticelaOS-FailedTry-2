/*
 * RoboInput fundamental types (Phase 12.1) — see RoboInputTypes.h.
 *
 * Everything in this file is a *query*, a *copy*, or a name. That is the whole content of the module's
 * bottom layer: a source is a number with a name, a device is a copied record with two rules about what may
 * be in it, and the modifiers are RoboWindow's own bit type with the questions a handler asks. Nothing here
 * allocates, caches, or remembers anything between calls, which is what lets every door be called from any
 * thread on any record.
 */

#include "RoboInput/RoboInputTypes.h"

#include "RoboInputInternal.h"

#include "RoboWindow/RoboWindowEvent.h"

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Sources                                                             */
/* ------------------------------------------------------------------ */

int InputSourceIsKnown(InputSource source)
{
    return source > RoboInputSourceUnknown && source < (InputSource)RoboInputSourceCount;
}

const char *InputSourceName(InputSource source)
{
    switch (source) {
    case RoboInputSourceUnknown:
        return "unknown";
    case RoboInputSourceKeyboard:
        return "keyboard";
    case RoboInputSourceMouse:
        return "mouse";
    case RoboInputSourcePointer:
        return "pointer";
    case RoboInputSourceTouch:
        return "touch";
    case RoboInputSourceTablet:
        return "tablet";
    case RoboInputSourceGameController:
        return "game-controller";
    case RoboInputSourceRemote:
        return "remote";
    case RoboInputSourceVirtual:
        return "virtual";
    default:
        break;
    }
    return "unknown";
}

int InputSourceIsPositioning(InputSource source)
{
    switch (source) {
    case RoboInputSourceMouse:
    case RoboInputSourcePointer:
    case RoboInputSourceTouch:
    case RoboInputSourceTablet:
    case RoboInputSourceGameController:
    case RoboInputSourceRemote:
    case RoboInputSourceVirtual:
        return 1;
    default:
        break;
    }
    return 0;
}

InputSource InputSourceForWindowEvent(int windowEventType)
{
    switch ((WindowEventType)windowEventType) {
    case RoboWindowEventKeyDown:
    case RoboWindowEventKeyUp:
        return RoboInputSourceKeyboard;
    case RoboWindowEventMouseMove:
    case RoboWindowEventMouseButtonDown:
    case RoboWindowEventMouseButtonUp:
    case RoboWindowEventMouseWheel:
        return RoboInputSourceMouse;
    default:
        break;
    }
    return RoboInputSourceUnknown;
}

/* ------------------------------------------------------------------ */
/* Devices                                                             */
/* ------------------------------------------------------------------ */

void InputDeviceInit(InputDevice *device)
{
    size_t index;

    if (!device) {
        return;
    }
    device->id = RoboInputDeviceIdNone;
    device->source = RoboInputSourceUnknown;
    device->flags = 0u;
    /* The **whole** array, not just its first byte. `InputDeviceEqual` compares every byte of `name` --
     * which is what makes it a value comparison rather than a string one -- so a name whose tail was left
     * as whatever the stack held would make two devices the caller built identically compare unequal. The
     * suite that found this is `input.device.descriptor`, and the reading it broke is the one a recording
     * and a replay depend on: two identical devices must produce identical bytes. */
    for (index = 0u; index < RoboInputDeviceNameMax; index++) {
        device->name[index] = '\0';
    }
}

int InputDeviceIsValid(const InputDevice *device)
{
    if (!device) {
        return 0;
    }
    if (device->source != RoboInputSourceUnknown && !InputSourceIsKnown(device->source)) {
        return 0;
    }
    if ((device->flags & ~RoboInputDeviceFlagAll) != 0u) {
        return 0;
    }
    /* A name must be terminated inside its capacity: a record whose name filled the whole array with no NUL
     * is a record a dump would read past the end of. */
    return RiBoundedStrlen(device->name, RoboInputDeviceNameMax) < RoboInputDeviceNameMax;
}

Result InputDeviceSet(InputDevice *device,
                                    InputDeviceId id,
                                    InputSource source,
                                    const char *name)
{
    if (!device) {
        return RoboInvalidArgument;
    }
    if (source != RoboInputSourceUnknown && !InputSourceIsKnown(source)) {
        return RoboInvalidArgument;
    }
    if (!RiTextFits(name, RoboInputDeviceNameMax)) {
        /* Cleared, not half-filled: a truncated name is a *different* device's name. */
        InputDeviceInit(device);
        LogDebug("robo_input: device name rejected, too long for %u bytes",
                       (unsigned)RoboInputDeviceNameMax);
        return RoboInvalidArgument;
    }

    InputDeviceInit(device);
    device->id = id;
    device->source = source;
    RiTextCopy(device->name, RoboInputDeviceNameMax, name);
    return RoboOk;
}

size_t InputDeviceName(const InputDevice *device, char *out, size_t capacity)
{
    size_t length;

    if (!out || capacity == 0u) {
        return 0u;
    }
    out[0] = '\0';
    if (!device) {
        return 0u;
    }
    length = RiBoundedStrlen(device->name, RoboInputDeviceNameMax);
    RiTextCopy(out, capacity, device->name);
    if (length >= capacity) {
        return capacity - 1u;
    }
    return length;
}

int InputDeviceEqual(const InputDevice *a, const InputDevice *b)
{
    size_t index;

    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    if (a->id != b->id || a->source != b->source || a->flags != b->flags) {
        return 0;
    }
    for (index = 0u; index < RoboInputDeviceNameMax; index++) {
        if (a->name[index] != b->name[index]) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Modifiers                                                           */
/* ------------------------------------------------------------------ */

int InputModifiersAreKnown(InputModifiers modifiers)
{
    return (modifiers & ~((InputModifiers)RoboInputModifierAll)) == 0u;
}

int InputModifiersHas(InputModifiers modifiers, InputModifiers mask)
{
    return (modifiers & mask) == mask;
}

int InputModifiersIsAggregate(InputModifiers modifiers)
{
    return (modifiers & (RoboKeyModShiftLeft | RoboKeyModShiftRight |
                         RoboKeyModCtrlLeft | RoboKeyModCtrlRight |
                         RoboKeyModAltLeft | RoboKeyModAltRight |
                         RoboKeyModMetaLeft | RoboKeyModMetaRight)) == 0u;
}

const char *InputModifierName(InputModifiers modifier)
{
    switch (modifier) {
    case RoboKeyModNone:
        return "none";
    case RoboKeyModShift:
        return "shift";
    case RoboKeyModCtrl:
        return "control";
    case RoboKeyModAlt:
        return "alt";
    case RoboKeyModMeta:
        return "meta";
    case RoboKeyModCapsLock:
        return "caps-lock";
    case RoboKeyModNumLock:
        return "num-lock";
    case RoboKeyModScrollLock:
        return "scroll-lock";
    case RoboKeyModShiftLeft:
        return "shift-left";
    case RoboKeyModShiftRight:
        return "shift-right";
    case RoboKeyModCtrlLeft:
        return "control-left";
    case RoboKeyModCtrlRight:
        return "control-right";
    case RoboKeyModAltLeft:
        return "alt-left";
    case RoboKeyModAltRight:
        return "alt-right";
    case RoboKeyModMetaLeft:
        return "meta-left";
    case RoboKeyModMetaRight:
        return "meta-right";
    default:
        break;
    }
    return "?";
}

/** The bits, in the order a modifier list is written: the seven categories, then the side-specific ones. */
static InputModifiers ModifierOrder(size_t index)
{
    static const InputModifiers order[] = {
        RoboKeyModShift,       RoboKeyModCtrl,        RoboKeyModAlt,
        RoboKeyModMeta,        RoboKeyModCapsLock,   RoboKeyModNumLock,
        RoboKeyModScrollLock, RoboKeyModShiftLeft,  RoboKeyModShiftRight,
        RoboKeyModCtrlLeft,   RoboKeyModCtrlRight,  RoboKeyModAltLeft,
        RoboKeyModAltRight,   RoboKeyModMetaLeft,   RoboKeyModMetaRight
    };

    if (index >= (sizeof(order) / sizeof(order[0]))) {
        return RoboKeyModNone;
    }
    return order[index];
}

Result InputModifiersWrite(InputModifiers modifiers,
                                         char *buffer,
                                         size_t capacity,
                                         size_t *outLength)
{
    InputWriter text;
    size_t index;
    int written = 0;
    InputModifiers state = modifiers & (InputModifiers)RoboInputModifierAll;

    if (!buffer || capacity == 0u || !outLength) {
        return RoboInvalidArgument;
    }

    RiWriterInit(&text, buffer, capacity);
    if (state == RoboKeyModNone) {
        RiWriterText(&text, "none");
    } else {
        for (index = 0u;; index++) {
            InputModifiers bit = ModifierOrder(index);

            if (bit == RoboKeyModNone) {
                break;
            }
            if ((state & bit) == 0u) {
                continue;
            }
            if (written) {
                RiWriterChar(&text, '+');
            }
            RiWriterText(&text, InputModifierName(bit));
            written = 1;
        }
    }
    return RiWriterFinish(&text, outLength, NULL);
}
