/*
 * RoboInput pointer suite (Phase 12.1) — `input.pointer.*`.
 *
 * Three cases about the one payload that carries a *place*: what a record holds, what a mask may say, and
 * what a wheel's deltas mean. The second is the one worth reading: a pressed-button mask is a bitmask over
 * RoboWindow's button identities, so the mapping between a button and its bit lives in exactly one function,
 * and this case pins that mapping rather than describing it.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* The payload                                                         */
/* ------------------------------------------------------------------ */

static void TestPointerPayload(void)
{
    InputPointerData data;

    InputPointerDataInit(&data);
    RoboCheckEqInt(data.pointer, RoboInputPointerIdPrimary);
    RoboCheckEqInt(data.button, RoboMouseButtonNone);
    RoboCheckEqInt(data.buttons, 0u);
    RoboCheckEqInt(data.position.x, 0);
    RoboCheckEqInt(data.hasPrevious, 0);
    RoboCheckEqInt(data.deltaX, 0);
    RoboCheckEqInt(data.clickCount, 0);
    RoboCheckEqInt(data.wheel.unit, RoboMouseScrollUnitLine);
    /* A cleared pointer is the primary one: an event that forgot to name a pointer means the mouse. */
    RoboCheck(data.flags & RoboInputPointerFlagPrimary);
    RoboCheck(InputPointerDataIsValid(&data));
    InputPointerDataInit(NULL);

    /* Position, button and mask in one door, so a press cannot be recorded without its place. */
    RoboCheckEqInt(InputPointerDataSet(&data, RoboInputPointerIdPrimary,
                                                 MakeGfxPoint(120, 84), RoboMouseButtonRight,
                                                 InputButtonMaskApply(0u, RoboMouseButtonRight,
                                                                              1)),
                      RoboOk);
    RoboCheckEqInt(data.position.x, 120);
    RoboCheckEqInt(data.position.y, 84);
    RoboCheckEqInt(data.button, RoboMouseButtonRight);
    RoboCheck(InputButtonMaskHas(data.buttons, RoboMouseButtonRight));

    /* What is refused: an unknown button, a mask bit no button stands for, and NULL. */
    RoboCheckEqInt(InputPointerDataSet(&data, RoboInputPointerIdPrimary,
                                                 MakeGfxPoint(1, 2), RoboMouseButtonUnknown, 0u),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputPointerDataSet(&data, RoboInputPointerIdPrimary,
                                                 MakeGfxPoint(1, 2), RoboMouseButtonNone,
                                                 (uint32_t)(1u << 6)),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputPointerDataSet(NULL, RoboInputPointerIdPrimary,
                                                 MakeGfxPoint(1, 2), RoboMouseButtonNone, 0u),
                      RoboInvalidArgument);

    /* A previous position derives the delta, so the two fields cannot disagree. */
    RoboCheckEqInt(InputPointerDataSetPrevious(&data, MakeGfxPoint(118, 84)), RoboOk);
    RoboCheckEqInt(data.hasPrevious, 1);
    RoboCheckEqInt(data.previous.x, 118);
    RoboCheckEqInt(data.deltaX, 2);
    RoboCheckEqInt(data.deltaY, 0);
    /* A stationary report is legitimate, and a *negative* movement too. */
    RoboCheckEqInt(InputPointerDataSetPrevious(&data, MakeGfxPoint(120, 84)), RoboOk);
    RoboCheckEqInt(data.deltaX, 0);
    RoboCheckEqInt(InputPointerDataSetPrevious(&data, MakeGfxPoint(140, 100)), RoboOk);
    RoboCheckEqInt(data.deltaX, -20);
    RoboCheckEqInt(data.deltaY, -16);
    RoboCheckEqInt(InputPointerDataSetPrevious(NULL, MakeGfxPoint(0, 0)),
                      RoboInvalidArgument);

    /* Two representable positions can differ by more than a delta can hold. That is refused, and *nothing*
     * is written: the record still holds the previous position and delta of the last accepted call. */
    {
        InputPointerData extremes;

        InputPointerDataInit(&extremes);
        RoboCheckEqInt(InputPointerDataSet(&extremes, RoboInputPointerIdPrimary,
                                                     MakeGfxPoint(INT32_MIN, 0), RoboMouseButtonNone,
                                                     0u),
                          RoboOk);
        RoboCheckEqInt(InputPointerDataSetPrevious(&extremes,
                                                              MakeGfxPoint(INT32_MAX, 0)),
                          RoboError);
        RoboCheckEqInt(extremes.hasPrevious, 0);
        RoboCheckEqInt(extremes.deltaX, 0);
        RoboCheckEqInt(extremes.previous.x, 0);
        /* ... and the record is still one the module would carry. */
        RoboCheck(InputPointerDataIsValid(&extremes));
    }

    /* The wheel: deltas and the unit they are in, with an undefined unit refused. */
    RoboCheckEqInt(InputPointerDataSetWheel(&data, 0, 120, RoboMouseScrollUnitLine),
                      RoboOk);
    RoboCheckEqInt(data.wheel.deltaY, 120);
    RoboCheckEqInt(data.wheel.unit, RoboMouseScrollUnitLine);
    RoboCheckEqInt(InputPointerDataSetWheel(&data, -40, 0, RoboMouseScrollUnitPixel),
                      RoboOk);
    RoboCheckEqInt(data.wheel.deltaX, -40);
    RoboCheckEqInt(InputPointerDataSetWheel(&data, 0, 0, (MouseScrollUnit)7),
                      RoboInvalidArgument);
    RoboCheckEqInt(data.wheel.unit, RoboMouseScrollUnitPixel);
    RoboCheckEqInt(InputPointerDataSetWheel(NULL, 0, 0, RoboMouseScrollUnitLine),
                      RoboInvalidArgument);

    /* Identities. */
    RoboCheckEqInt(InputScrollUnitIsKnown(RoboMouseScrollUnitLine), 1);
    RoboCheckEqInt(InputScrollUnitIsKnown(RoboMouseScrollUnitPixel), 1);
    RoboCheckEqInt(InputScrollUnitIsKnown((MouseScrollUnit)7), 0);
    RoboCheckStrEq(InputScrollUnitName(RoboMouseScrollUnitLine), "line");
    RoboCheckStrEq(InputScrollUnitName(RoboMouseScrollUnitPixel), "pixel");
    RoboCheckStrEq(InputScrollUnitName((MouseScrollUnit)7), "unknown");
}

static void TestPointerValidation(void)
{
    InputPointerData data = GetRoboInputPointerData(5, 6, RoboMouseButtonNone, 0u);

    RoboCheck(InputPointerDataIsValid(&data));
    RoboCheck(!InputPointerDataIsValid(NULL));

    /* A mask bit for a button that does not exist -- including "none" and "unknown", whose identities are
     * 0 and 6 and would be bits 0 and 5 if the mapping were taken literally. */
    data.buttons = (uint32_t)(1u << 5);
    RoboCheck(!InputPointerDataIsValid(&data));
    data.buttons = 0u;

    /* An unknown button. */
    data.button = RoboMouseButtonUnknown;
    RoboCheck(!InputPointerDataIsValid(&data));
    data.button = RoboMouseButtonNone;

    /* A unit this build does not define, a negative click count, an undefined flag, and a `hasPrevious`
     * that is neither yes nor no. */
    data.wheel.unit = (MouseScrollUnit)9;
    RoboCheck(!InputPointerDataIsValid(&data));
    data.wheel.unit = RoboMouseScrollUnitLine;
    data.clickCount = -1;
    RoboCheck(!InputPointerDataIsValid(&data));
    data.clickCount = 0;
    data.flags = (uint32_t)(1u << 20);
    RoboCheck(!InputPointerDataIsValid(&data));
    data.flags = 0u;
    data.hasPrevious = 2;
    RoboCheck(!InputPointerDataIsValid(&data));
    data.hasPrevious = 1;
    RoboCheck(InputPointerDataIsValid(&data));
    data.hasPrevious = 0;

    /* A cleared payload is valid: it describes a pointer that has not been heard from, which is a fact. */
    InputPointerDataInit(&data);
    RoboCheck(InputPointerDataIsValid(&data));

    /* The payload does *not* judge the deltas: a wheel that scrolled nowhere is something a high-resolution
     * device reports, and refusing it would drop a real event. */
    data.wheel.deltaX = 0;
    data.wheel.deltaY = 0;
    RoboCheck(InputPointerDataIsValid(&data));
}

static void TestPointerButtons(void)
{
    /* The mapping, pinned: bit `n - 1` for button `n`, and nothing for the two identities that are not
     * buttons. Everything else in this module reads the mask through these doors. */
    RoboCheckEqInt(InputButtonMask(RoboMouseButtonLeft), 1u);
    RoboCheckEqInt(InputButtonMask(RoboMouseButtonRight), 2u);
    RoboCheckEqInt(InputButtonMask(RoboMouseButtonMiddle), 4u);
    RoboCheckEqInt(InputButtonMask(RoboMouseButtonX1), 8u);
    RoboCheckEqInt(InputButtonMask(RoboMouseButtonX2), 16u);
    RoboCheckEqInt(InputButtonMask(RoboMouseButtonNone), 0u);
    RoboCheckEqInt(InputButtonMask(RoboMouseButtonUnknown), 0u);

    /* Set, clear, query. */
    {
        uint32_t mask = 0u;

        RoboCheck(!InputButtonMaskHas(mask, RoboMouseButtonLeft));
        mask = InputButtonMaskApply(mask, RoboMouseButtonLeft, 1);
        mask = InputButtonMaskApply(mask, RoboMouseButtonX1, 1);
        RoboCheckEqInt(mask, 9u);
        RoboCheck(InputButtonMaskHas(mask, RoboMouseButtonLeft));
        RoboCheck(InputButtonMaskHas(mask, RoboMouseButtonX1));
        RoboCheck(!InputButtonMaskHas(mask, RoboMouseButtonRight));
        mask = InputButtonMaskApply(mask, RoboMouseButtonLeft, 0);
        RoboCheckEqInt(mask, 8u);
        RoboCheck(!InputButtonMaskHas(mask, RoboMouseButtonLeft));

        /* Applying a non-button leaves the mask alone rather than clearing a bit that is not there. */
        RoboCheckEqInt(InputButtonMaskApply(mask, RoboMouseButtonNone, 1), mask);
        RoboCheckEqInt(InputButtonMaskApply(mask, RoboMouseButtonUnknown, 1), mask);
        RoboCheck(!InputButtonMaskHas(mask, RoboMouseButtonNone));
    }

    /* The text form a debug line reads, from nothing to everything. */
    {
        char buffer[64];
        size_t length = 0u;
        uint32_t all = 0u;
        int index;

        for (index = (int)RoboMouseButtonLeft; index <= (int)RoboMouseButtonX2; index++) {
            all = InputButtonMaskApply(all, (MouseButton)index, 1);
        }

        RoboCheckEqInt(InputButtonMaskWrite(0u, buffer, sizeof(buffer), &length), RoboOk);
        RoboCheckStrEq(buffer, "none");
        RoboCheckEqInt((int)length, 4);
        RoboCheckEqInt(InputButtonMaskWrite(InputButtonMask(RoboMouseButtonLeft),
                                                      buffer, sizeof(buffer), &length),
                          RoboOk);
        RoboCheckStrEq(buffer, "left");
        RoboCheckEqInt(InputButtonMaskWrite(3u, buffer, sizeof(buffer), &length), RoboOk);
        RoboCheckStrEq(buffer, "left+right");
        RoboCheckEqInt(InputButtonMaskWrite(all, buffer, sizeof(buffer), &length), RoboOk);
        RoboCheckStrEq(buffer, "left+right+middle+x1+x2");

        /* A buffer too small refuses and reports what it needed, leaving a `?` rather than a cut-off list. */
        RoboCheckEqInt(InputButtonMaskWrite(all, buffer, 4u, &length), RoboNotSupported);
        RoboCheckStrEq(buffer, "?");
        RoboCheckEqInt((int)length, 23);
        RoboCheckEqInt((int)strlen("left+right+middle+x1+x2"), 23);
        RoboCheckEqInt(InputButtonMaskWrite(0u, NULL, sizeof(buffer), &length),
                          RoboInvalidArgument);
        RoboCheckEqInt(InputButtonMaskWrite(0u, buffer, sizeof(buffer), NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(InputButtonMaskWrite(0u, buffer, 0u, &length), RoboInvalidArgument);
    }
}

void TestRegisterInputPointer(void)
{
    TestRegister("input.pointer.payload", TestPointerPayload);
    TestRegister("input.pointer.validation", TestPointerValidation);
    TestRegister("input.pointer.buttons", TestPointerButtons);
}
