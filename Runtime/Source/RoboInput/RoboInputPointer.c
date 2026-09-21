/*
 * RoboInput pointer payload (Phase 12.1) — see RoboInputPointer.h.
 *
 * A payload is a *record of a place and a button state*, and the two facts it carries that a consumer
 * cannot recompute are the ones this file takes care of: the pressed-button **mask** (which must never
 * claim a button that does not exist) and the **previous** position (which must not be invented when the
 * platform did not report one). Everything else is a copy with a rule attached.
 */

#include "RoboInput/RoboInputPointer.h"

#include "RoboInputInternal.h"

#include "RoboWindow/RoboWindowMouse.h"

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Wheel                                                               */
/* ------------------------------------------------------------------ */

void InputWheelDataInit(InputWheelData *wheel)
{
    if (!wheel) {
        return;
    }
    wheel->deltaX = 0;
    wheel->deltaY = 0;
    wheel->unit = RoboMouseScrollUnitLine;
}

int InputScrollUnitIsKnown(MouseScrollUnit unit)
{
    return unit == RoboMouseScrollUnitLine || unit == RoboMouseScrollUnitPixel;
}

const char *InputScrollUnitName(MouseScrollUnit unit)
{
    switch (unit) {
    case RoboMouseScrollUnitLine:
        return "line";
    case RoboMouseScrollUnitPixel:
        return "pixel";
    default:
        break;
    }
    return "unknown";
}

/* ------------------------------------------------------------------ */
/* Button masks                                                        */
/* ------------------------------------------------------------------ */

uint32_t InputButtonMask(MouseButton button)
{
    if (!MouseButtonIsKnown(button)) {
        return 0u;
    }
    /* Button identities start at `LEFT == 1` and the mask is per *bit*, so the mapping is `1 << (button - 1)`
     * and nothing else in this module is allowed to write it. */
    return (uint32_t)1u << (uint32_t)((int)button - 1);
}

int InputButtonMaskHas(uint32_t mask, MouseButton button)
{
    uint32_t bit = InputButtonMask(button);

    return bit != 0u && (mask & bit) != 0u;
}

uint32_t InputButtonMaskApply(uint32_t mask, MouseButton button, int pressed)
{
    uint32_t bit = InputButtonMask(button);

    if (bit == 0u) {
        return mask;
    }
    return pressed ? (mask | bit) : (mask & ~bit);
}

/**
 * Every bit that stands for a button this build has.
 *
 * Written from the identities rather than as a literal so that a future button added to RoboWindow's enum is
 * included here by the loop in `InputButtonMask` and nowhere else.
 */
static uint32_t DefinedButtonMask(void)
{
    uint32_t defined = 0u;
    int button;

    for (button = (int)RoboMouseButtonLeft; button <= (int)RoboMouseButtonX2; button++) {
        defined |= InputButtonMask((MouseButton)button);
    }
    return defined;
}

Result InputButtonMaskWrite(uint32_t mask, char *buffer, size_t capacity, size_t *outLength)
{
    InputWriter text;
    int written = 0;
    int button;

    if (!buffer || capacity == 0u || !outLength) {
        return RoboInvalidArgument;
    }

    RiWriterInit(&text, buffer, capacity);
    for (button = (int)RoboMouseButtonLeft; button <= (int)RoboMouseButtonX2; button++) {
        if ((mask & InputButtonMask((MouseButton)button)) == 0u) {
            continue;
        }
        if (written) {
            RiWriterChar(&text, '+');
        }
        RiWriterText(&text, MouseButtonName((MouseButton)button));
        written = 1;
    }
    if (!written) {
        /* Lower case, like every other empty list in this module's dumps (`modifiers=none`, `flags=none`,
         * `previous=none`) and like the button names themselves, which are RoboWindow's. */
        RiWriterText(&text, "none");
    }
    return RiWriterFinish(&text, outLength, NULL);
}

/* ------------------------------------------------------------------ */
/* Pointer data                                                        */
/* ------------------------------------------------------------------ */

void InputPointerDataInit(InputPointerData *data)
{
    if (!data) {
        return;
    }
    data->pointer = RoboInputPointerIdPrimary;
    data->button = RoboMouseButtonNone;
    data->buttons = 0u;
    data->position = MakeGfxPoint(0, 0);
    data->previous = MakeGfxPoint(0, 0);
    data->hasPrevious = 0;
    data->deltaX = 0;
    data->deltaY = 0;
    data->clickCount = 0;
    InputWheelDataInit(&data->wheel);
    /* A cleared pointer is the primary one, because that is what a click with no pointer id means: an
     * event that forgot to name its pointer names the mouse rather than nothing. */
    data->flags = RoboInputPointerFlagPrimary;
}

Result InputPointerDataSet(InputPointerData *data,
                                          InputPointerId pointer,
                                          GfxPoint position,
                                          MouseButton button,
                                          uint32_t buttons)
{
    if (!data) {
        return RoboInvalidArgument;
    }
    if (button != RoboMouseButtonNone && !MouseButtonIsKnown(button)) {
        return RoboInvalidArgument;
    }
    if ((buttons & ~DefinedButtonMask()) != 0u) {
        return RoboInvalidArgument;
    }

    data->pointer = pointer;
    data->position = position;
    data->button = button;
    data->buttons = buttons;
    return RoboOk;
}

Result InputPointerDataSetPrevious(InputPointerData *data,
                                                   GfxPoint previous)
{
    int64_t dx;
    int64_t dy;

    if (!data) {
        return RoboInvalidArgument;
    }
    dx = (int64_t)data->position.x - (int64_t)previous.x;
    dy = (int64_t)data->position.y - (int64_t)previous.y;
    /* Two representable positions can differ by more than a delta can hold. That is *refused* rather than
     * truncated, because a wrapped delta is a movement in the wrong direction -- and refused before anything
     * is written, so a caller that gets an error still holds a coherent record. */
    if (dx < (int64_t)INT32_MIN || dx > (int64_t)INT32_MAX || dy < (int64_t)INT32_MIN ||
        dy > (int64_t)INT32_MAX) {
        return RoboError;
    }
    data->previous = previous;
    data->hasPrevious = 1;
    /* The delta is the *derived* form here, so both fields answer the same question rather than one being
     * derived and the other remembered. */
    data->deltaX = (int32_t)dx;
    data->deltaY = (int32_t)dy;
    return RoboOk;
}

Result InputPointerDataSetWheel(InputPointerData *data,
                                                int32_t deltaX,
                                                int32_t deltaY,
                                                MouseScrollUnit unit)
{
    if (!data) {
        return RoboInvalidArgument;
    }
    if (!InputScrollUnitIsKnown(unit)) {
        return RoboInvalidArgument;
    }
    data->wheel.deltaX = deltaX;
    data->wheel.deltaY = deltaY;
    data->wheel.unit = unit;
    return RoboOk;
}

int InputPointerDataIsValid(const InputPointerData *data)
{
    if (!data) {
        return 0;
    }
    if (data->button != RoboMouseButtonNone && !MouseButtonIsKnown(data->button)) {
        return 0;
    }
    /* The mask may carry only bits that stand for a real button: a bit for "none" or "unknown" would be an
     * event claiming a button no screen has. */
    if ((data->buttons & ~DefinedButtonMask()) != 0u) {
        return 0;
    }
    if (!InputScrollUnitIsKnown(data->wheel.unit)) {
        return 0;
    }
    if (data->clickCount < 0) {
        return 0;
    }
    if ((data->flags & ~RoboInputPointerFlagAll) != 0u) {
        return 0;
    }
    if (data->hasPrevious != 0 && data->hasPrevious != 1) {
        return 0;
    }
    return 1;
}
