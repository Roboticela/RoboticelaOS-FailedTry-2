/*
 * RoboInput keyboard payload (Phase 12.1) — see RoboInputKeyboard.h.
 *
 * The payload is a record of what a key *did*, using RoboWindow's own two key vocabularies and adding only
 * what those vocabularies cannot carry: the action, the repeat flag, and the text. Two facts are derived
 * rather than demanded of the caller -- `PHYSICAL_UNKNOWN` from the key identity, and which keys are
 * modifiers -- because a caller that had to remember them would be a caller that could forget them, and the
 * dump would then print a key as mapped when it is not.
 */

#include "RoboInput/RoboInputKeyboard.h"

#include "RoboInput/RoboInputEvent.h"
#include "RoboInputInternal.h"

#include "RoboWindow/RoboWindowKey.h"

#include <stddef.h>
#include <stdint.h>

void InputKeyboardDataInit(InputKeyboardData *data)
{
    if (!data) {
        return;
    }
    data->physical = RoboPhysicalKeyUnknown;
    data->logical = RoboLogicalKeyUnknown;
    data->action = RoboKeyActionNone;
    data->repeat = 0;
    data->scanCode = 0u;
    data->flags = 0u;
    data->text[0] = '\0';
}

/** Non-zero for the physical keys on the numeric keypad, which a consumer may want to distinguish. */
static int PhysicalKeyIsNumpad(PhysicalKey physical)
{
    return physical >= RoboPhysicalKeyNumpadDivide && physical <= RoboPhysicalKeyNumpadDecimal;
}

int InputPhysicalKeyIsModifier(PhysicalKey physical)
{
    return InputPhysicalKeyModifier(physical) != RoboKeyModNone;
}

KeyModifierFlags InputPhysicalKeyModifier(PhysicalKey physical)
{
    switch (physical) {
    case RoboPhysicalKeyShiftLeft:
        return RoboKeyModShift | RoboKeyModShiftLeft;
    case RoboPhysicalKeyShiftRight:
        return RoboKeyModShift | RoboKeyModShiftRight;
    case RoboPhysicalKeyControlLeft:
        return RoboKeyModCtrl | RoboKeyModCtrlLeft;
    case RoboPhysicalKeyControlRight:
        return RoboKeyModCtrl | RoboKeyModCtrlRight;
    case RoboPhysicalKeyAltLeft:
        return RoboKeyModAlt | RoboKeyModAltLeft;
    case RoboPhysicalKeyAltRight:
        return RoboKeyModAlt | RoboKeyModAltRight;
    case RoboPhysicalKeyMetaLeft:
        return RoboKeyModMeta | RoboKeyModMetaLeft;
    case RoboPhysicalKeyMetaRight:
        return RoboKeyModMeta | RoboKeyModMetaRight;
    case RoboPhysicalKeyCapsLock:
        return RoboKeyModCapsLock;
    case RoboPhysicalKeyNumLock:
        return RoboKeyModNumLock;
    case RoboPhysicalKeyScrollLock:
        return RoboKeyModScrollLock;
    default:
        break;
    }
    return RoboKeyModNone;
}

Result InputKeyboardDataSet(InputKeyboardData *data,
                                           PhysicalKey physical,
                                           LogicalKey logical,
                                           KeyAction action,
                                           int repeat,
                                           const char *text)
{
    if (!data) {
        return RoboInvalidArgument;
    }
    /* Cleared *first*, so that every refusal below leaves an empty record rather than the caller's previous
     * one: a payload that half-describes the refused call and half-describes the last accepted one is the
     * one outcome nobody can reason about. The same rule `InputDeviceSet` and the target doors use. */
    InputKeyboardDataInit(data);
    if (action != RoboKeyActionDown && action != RoboKeyActionUp) {
        return RoboInvalidArgument;
    }
    if (physical != RoboPhysicalKeyUnknown && !PhysicalKeyIsKnown(physical)) {
        return RoboInvalidArgument;
    }
    if (logical != RoboLogicalKeyUnknown && !LogicalKeyIsKnown(logical)) {
        return RoboInvalidArgument;
    }
    if (!RiTextFits(text, RoboInputTextMax)) {
        LogDebug("robo_input: key text rejected, longer than %u bytes",
                       (unsigned)RoboInputTextMax);
        return RoboInvalidArgument;
    }

    data->physical = physical;
    data->logical = logical;
    data->action = action;
    /* A release is not a repeat, whatever the caller says: normalizing here means the rule lives in one
     * place rather than in every consumer. */
    data->repeat = (action == RoboKeyActionUp) ? 0 : (repeat ? 1 : 0);
    if (physical == RoboPhysicalKeyUnknown) {
        data->flags |= RoboInputKeyFlagPhysicalUnknown;
    }
    if (InputPhysicalKeyIsModifier(physical)) {
        data->flags |= RoboInputKeyFlagModifier;
    }
    if (PhysicalKeyIsNumpad(physical)) {
        data->flags |= RoboInputKeyFlagNumpad;
    }
    RiTextCopy(data->text, RoboInputTextMax, text);
    return RoboOk;
}

int InputKeyboardDataIsValid(const InputKeyboardData *data)
{
    if (!data) {
        return 0;
    }
    if (data->action != RoboKeyActionDown && data->action != RoboKeyActionUp) {
        return 0;
    }
    if (data->physical != RoboPhysicalKeyUnknown && !PhysicalKeyIsKnown(data->physical)) {
        return 0;
    }
    if (data->logical != RoboLogicalKeyUnknown && !LogicalKeyIsKnown(data->logical)) {
        return 0;
    }
    if ((data->flags & ~RoboInputKeyFlagAll) != 0u) {
        return 0;
    }
    /* The flag and the identity have to agree in both directions: a key with no physical identity that does
     * not say so is a record a consumer would read as "this key is A". */
    if (((data->flags & RoboInputKeyFlagPhysicalUnknown) != 0u) !=
        (data->physical == RoboPhysicalKeyUnknown)) {
        return 0;
    }
    if (data->repeat != 0 && data->repeat != 1) {
        return 0;
    }
    if (data->action == RoboKeyActionUp && data->text[0] != '\0') {
        return 0;
    }
    return RiBoundedStrlen(data->text, RoboInputTextMax) < RoboInputTextMax;
}

KeyAction InputKeyboardActionForEvent(int eventType)
{
    switch ((InputEventType)eventType) {
    case RoboInputEventKeyDown:
        return RoboKeyActionDown;
    case RoboInputEventKeyUp:
        return RoboKeyActionUp;
    default:
        break;
    }
    return RoboKeyActionNone;
}

int InputKeyboardDataActionMatches(const InputKeyboardData *data, int eventType)
{
    KeyAction action;

    if (!data) {
        return 0;
    }
    action = InputKeyboardActionForEvent(eventType);
    return action != RoboKeyActionNone && data->action == action;
}

int InputKeyboardDataEqual(const InputKeyboardData *a,
                                   const InputKeyboardData *b)
{
    size_t index;

    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    if (a->physical != b->physical || a->logical != b->logical || a->action != b->action ||
        a->repeat != b->repeat || a->scanCode != b->scanCode || a->flags != b->flags) {
        return 0;
    }
    for (index = 0u; index < RoboInputTextMax; index++) {
        if (a->text[index] != b->text[index]) {
            return 0;
        }
    }
    return 1;
}

size_t InputKeyboardDataText(const InputKeyboardData *data,
                                     char *out,
                                     size_t capacity)
{
    size_t length;

    if (!out || capacity == 0u) {
        return 0u;
    }
    out[0] = '\0';
    if (!data) {
        return 0u;
    }
    length = RiBoundedStrlen(data->text, RoboInputTextMax);
    RiTextCopy(out, capacity, data->text);
    if (length >= capacity) {
        return capacity - 1u;
    }
    return length;
}
