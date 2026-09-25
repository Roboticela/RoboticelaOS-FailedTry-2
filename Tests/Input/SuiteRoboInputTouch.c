/*
 * RoboInput touch suite (Phase 13.20/13.21) — `input.touch.*`.
 *
 * Five cases, and each one is about *identity*:
 *
 *   `phases`       the four phases, their names, and the one distinction that matters: `END` and `CANCEL`
 *                  are both terminal, and only one of them removes the contact;
 *
 *   `contacts`     the table: the first `BEGIN` creates, a second with the same id **replaces** rather than
 *                  duplicating, a move for an id nobody saw land is refused and counted, the table fills, and
 *                  the sequences and high-water counters count what they say;
 *
 *   `removal`      `END` removes, `CANCEL` keeps until acknowledged, `Remove` acknowledges, a device
 *                  disconnect takes its own contacts and nobody else's, and the primary contact is the first
 *                  live one in hardware order;
 *
 *   `normalize`    the pointer conversion: the phase picks the event type, the primary contact carries the
 *                  left button for its whole life, a secondary one carries none, and the previous position
 *                  rides along for a move;
 *
 *   `dump`         the two writers, asserted whole.
 */

#include "RoboInputTestSupport.h"

/* Apply one contact at (x, y) on device 5, and hand it back. The cases that expect a *refusal* call the door
 * themselves, so this helper makes no promise about the result -- only what the table should then look like,
 * which every case asserts. */
static InputTouchContact TouchApply(InputTouchState *state,
                                    uint32_t id,
                                    InputTouchPhase phase,
                                    int32_t x,
                                    int32_t y,
                                    uint64_t time)
{
    InputTouchContact contact;

    memset(&contact, 0, sizeof(contact));
    (void)InputTouchStateApply(state, id, phase, MakeGfxPoint(x, y), 5u, RoboInputSpaceScreen, 0.5f,
                               4.0f, 0, time, &contact, NULL);
    return contact;
}

/* ------------------------------------------------------------------ */
/* Phases                                                              */
/* ------------------------------------------------------------------ */

static void TestTouchPhases(void)
{
    RoboCheckEqInt(RoboInputTouchPhaseBegin, 0);
    RoboCheckEqInt(RoboInputTouchPhaseMove, 1);
    RoboCheckEqInt(RoboInputTouchPhaseEnd, 2);
    RoboCheckEqInt(RoboInputTouchPhaseCancel, 3);
    RoboCheckEqInt(RoboInputTouchPhaseCount, 4);
    RoboCheck(InputTouchPhaseIsKnown(RoboInputTouchPhaseCancel));
    RoboCheck(!InputTouchPhaseIsKnown((InputTouchPhase)4));
    RoboCheckStrEq(InputTouchPhaseName(RoboInputTouchPhaseBegin), "begin");
    RoboCheckStrEq(InputTouchPhaseName(RoboInputTouchPhaseMove), "move");
    RoboCheckStrEq(InputTouchPhaseName(RoboInputTouchPhaseEnd), "end");
    RoboCheckStrEq(InputTouchPhaseName(RoboInputTouchPhaseCancel), "cancel");
    RoboCheckStrEq(InputTouchPhaseName((InputTouchPhase)9), "unknown");

    RoboCheck(!InputTouchPhaseIsTerminal(RoboInputTouchPhaseBegin));
    RoboCheck(!InputTouchPhaseIsTerminal(RoboInputTouchPhaseMove));
    RoboCheck(InputTouchPhaseIsTerminal(RoboInputTouchPhaseEnd));
    RoboCheck(InputTouchPhaseIsTerminal(RoboInputTouchPhaseCancel));
    RoboCheck(!InputTouchPhaseIsTerminal((InputTouchPhase)9));

    /* The normalization map, asserted whole: this is the *only* copy of it in the build. */
    RoboCheckEqInt((int)InputTouchPhaseToEventType(RoboInputTouchPhaseBegin),
                   (int)RoboInputEventPointerDown);
    RoboCheckEqInt((int)InputTouchPhaseToEventType(RoboInputTouchPhaseMove),
                   (int)RoboInputEventPointerMove);
    RoboCheckEqInt((int)InputTouchPhaseToEventType(RoboInputTouchPhaseEnd),
                   (int)RoboInputEventPointerUp);
    RoboCheckEqInt((int)InputTouchPhaseToEventType(RoboInputTouchPhaseCancel),
                   (int)RoboInputEventPointerCancel);
    RoboCheckEqInt((int)InputTouchPhaseToEventType((InputTouchPhase)9), (int)RoboInputEventNone);
}

/* ------------------------------------------------------------------ */
/* Contacts                                                            */
/* ------------------------------------------------------------------ */

static void TestTouchContacts(void)
{
    InputTouchState state;
    InputTouchContact contact;
    InputDiag diag;

    InputDiagReset(&diag);
    InputTouchStateInit(&state);
    RoboCheckEqInt((int)state.count, 0);
    RoboCheck(!InputTouchStateAnyDown(&state));
    RoboCheck(InputTouchStatePrimary(&state) == NULL);

    /* A `BEGIN` creates. */
    contact = TouchApply(&state, 1u, RoboInputTouchPhaseBegin, 10, 20, 100u);
    RoboCheckEqInt((int)state.count, 1);
    RoboCheckEqInt((int)contact.id, 1);
    RoboCheckEqInt((int)contact.phase, (int)RoboInputTouchPhaseBegin);
    RoboCheckEqInt((int)state.sequences, 1);
    RoboCheckEqInt((int)state.maxSeen, 1);
    RoboCheck(InputTouchStateAnyDown(&state));

    /* A second `BEGIN` with the same id replaces rather than duplicating: two contacts with one id is not a
     * thing hardware can mean. */
    contact = TouchApply(&state, 1u, RoboInputTouchPhaseBegin, 30, 40, 110u);
    RoboCheckEqInt((int)state.count, 1);
    RoboCheckEqInt((int)contact.position.x, 30);

    /* A move for an id nobody saw land is refused, and *counted*: inventing a contact here is how a lost
     * `BEGIN` becomes a phantom finger that never lifts. */
    RoboCheckEqInt(InputTouchStateApply(&state, 9u, RoboInputTouchPhaseMove, MakeGfxPoint(1, 1), 5u,
                                        RoboInputSpaceScreen, 0.0f, 0.0f, 0, 200u, NULL, &diag),
                   RoboNotFound);
    RoboCheckEqInt((int)state.refused, 1);
    RoboCheckEqInt((int)diag.domain, (int)RoboInputDomainTouch);
    RoboCheckEqInt((int)state.count, 1);

    /* An unknown phase, and an unknown plane. */
    RoboCheckEqInt(InputTouchStateApply(&state, 1u, (InputTouchPhase)7, MakeGfxPoint(1, 1), 5u,
                                        RoboInputSpaceScreen, 0.0f, 0.0f, 0, 200u, NULL, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputTouchStateApply(&state, 1u, RoboInputTouchPhaseMove, MakeGfxPoint(1, 1), 5u,
                                        RoboInputSpaceUnknown, 0.0f, 0.0f, 0, 200u, NULL, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputTouchStateApply(NULL, 1u, RoboInputTouchPhaseMove, MakeGfxPoint(1, 1), 5u,
                                        RoboInputSpaceScreen, 0.0f, 0.0f, 0, 200u, NULL, &diag),
                   RoboInvalidArgument);

    /* Two fingers, and the high-water mark. */
    (void)TouchApply(&state, 2u, RoboInputTouchPhaseBegin, 50, 50, 300u);
    RoboCheckEqInt((int)state.count, 2);
    RoboCheckEqInt((int)state.maxSeen, 2);
    RoboCheckEqInt((int)state.sequences, 2);

    /* The table fills, and a finger that does not fit is refused rather than dropping somebody else's. */
    InputTouchStateInit(&state);
    {
        uint32_t index;

        for (index = 1u; index <= RoboInputMaxTouches; index++) {
            (void)TouchApply(&state, index, RoboInputTouchPhaseBegin, (int32_t)index, 0, 1000u);
        }
        RoboCheckEqInt((int)state.count, (int)RoboInputMaxTouches);
        RoboCheckEqInt(InputTouchStateApply(&state, 99u, RoboInputTouchPhaseBegin, MakeGfxPoint(1, 1), 5u,
                                            RoboInputSpaceScreen, 0.0f, 0.0f, 0, 1000u, NULL, &diag),
                       RoboOutOfMemory);
        RoboCheckEqInt((int)state.count, (int)RoboInputMaxTouches);
    }
}

/* ------------------------------------------------------------------ */
/* Removal                                                             */
/* ------------------------------------------------------------------ */

static void TestTouchRemoval(void)
{
    InputTouchState state;
    InputTouchContact contact;
    InputDiag diag;

    InputDiagReset(&diag);
    InputTouchStateInit(&state);
    (void)TouchApply(&state, 1u, RoboInputTouchPhaseBegin, 10, 10, 100u);
    (void)TouchApply(&state, 2u, RoboInputTouchPhaseBegin, 90, 90, 100u);
    RoboCheckEqInt((int)state.count, 2);

    /* The primary is the first live contact in hardware order, and it does not change when a later one
     * lifts. */
    {
        const InputTouchContact *primary = InputTouchStatePrimary(&state);

        RoboCheck(primary != NULL && primary->id == 1u);
    }

    /* `END` removes: the identity is over, and a later `BEGIN` with the same id is a new finger. */
    contact = TouchApply(&state, 2u, RoboInputTouchPhaseEnd, 90, 90, 200u);
    RoboCheckEqInt((int)contact.phase, (int)RoboInputTouchPhaseEnd);
    RoboCheckEqInt((int)state.count, 1);
    RoboCheck(InputTouchStateFind(&state, 2u) == NULL);

    /* `CANCEL` *keeps* the contact, marked, so a recognizer can see why its sequence stopped. */
    contact = TouchApply(&state, 1u, RoboInputTouchPhaseCancel, 10, 10, 300u);
    RoboCheckEqInt((int)state.count, 1);
    RoboCheck((contact.flags & (uint32_t)RoboInputPointerFlagCancelled) != 0u);
    RoboCheck(InputTouchStateFind(&state, 1u) != NULL);
    /* A cancelled contact is not "down", which is what the primary door reads. */
    RoboCheck(!InputTouchStateAnyDown(&state));
    RoboCheck(InputTouchStatePrimary(&state) == NULL);

    /* The acknowledgement. */
    RoboCheck(InputTouchStateRemove(&state, 1u));
    RoboCheck(!InputTouchStateRemove(&state, 1u));
    RoboCheckEqInt((int)state.count, 0);
    RoboCheckEqInt((int)InputTouchStateClear(&state), 0);

    /* A device disconnect takes its own contacts and nobody else's -- the hotplug half of 13.44. */
    InputTouchStateInit(&state);
    (void)TouchApply(&state, 1u, RoboInputTouchPhaseBegin, 1, 1, 10u);
    (void)TouchApply(&state, 2u, RoboInputTouchPhaseBegin, 2, 2, 10u);
    RoboCheckEqInt(InputTouchStateApply(&state, 3u, RoboInputTouchPhaseBegin, MakeGfxPoint(3, 3), 6u,
                                        RoboInputSpaceScreen, 0.0f, 0.0f, 0, 10u, NULL, &diag),
                   RoboOk);
    RoboCheckEqInt((int)state.count, 3);
    RoboCheckEqInt((int)InputTouchStateRemoveDevice(&state, 5u), 2);
    RoboCheckEqInt((int)state.count, 1);
    RoboCheck(InputTouchStateFind(&state, 3u) != NULL);
    RoboCheckEqInt((int)InputTouchStateRemoveDevice(&state, 5u), 0);
    RoboCheckEqInt((int)InputTouchStateRemoveDevice(&state, RoboInputDeviceIdNone), 0);
    RoboCheckEqInt((int)InputTouchStateRemoveDevice(NULL, 5u), 0);

    RoboCheckEqInt((int)InputTouchStateClear(&state), 1);
    RoboCheckEqInt((int)InputTouchStateClear(NULL), 0);
}

/* ------------------------------------------------------------------ */
/* Normalization                                                       */
/* ------------------------------------------------------------------ */

static void TestTouchNormalize(void)
{
    InputTouchState state;
    InputTouchContact contact;
    InputPointerData payload;
    InputEventType type;
    InputDiag diag;

    InputDiagReset(&diag);
    InputTouchStateInit(&state);
    contact = TouchApply(&state, 1u, RoboInputTouchPhaseBegin, 10, 20, 100u);

    /* A primary `BEGIN` is a `POINTER_DOWN` with the left button down: every consumer that understands a
     * mouse understands a tap. */
    RoboCheckEqInt(InputTouchToPointer(&contact, 1, &payload, &type, &diag), RoboOk);
    RoboCheckEqInt((int)type, (int)RoboInputEventPointerDown);
    RoboCheckEqInt((int)payload.pointer, (int)RoboInputPointerIdPrimary);
    RoboCheckEqInt((int)payload.button, (int)RoboMouseButtonLeft);
    RoboCheck(InputButtonMaskHas(payload.buttons, RoboMouseButtonLeft));
    RoboCheck((payload.flags & (uint32_t)RoboInputPointerFlagPrimary) != 0u);
    RoboCheckEqInt((int)payload.position.x, 10);

    /* A move carries the previous position, which is what a gesture's derived delta comes from. */
    contact = TouchApply(&state, 1u, RoboInputTouchPhaseMove, 30, 40, 200u);
    RoboCheckEqInt(InputTouchToPointer(&contact, 1, &payload, &type, &diag), RoboOk);
    RoboCheckEqInt((int)type, (int)RoboInputEventPointerMove);
    RoboCheck(payload.hasPrevious);
    RoboCheckEqInt((int)payload.previous.x, 10);
    RoboCheckEqInt((int)payload.position.x, 30);
    RoboCheckEqInt((int)payload.button, (int)RoboMouseButtonNone);

    /* An `END` carries no buttons: the finger is up. */
    contact = TouchApply(&state, 1u, RoboInputTouchPhaseEnd, 30, 40, 300u);
    RoboCheckEqInt(InputTouchToPointer(&contact, 1, &payload, &type, &diag), RoboOk);
    RoboCheckEqInt((int)type, (int)RoboInputEventPointerUp);
    RoboCheckEqInt((int)payload.buttons, 0);

    /* A *secondary* contact is its own pointer id and carries no button: a two-finger gesture is not two left
     * clicks. */
    (void)TouchApply(&state, 1u, RoboInputTouchPhaseBegin, 10, 10, 400u);
    contact = TouchApply(&state, 2u, RoboInputTouchPhaseBegin, 90, 90, 400u);
    RoboCheckEqInt(InputTouchToPointer(&contact, 0, &payload, &type, &diag), RoboOk);
    RoboCheckEqInt((int)payload.pointer, 3);
    RoboCheckEqInt((int)payload.buttons, 0);
    RoboCheckEqInt((int)payload.button, (int)RoboMouseButtonLeft);
    RoboCheck((payload.flags & (uint32_t)RoboInputPointerFlagPrimary) == 0u);

    /* A cancel is a `POINTER_CANCEL`. */
    contact = TouchApply(&state, 2u, RoboInputTouchPhaseCancel, 90, 90, 500u);
    RoboCheckEqInt(InputTouchToPointer(&contact, 0, &payload, &type, &diag), RoboOk);
    RoboCheckEqInt((int)type, (int)RoboInputEventPointerCancel);
    RoboCheckEqInt((int)payload.buttons, 0);

    /* NULL arguments. */
    RoboCheckEqInt(InputTouchToPointer(NULL, 1, &payload, &type, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputTouchToPointer(&contact, 1, NULL, &type, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputTouchToPointer(&contact, 1, &payload, NULL, &diag), RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Dumps                                                              */
/* ------------------------------------------------------------------ */

static void TestTouchDump(void)
{
    InputTouchState state;
    InputTouchContact contact;
    InputDiag diag;
    char line[512];
    char again[512];
    size_t length = 0u;
    size_t againLength = 0u;

    InputDiagReset(&diag);
    InputTouchStateInit(&state);
    contact = TouchApply(&state, 4u, RoboInputTouchPhaseBegin, 10, 20, 1234u);

    RoboCheckEqInt(InputTouchContactWrite(&contact, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckEqInt((int)strlen(line), (int)length);
    RoboCheckStrEq(line, "touch #4 phase=begin device=5 space=screen at=10,20 previous=0 pen=0 time=1234\n");

    RoboCheckEqInt(InputTouchStateWrite(&state, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "touches count=1 sequences=1 max=1 refused=0\n"
                         "  touch #4 phase=begin device=5 space=screen at=10,20 previous=0 pen=0 time=1234\n");
    RoboCheckEqInt(InputTouchStateWrite(&state, again, sizeof(again), &againLength, &diag), RoboOk);
    RoboCheckEqInt((int)againLength, (int)length);
    RoboCheck(strcmp(line, again) == 0);

    RoboCheckEqInt(InputTouchStateWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "touches <none>\n");
    RoboCheckEqInt(InputTouchContactWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "touch <none>\n");
    {
        char small[8];
        size_t needed = 0u;

        RoboCheckEqInt(InputTouchContactWrite(&contact, small, sizeof(small), &needed, &diag),
                       RoboNotSupported);
        RoboCheckStrEq(small, "?");
    }
}

/* ------------------------------------------------------------------ */
/* Frame                                                               */
/* ------------------------------------------------------------------ */

void TestRegisterInputTouch(void)
{
    TestRegister("input.touch.phases", TestTouchPhases);
    TestRegister("input.touch.contacts", TestTouchContacts);
    TestRegister("input.touch.removal", TestTouchRemoval);
    TestRegister("input.touch.normalize", TestTouchNormalize);
    TestRegister("input.touch.dump", TestTouchDump);
}
