/*
 * RoboInput keyboard suite (Phase 12.1) — `input.keyboard.*`.
 *
 * The payload is a *record of what a key did*: RoboWindow's two key identities plus the three things those
 * identities cannot carry (the action, the repeat flag, the text). These cases are about the three ways a
 * producer can get that wrong, since each one is a mistake a consumer would act on: an action that disagrees
 * with the event, a repeat flag invented for a release, or text on an event that produced none.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* The payload                                                         */
/* ------------------------------------------------------------------ */

static void TestKeyboardPayload(void)
{
    InputKeyboardData data;

    InputKeyboardDataInit(&data);
    RoboCheckEqInt(data.physical, RoboPhysicalKeyUnknown);
    RoboCheckEqInt(data.logical, RoboLogicalKeyUnknown);
    RoboCheckEqInt(data.action, RoboKeyActionNone);
    RoboCheckEqInt(data.repeat, 0);
    RoboCheckEqInt(data.text[0], '\0');
    /* A cleared payload is not a payload that happened: no action means the validator refuses it. */
    RoboCheck(!InputKeyboardDataIsValid(&data));
    InputKeyboardDataInit(NULL);

    /* A press of A, with the text the key produced. The *derived* flags are the interesting part: the
     * caller never sets `PHYSICAL_UNKNOWN` or `MODIFIER`, so they cannot forget them either. */
    RoboCheckEqInt(InputKeyboardDataSet(&data, RoboPhysicalKeyA, RoboLogicalKeyA,
                                                  RoboKeyActionDown, 0, "a"),
                      RoboOk);
    RoboCheck(InputKeyboardDataIsValid(&data));
    RoboCheckEqInt(data.flags, 0u);
    RoboCheckStrEq(data.text, "a");
    RoboCheckEqInt(data.scanCode, 0u);

    /* ... a modifier key reports itself as one, a numpad key reports its own flag, and both at once for a
     * numpad-located modifier is not a thing this table has. */
    RoboCheckEqInt(InputKeyboardDataSet(&data, RoboPhysicalKeyShiftLeft,
                                                  RoboLogicalKeyShift, RoboKeyActionDown, 0, NULL),
                      RoboOk);
    RoboCheckEqInt(data.flags, (uint32_t)RoboInputKeyFlagModifier);
    RoboCheckEqInt(InputKeyboardDataSet(&data, RoboPhysicalKeyNumpad5,
                                                  RoboLogicalKeyNumpad5, RoboKeyActionDown, 0, "5"),
                      RoboOk);
    RoboCheckEqInt(data.flags, (uint32_t)RoboInputKeyFlagNumpad);

    /* ... a key with no physical identity says so, and the flag it sets is the reason the record stays
     * valid: "I do not know which key this was" is a fact, not a missing field. */
    RoboCheckEqInt(InputKeyboardDataSet(&data, RoboPhysicalKeyUnknown,
                                                  RoboLogicalKeyF5, RoboKeyActionDown, 0, NULL),
                      RoboOk);
    RoboCheckEqInt(data.flags, (uint32_t)RoboInputKeyFlagPhysicalUnknown);
    RoboCheck(InputKeyboardDataIsValid(&data));

    /* A repeat is normalized to a flag, and a release can never be a repeat whatever the caller says. */
    RoboCheckEqInt(InputKeyboardDataSet(&data, RoboPhysicalKeyA, RoboLogicalKeyA,
                                                  RoboKeyActionDown, 7, "a"),
                      RoboOk);
    RoboCheckEqInt(data.repeat, 1);
    RoboCheckEqInt(InputKeyboardDataSet(&data, RoboPhysicalKeyA, RoboLogicalKeyA,
                                                  RoboKeyActionUp, 7, NULL),
                      RoboOk);
    RoboCheckEqInt(data.repeat, 0);

    /* What is refused: no action, an action that is not down/up, and an identity outside the vocabulary.
     * Each leaves the payload cleared rather than half-filled. */
    RoboCheckEqInt(InputKeyboardDataSet(&data, RoboPhysicalKeyA, RoboLogicalKeyA,
                                                  RoboKeyActionNone, 0, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(data.action, RoboKeyActionNone);
    RoboCheckEqInt(InputKeyboardDataSet(&data, (PhysicalKey)999,
                                                  RoboLogicalKeyA, RoboKeyActionDown, 0, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputKeyboardDataSet(&data, RoboPhysicalKeyA,
                                                  (LogicalKey)999, RoboKeyActionDown, 0, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputKeyboardDataSet(NULL, RoboPhysicalKeyA, RoboLogicalKeyA,
                                                  RoboKeyActionDown, 0, NULL),
                      RoboInvalidArgument);

    /* Text is bounded, and a fragment that does not fit is *refused*, not truncated: a truncated keystroke
     * is a different character. */
    {
        char longText[RoboInputTextMax + 4];
        size_t index;

        for (index = 0u; index < sizeof(longText) - 1u; index++) {
            longText[index] = 'x';
        }
        longText[sizeof(longText) - 1u] = '\0';
        RoboCheckEqInt(InputKeyboardDataSet(&data, RoboPhysicalKeyA, RoboLogicalKeyA,
                                                      RoboKeyActionDown, 0, longText),
                          RoboInvalidArgument);
        RoboCheckEqInt(data.text[0], '\0');
        RoboCheckEqInt(data.action, RoboKeyActionNone);

        /* The longest text that *does* fit, for the bound's other side. */
        for (index = 0u; index + 1u < RoboInputTextMax; index++) {
            longText[index] = 'y';
        }
        longText[RoboInputTextMax - 1u] = '\0';
        RoboCheckEqInt(InputKeyboardDataSet(&data, RoboPhysicalKeyA, RoboLogicalKeyA,
                                                      RoboKeyActionDown, 0, longText),
                          RoboOk);
        RoboCheckEqInt((int)strlen(data.text), (int)(RoboInputTextMax - 1u));
    }
}

static void TestKeyboardValidation(void)
{
    InputKeyboardData data = InputKeyData(RoboPhysicalKeyA, RoboLogicalKeyA,
                                                     RoboKeyActionDown, 0, "a");

    RoboCheck(InputKeyboardDataIsValid(&data));
    RoboCheck(!InputKeyboardDataIsValid(NULL));

    /* Undefined flag bits. */
    data.flags = (uint32_t)(1u << 20);
    RoboCheck(!InputKeyboardDataIsValid(&data));
    data.flags = 0u;

    /* The flag and the identity must agree in both directions. */
    data.flags = (uint32_t)RoboInputKeyFlagPhysicalUnknown;
    RoboCheck(!InputKeyboardDataIsValid(&data));
    data.flags = 0u;
    data.physical = RoboPhysicalKeyUnknown;
    RoboCheck(!InputKeyboardDataIsValid(&data));
    data.flags = (uint32_t)RoboInputKeyFlagPhysicalUnknown;
    RoboCheck(InputKeyboardDataIsValid(&data));
    data.physical = RoboPhysicalKeyA;
    data.flags = 0u;

    /* An action that is not down/up, and a repeat that is neither 0 nor 1. */
    data.action = RoboKeyActionNone;
    RoboCheck(!InputKeyboardDataIsValid(&data));
    data.action = RoboKeyActionDown;
    data.repeat = 2;
    RoboCheck(!InputKeyboardDataIsValid(&data));
    data.repeat = 0;

    /* A release that carried text, and a buffer that is not terminated inside its capacity. */
    data.action = RoboKeyActionUp;
    RoboCheck(!InputKeyboardDataIsValid(&data));
    data.action = RoboKeyActionDown;
    {
        size_t index;

        for (index = 0u; index < RoboInputTextMax; index++) {
            data.text[index] = 'z';
        }
        RoboCheck(!InputKeyboardDataIsValid(&data));
        data.text[0] = '\0';
    }

    /* An identity outside the vocabulary is refused by the validator too, not only by the setter: a record
     * can arrive from anywhere. */
    data.physical = (PhysicalKey)999;
    RoboCheck(!InputKeyboardDataIsValid(&data));
    data.physical = RoboPhysicalKeyA;
    data.logical = (LogicalKey)999;
    RoboCheck(!InputKeyboardDataIsValid(&data));
}

static void TestKeyboardModifiers(void)
{
    /* The four modifier groups report themselves and their side; the three locks report their lock. */
    RoboCheck(InputPhysicalKeyIsModifier(RoboPhysicalKeyShiftLeft));
    RoboCheck(InputPhysicalKeyIsModifier(RoboPhysicalKeyControlRight));
    RoboCheck(InputPhysicalKeyIsModifier(RoboPhysicalKeyAltLeft));
    RoboCheck(InputPhysicalKeyIsModifier(RoboPhysicalKeyMetaRight));
    RoboCheck(InputPhysicalKeyIsModifier(RoboPhysicalKeyCapsLock));
    RoboCheck(InputPhysicalKeyIsModifier(RoboPhysicalKeyNumLock));
    RoboCheck(InputPhysicalKeyIsModifier(RoboPhysicalKeyScrollLock));
    RoboCheck(!InputPhysicalKeyIsModifier(RoboPhysicalKeyA));
    RoboCheck(!InputPhysicalKeyIsModifier(RoboPhysicalKeyF5));
    RoboCheck(!InputPhysicalKeyIsModifier(RoboPhysicalKeyUnknown));

    RoboCheckEqInt(InputPhysicalKeyModifier(RoboPhysicalKeyShiftLeft),
                      RoboKeyModShift | RoboKeyModShiftLeft);
    RoboCheckEqInt(InputPhysicalKeyModifier(RoboPhysicalKeyShiftRight),
                      RoboKeyModShift | RoboKeyModShiftRight);
    RoboCheckEqInt(InputPhysicalKeyModifier(RoboPhysicalKeyControlLeft),
                      RoboKeyModCtrl | RoboKeyModCtrlLeft);
    RoboCheckEqInt(InputPhysicalKeyModifier(RoboPhysicalKeyAltRight),
                      RoboKeyModAlt | RoboKeyModAltRight);
    RoboCheckEqInt(InputPhysicalKeyModifier(RoboPhysicalKeyMetaLeft),
                      RoboKeyModMeta | RoboKeyModMetaLeft);
    RoboCheckEqInt(InputPhysicalKeyModifier(RoboPhysicalKeyCapsLock),
                      RoboKeyModCapsLock);
    RoboCheckEqInt(InputPhysicalKeyModifier(RoboPhysicalKeyScrollLock),
                      RoboKeyModScrollLock);
    RoboCheckEqInt(InputPhysicalKeyModifier(RoboPhysicalKeyA), RoboKeyModNone);
    RoboCheckEqInt(InputPhysicalKeyModifier(RoboPhysicalKeyUnknown), RoboKeyModNone);

    /* Every bit a key can produce is one the modifier vocabulary defines, which is what lets a state update
     * be a plain OR. */
    {
        int index;
        InputModifiers produced = RoboInputModifiersNone;

        for (index = 0; index < (int)RoboPhysicalKeyCount; index++) {
            produced |= InputPhysicalKeyModifier((PhysicalKey)index);
        }
        RoboCheck(InputModifiersAreKnown(produced));
        RoboCheck(produced != RoboInputModifiersNone);
    }
}

static void TestKeyboardActionAndText(void)
{
    InputKeyboardData down = InputKeyData(RoboPhysicalKeyA, RoboLogicalKeyA,
                                                     RoboKeyActionDown, 0, "a");
    InputKeyboardData up = InputKeyData(RoboPhysicalKeyA, RoboLogicalKeyA,
                                                   RoboKeyActionUp, 0, NULL);
    char text[RoboInputTextMax];

    /* The action a type implies, and the agreement rule the event validator leans on. */
    RoboCheckEqInt(InputKeyboardActionForEvent(RoboInputEventKeyDown),
                      RoboKeyActionDown);
    RoboCheckEqInt(InputKeyboardActionForEvent(RoboInputEventKeyUp), RoboKeyActionUp);
    RoboCheckEqInt(InputKeyboardActionForEvent(RoboInputEventPointerMove),
                      RoboKeyActionNone);
    RoboCheckEqInt(InputKeyboardActionForEvent(RoboInputEventTextInput),
                      RoboKeyActionNone);
    RoboCheck(InputKeyboardDataActionMatches(&down, RoboInputEventKeyDown));
    RoboCheck(!InputKeyboardDataActionMatches(&down, RoboInputEventKeyUp));
    RoboCheck(InputKeyboardDataActionMatches(&up, RoboInputEventKeyUp));
    RoboCheck(!InputKeyboardDataActionMatches(NULL, RoboInputEventKeyDown));

    /* Reading the text out: copied, terminated, and never longer than the destination. */
    RoboCheckEqInt((int)InputKeyboardDataText(&down, text, sizeof(text)), 1);
    RoboCheckStrEq(text, "a");
    RoboCheckEqInt((int)InputKeyboardDataText(&down, text, 1u), 0);
    RoboCheckStrEq(text, "");
    RoboCheckEqInt((int)InputKeyboardDataText(NULL, text, sizeof(text)), 0);
    RoboCheckEqInt((int)InputKeyboardDataText(&down, NULL, sizeof(text)), 0);
    RoboCheckEqInt((int)InputKeyboardDataText(&down, text, 0u), 0);

    /* Equality is field-by-field, and the text is part of it. */
    RoboCheck(InputKeyboardDataEqual(&down, &down));
    RoboCheck(InputKeyboardDataEqual(NULL, NULL));
    RoboCheck(!InputKeyboardDataEqual(&down, NULL));
    RoboCheck(!InputKeyboardDataEqual(&down, &up));
    RoboCheck(InputKeyboardDataEqual(&up, &up));
}

void TestRegisterInputKeyboard(void)
{
    TestRegister("input.keyboard.payload", TestKeyboardPayload);
    TestRegister("input.keyboard.validation", TestKeyboardValidation);
    TestRegister("input.keyboard.modifiers", TestKeyboardModifiers);
    TestRegister("input.keyboard.action_and_text", TestKeyboardActionAndText);
}
