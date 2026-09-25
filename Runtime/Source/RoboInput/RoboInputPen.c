/*
 * RoboInput pen payload (Phase 13.22) — see `RoboInputPen.h`.
 *
 * A small file, and the two rules worth reading are the two consistency rules `InputPenDataIsValid`
 * enforces, because they are the ones a producer can get wrong without noticing:
 *
 *   `PRESSURE_UNKNOWN` and a non-zero pressure cannot both be true -- the flag says the zero *is not a
 *   measurement*, so a record that carries a measurement and that flag has said two things at once;
 *
 *   `HOVER` and a tip contact cannot both be true -- hovering is being in range *without* touching, so a
 *   hover sample whose pointer half has the tip button down is a record that describes two different
 *   moments.
 *
 * Both are refused rather than repaired, for this module's standing reason: a repaired sample is a plausible
 * wrong one, and a stylus that silently drops a press is worse than one that reports nothing.
 */

#include "RoboInput/RoboInputPen.h"

#include <stddef.h>

void InputPenDataInit(InputPenData *data)
{
    if (!data) {
        return;
    }
    InputPointerDataInit(&data->pointer);
    data->pressure = 0;
    data->tiltX = 0;
    data->tiltY = 0;
    data->rotation = 0;
    data->flags = 0u;
}

Result InputPenDataSet(InputPenData *data,
                       const InputPointerData *pointer,
                       int32_t pressure,
                       int32_t tiltX,
                       int32_t tiltY,
                       int32_t rotation,
                       uint32_t flags,
                       InputDiag *diag)
{
    if (!data || !pointer) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "a pen sample needs somewhere to put it and a pointer sample");
    }
    /* Cleared first, so a refusal leaves an empty sample rather than a mixture of this call and the
     * previous accepted one. */
    InputPenDataInit(data);
    if (!InputPointerDataIsValid(pointer)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "the pointer half of a pen sample is not one this build may carry");
    }
    if ((flags & ~(uint32_t)RoboInputPenFlagAll) != 0u) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "pen flags carry a bit this build does not define");
    }
    if (pressure < 0 || pressure > RoboInputPenPressureMax) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "pressure is outside the stated range; a clamped measurement is a wrong "
                             "number a consumer cannot detect");
    }
    if (tiltX < RoboInputPenTiltMin || tiltX > RoboInputPenTiltMax ||
        tiltY < RoboInputPenTiltMin || tiltY > RoboInputPenTiltMax) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "tilt is outside the stated range");
    }
    if (rotation < 0 || rotation > RoboInputPenRotationMax) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "rotation is outside the stated range");
    }

    data->pointer = *pointer;
    data->pressure = pressure;
    data->tiltX = tiltX;
    data->tiltY = tiltY;
    data->rotation = rotation;
    data->flags = flags;
    return RoboOk;
}

int InputPenDataIsValid(const InputPenData *data)
{
    if (!data) {
        return 0;
    }
    if (!InputPointerDataIsValid(&data->pointer)) {
        return 0;
    }
    if ((data->flags & ~(uint32_t)RoboInputPenFlagAll) != 0u) {
        return 0;
    }
    if (data->pressure < 0 || data->pressure > RoboInputPenPressureMax) {
        return 0;
    }
    if (data->tiltX < RoboInputPenTiltMin || data->tiltX > RoboInputPenTiltMax ||
        data->tiltY < RoboInputPenTiltMin || data->tiltY > RoboInputPenTiltMax) {
        return 0;
    }
    if (data->rotation < 0 || data->rotation > RoboInputPenRotationMax) {
        return 0;
    }
    /* "There is no pressure measurement" and "there is one, and it is this" cannot both hold: the flag
     * says the zero is *not* a measurement, so a record carrying both has said two things at once. */
    if ((data->flags & (uint32_t)RoboInputPenFlagPressureUnknown) != 0u && data->pressure != 0) {
        return 0;
    }
    /* Hovering is being in range without touching, so a hover sample holds no contact. */
    if ((data->flags & (uint32_t)RoboInputPenFlagHover) != 0u &&
        (data->pointer.button != RoboMouseButtonNone || data->pointer.buttons != 0u)) {
        return 0;
    }
    return 1;
}

int InputPenDataIsHover(const InputPenData *data)
{
    return data != NULL && (data->flags & (uint32_t)RoboInputPenFlagHover) != 0u;
}

int InputPenDataIsEraser(const InputPenData *data)
{
    return data != NULL && (data->flags & (uint32_t)RoboInputPenFlagEraser) != 0u;
}

int InputPenDataEqual(const InputPenData *a, const InputPenData *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    /* The pointer half first, field for field -- the same comparison `InputEventEqual` makes for a pointer
     * event, and repeated here rather than reached for through a door because the two payloads are two
     * records. */
    if (a->pointer.pointer != b->pointer.pointer || a->pointer.button != b->pointer.button ||
        a->pointer.buttons != b->pointer.buttons ||
        !GfxPointEqual(a->pointer.position, b->pointer.position) ||
        !GfxPointEqual(a->pointer.previous, b->pointer.previous) ||
        a->pointer.hasPrevious != b->pointer.hasPrevious ||
        a->pointer.deltaX != b->pointer.deltaX || a->pointer.deltaY != b->pointer.deltaY ||
        a->pointer.clickCount != b->pointer.clickCount ||
        a->pointer.wheel.deltaX != b->pointer.wheel.deltaX ||
        a->pointer.wheel.deltaY != b->pointer.wheel.deltaY ||
        a->pointer.wheel.unit != b->pointer.wheel.unit ||
        a->pointer.flags != b->pointer.flags) {
        return 0;
    }
    return a->pressure == b->pressure && a->tiltX == b->tiltX && a->tiltY == b->tiltY &&
           a->rotation == b->rotation && a->flags == b->flags;
}
