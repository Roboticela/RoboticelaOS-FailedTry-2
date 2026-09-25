/*
 * RoboInput gamepad vocabulary and payload (Phase 13.53) — see `RoboInputGamepad.h`.
 *
 * The vocabulary is a table of names with an `IsKnown` beside each -- the shape every numbered vocabulary in
 * this module has, and asserted positionally by its suite -- and the payload is two factories that cannot
 * produce a record the validator would refuse, plus the validator itself.
 *
 * The one rule worth reading here is the one the two halves exist for: a record carries a **button or an
 * axis, never both**, and `InputGamepadDataIsValid` refuses the in-between cases. A record with neither is a
 * gamepad event that reports nothing (a cleared one); a record with both is an event that would have to be
 * read twice to know what happened, and whichever half a consumer happened to read first, the other would be
 * silently dropped.
 */

#include "RoboInput/RoboInputGamepad.h"

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

int InputGamepadButtonIsKnown(InputGamepadButton button)
{
    return (int)button >= (int)RoboGamepadButtonNone && (int)button < (int)RoboGamepadButtonCount;
}

const char *InputGamepadButtonName(InputGamepadButton button)
{
    switch (button) {
    case RoboGamepadButtonNone: return "none";
    case RoboGamepadButtonA: return "a";
    case RoboGamepadButtonB: return "b";
    case RoboGamepadButtonX: return "x";
    case RoboGamepadButtonY: return "y";
    case RoboGamepadButtonLeftBumper: return "left-bumper";
    case RoboGamepadButtonRightBumper: return "right-bumper";
    case RoboGamepadButtonBack: return "back";
    case RoboGamepadButtonStart: return "start";
    case RoboGamepadButtonGuide: return "guide";
    case RoboGamepadButtonLeftStick: return "left-stick";
    case RoboGamepadButtonRightStick: return "right-stick";
    case RoboGamepadButtonDpadUp: return "dpad-up";
    case RoboGamepadButtonDpadDown: return "dpad-down";
    case RoboGamepadButtonDpadLeft: return "dpad-left";
    case RoboGamepadButtonDpadRight: return "dpad-right";
    default: break;
    }
    return "unknown";
}

int InputGamepadAxisIsKnown(InputGamepadAxis axis)
{
    return (int)axis >= (int)RoboGamepadAxisNone && (int)axis < (int)RoboGamepadAxisCount;
}

const char *InputGamepadAxisName(InputGamepadAxis axis)
{
    switch (axis) {
    case RoboGamepadAxisNone: return "none";
    case RoboGamepadAxisLeftX: return "left-x";
    case RoboGamepadAxisLeftY: return "left-y";
    case RoboGamepadAxisRightX: return "right-x";
    case RoboGamepadAxisRightY: return "right-y";
    case RoboGamepadAxisLeftTrigger: return "left-trigger";
    case RoboGamepadAxisRightTrigger: return "right-trigger";
    case RoboGamepadAxisDpadX: return "dpad-x";
    case RoboGamepadAxisDpadY: return "dpad-y";
    default: break;
    }
    return "unknown";
}

/* ------------------------------------------------------------------ */
/* The payload                                                         */
/* ------------------------------------------------------------------ */

void InputGamepadDataInit(InputGamepadData *data)
{
    if (!data) {
        return;
    }
    data->button = RoboGamepadButtonNone;
    data->axis = RoboGamepadAxisNone;
    data->value = 0;
    data->flags = 0u;
}

Result InputGamepadDataSetButton(InputGamepadData *data,
                                 InputGamepadButton button,
                                 int pressed,
                                 InputDiag *diag)
{
    if (!data) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "nowhere to put the button sample");
    }
    InputGamepadDataInit(data);
    if (!InputGamepadButtonIsKnown(button) || button == RoboGamepadButtonNone) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "a button transition needs a button this build defines");
    }
    data->button = button;
    data->value = pressed ? 1 : 0;
    return RoboOk;
}

Result InputGamepadDataSetAxis(InputGamepadData *data,
                               InputGamepadAxis axis,
                               int32_t value,
                               InputDiag *diag)
{
    if (!data) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "nowhere to put the axis sample");
    }
    InputGamepadDataInit(data);
    if (!InputGamepadAxisIsKnown(axis) || axis == RoboGamepadAxisNone) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "an axis sample needs an axis this build defines");
    }
    if (value < RoboInputGamepadAxisMin || value > RoboInputGamepadAxisMax) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "an axis value outside the stated range is refused rather than clamped, "
                             "because a clamped reading is a plausible wrong number");
    }
    data->axis = axis;
    data->value = value;
    /* Derived, not trusted: a caller cannot hand over a zero sample that claims not to be centred. */
    if (value == 0) {
        data->flags |= (uint32_t)RoboInputGamepadFlagCentered;
    }
    return RoboOk;
}

int InputGamepadDataIsValid(const InputGamepadData *data)
{
    int hasButton;
    int hasAxis;

    if (!data) {
        return 0;
    }
    if ((data->flags & ~(uint32_t)RoboInputGamepadFlagAll) != 0u) {
        return 0;
    }
    hasButton = InputGamepadDataIsButton(data);
    hasAxis = InputGamepadDataIsAxis(data);
    /* Exactly one half, or neither (a cleared record, which is what a discarded event looks like). Never
     * both: see this file's comment. */
    if (hasButton && hasAxis) {
        return 0;
    }
    if (hasButton) {
        return data->value == 0 || data->value == 1;
    }
    if (hasAxis) {
        if (data->value < RoboInputGamepadAxisMin || data->value > RoboInputGamepadAxisMax) {
            return 0;
        }
        /* The flag and the value agree, as they do for a pen's pressure. */
        return ((data->flags & (uint32_t)RoboInputGamepadFlagCentered) != 0u) ==
               (data->value == 0);
    }
    return 1;
}

int InputGamepadDataIsButton(const InputGamepadData *data)
{
    return data != NULL && data->button != RoboGamepadButtonNone;
}

int InputGamepadDataIsAxis(const InputGamepadData *data)
{
    return data != NULL && data->axis != RoboGamepadAxisNone;
}

int InputGamepadDataEqual(const InputGamepadData *a, const InputGamepadData *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    return a->button == b->button && a->axis == b->axis && a->value == b->value &&
           a->flags == b->flags;
}
