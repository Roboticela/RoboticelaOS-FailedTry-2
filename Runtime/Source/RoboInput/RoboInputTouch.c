/*
 * Multi-touch (Phase 13.20/13.21) — see `RoboInputTouch.h`.
 *
 * Two pieces: the contact table, whose whole subject is identity, and the one conversion into the pointer
 * model. Both are pure: the state is a value a caller owns, and nothing here reads a device or routes an
 * event.
 */

#include "RoboInput/RoboInputTouch.h"

#include "RoboInputInternal.h"

#include <string.h>

static const char *const RiTouchPhaseNames[RoboInputTouchPhaseCount] = {
    "begin", "move", "end", "cancel"
};

int InputTouchPhaseIsKnown(InputTouchPhase phase)
{
    return (int)phase >= 0 && (size_t)phase < (size_t)RoboInputTouchPhaseCount;
}

const char *InputTouchPhaseName(InputTouchPhase phase)
{
    if (!InputTouchPhaseIsKnown(phase)) {
        return "unknown";
    }
    return RiTouchPhaseNames[(size_t)phase];
}

int InputTouchPhaseIsTerminal(InputTouchPhase phase)
{
    return phase == RoboInputTouchPhaseEnd || phase == RoboInputTouchPhaseCancel;
}

void InputTouchStateInit(InputTouchState *state)
{
    if (!state) {
        return;
    }
    memset(state, 0, sizeof(*state));
}

/* Liveness is "a device is named": the slot is cleared by writing `RoboInputDeviceIdNone`, which is what
 * makes removal and a cancelled-but-unacknowledged contact two different states rather than two flags. */
static InputTouchContact *RiTouchFind(InputTouchState *state, uint32_t id)
{
    uint32_t index;

    if (!state) {
        return NULL;
    }
    for (index = 0u; index < RoboInputMaxTouches; index++) {
        if (state->contacts[index].device != RoboInputDeviceIdNone &&
            state->contacts[index].id == id) {
            return &state->contacts[index];
        }
    }
    return NULL;
}

Result InputTouchStateApply(InputTouchState *state,
                            uint32_t id,
                            InputTouchPhase phase,
                            GfxPoint position,
                            InputDeviceId device,
                            InputSpace space,
                            float pressure,
                            float size,
                            int pen,
                            uint64_t timestampMs,
                            InputTouchContact *out,
                            InputDiag *diag)
{
    InputTouchContact *contact;
    uint32_t index;
    int existed;

    if (out) {
        memset(out, 0, sizeof(*out));
    }
    if (!state) {
        return RoboInputDiag(diag, RoboInputDomainTouch, RoboInvalidArgument,
                             "a touch state is required");
    }
    if (!InputTouchPhaseIsKnown(phase)) {
        state->refused++;
        return RoboInputDiag(diag, RoboInputDomainTouch, RoboInvalidArgument,
                             "a contact update names a defined phase");
    }
    if (!InputSpaceIsKnown(space)) {
        state->refused++;
        return RoboInputDiag(diag, RoboInputDomainTouch, RoboInvalidArgument,
                             "a contact needs a known plane");
    }

    contact = RiTouchFind(state, id);
    existed = contact != NULL;
    if (!contact && phase != RoboInputTouchPhaseBegin) {
        /*
         * A move or an end for a finger nobody saw land. Inventing a contact here is how a lost `BEGIN`
         * becomes a phantom finger that never lifts, so it is refused and *counted*.
         */
        state->refused++;
        return RoboInputDiag(diag, RoboInputDomainTouch, RoboNotFound,
                             "no live contact has that id");
    }
    if (!contact) {
        for (index = 0u; index < RoboInputMaxTouches; index++) {
            if (state->contacts[index].device == RoboInputDeviceIdNone) {
                contact = &state->contacts[index];
                break;
            }
        }
        if (!contact) {
            state->refused++;
            return RoboInputDiag(diag, RoboInputDomainTouch, RoboOutOfMemory,
                                 "the contact table is full");
        }
        memset(contact, 0, sizeof(*contact));
        contact->id = id;
        contact->device = device;
        state->count++;
        state->sequences++;
        if (state->count > state->maxSeen) {
            state->maxSeen = state->count;
        }
    }

    contact->phase = phase;
    /*
     * A contact that *just began* has no previous position, and saying it has one would put a `hasPrevious`
     * with nothing behind it on every first sample -- which a consumer reads as "the finger moved here from
     * `position`", a movement that did not happen.
     */
    if (existed) {
        contact->previous = contact->position;
        contact->hasPrevious = 1;
    } else {
        contact->previous = position;
        contact->hasPrevious = 0;
    }
    contact->position = position;
    contact->device = device;
    contact->space = space;
    contact->pressure = pressure;
    contact->size = size;
    contact->pen = pen ? 1 : 0;
    contact->timestampMs = timestampMs;
    if (phase == RoboInputTouchPhaseCancel) {
        contact->flags |= (uint32_t)RoboInputPointerFlagCancelled;
    }

    if (phase == RoboInputTouchPhaseEnd) {
        /* An `END` removes the contact: its identity is over, and a later `BEGIN` with the same id is a new
         * finger rather than a continuation. */
        contact->device = RoboInputDeviceIdNone;
        contact->flags = 0u;
        if (state->count > 0u) {
            state->count--;
        }
        if (out) {
            *out = *contact;
        }
        if (diag) {
            InputDiagReset(diag);
        }
        return RoboOk;
    }

    if (out) {
        *out = *contact;
    }
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

const InputTouchContact *InputTouchStateFind(const InputTouchState *state, uint32_t id)
{
    uint32_t index;

    if (!state) {
        return NULL;
    }
    for (index = 0u; index < RoboInputMaxTouches; index++) {
        if (state->contacts[index].device == RoboInputDeviceIdNone) {
            continue;
        }
        if (state->contacts[index].id != id) {
            continue;
        }
        /* A cancelled contact is still findable: that is how a recognizer learns *why* its sequence
         * stopped instead of waiting for an end that will never come. */
        return &state->contacts[index];
    }
    return NULL;
}

int InputTouchStateRemove(InputTouchState *state, uint32_t id)
{
    uint32_t index;

    if (!state) {
        return 0;
    }
    for (index = 0u; index < RoboInputMaxTouches; index++) {
        if (state->contacts[index].device == RoboInputDeviceIdNone ||
            state->contacts[index].id != id) {
            continue;
        }
        memset(&state->contacts[index], 0, sizeof(state->contacts[index]));
        if (state->count > 0u) {
            state->count--;
        }
        return 1;
    }
    return 0;
}

uint32_t InputTouchStateRemoveDevice(InputTouchState *state, InputDeviceId device)
{
    uint32_t index;
    uint32_t removed = 0u;

    if (!state || device == RoboInputDeviceIdNone) {
        return 0u;
    }
    for (index = 0u; index < RoboInputMaxTouches; index++) {
        if (state->contacts[index].device == device) {
            memset(&state->contacts[index], 0, sizeof(state->contacts[index]));
            removed++;
        }
    }
    if (removed > state->count) {
        state->count = 0u;
    } else {
        state->count -= removed;
    }
    return removed;
}

uint32_t InputTouchStateClear(InputTouchState *state)
{
    uint32_t removed;

    if (!state) {
        return 0u;
    }
    removed = state->count;
    memset(state->contacts, 0, sizeof(state->contacts));
    state->count = 0u;
    return removed;
}

int InputTouchStateAnyDown(const InputTouchState *state)
{
    uint32_t index;

    if (!state) {
        return 0;
    }
    for (index = 0u; index < RoboInputMaxTouches; index++) {
        if (state->contacts[index].device == RoboInputDeviceIdNone) {
            continue;
        }
        if ((state->contacts[index].flags & (uint32_t)RoboInputPointerFlagCancelled) != 0u) {
            continue;
        }
        return 1;
    }
    return 0;
}

const InputTouchContact *InputTouchStatePrimary(const InputTouchState *state)
{
    uint32_t index;

    if (!state) {
        return NULL;
    }
    for (index = 0u; index < RoboInputMaxTouches; index++) {
        if (state->contacts[index].device == RoboInputDeviceIdNone) {
            continue;
        }
        if ((state->contacts[index].flags & (uint32_t)RoboInputPointerFlagCancelled) != 0u) {
            continue;
        }
        return &state->contacts[index];
    }
    return NULL;
}

/* ============================================================================================== *
 * Normalization
 * ============================================================================================== */

InputEventType InputTouchPhaseToEventType(InputTouchPhase phase)
{
    switch (phase) {
    case RoboInputTouchPhaseBegin:
        return RoboInputEventPointerDown;
    case RoboInputTouchPhaseMove:
        return RoboInputEventPointerMove;
    case RoboInputTouchPhaseEnd:
        return RoboInputEventPointerUp;
    case RoboInputTouchPhaseCancel:
        return RoboInputEventPointerCancel;
    default:
        return RoboInputEventNone;
    }
}

Result InputTouchToPointer(const InputTouchContact *contact,
                           int isPrimary,
                           InputPointerData *out,
                           InputEventType *outType,
                           InputDiag *diag)
{
    uint32_t buttons = 0u;
    InputPointerId pointer;

    if (!contact) {
        return RoboInputDiag(diag, RoboInputDomainTouch, RoboInvalidArgument,
                             "a contact is required");
    }
    if (!out || !outType) {
        return RoboInputDiag(diag, RoboInputDomainTouch, RoboInvalidArgument,
                             "a conversion needs a payload and a type to fill");
    }
    if (!InputSpaceIsKnown(contact->space)) {
        return RoboInputDiag(diag, RoboInputDomainTouch, RoboInvalidArgument,
                             "a contact with no plane has no position to convert");
    }

    InputPointerDataInit(out);
    /*
     * A touch has no buttons, and every consumer that understands a mouse understands a tap only if the
     * primary contact reports the left button down. So a *primary* contact carries a touch's one button for
     * its whole life, and a secondary one carries none: a two-finger gesture is not two left clicks.
     */
    pointer = isPrimary ? RoboInputPointerIdPrimary : (InputPointerId)(contact->id + 1u);
    if (isPrimary && contact->phase != RoboInputTouchPhaseEnd &&
        contact->phase != RoboInputTouchPhaseCancel) {
        buttons = InputButtonMask(RoboMouseButtonLeft);
    }
    if (contact->phase == RoboInputTouchPhaseEnd || contact->phase == RoboInputTouchPhaseCancel) {
        buttons = 0u;
    }

    *outType = InputTouchPhaseToEventType(contact->phase);
    if (*outType == RoboInputEventNone) {
        return RoboInputDiag(diag, RoboInputDomainTouch, RoboInvalidArgument,
                             "that phase has no pointer event");
    }

    {
        Result status = InputPointerDataSet(out, pointer, contact->position,
                                            contact->phase == RoboInputTouchPhaseBegin
                                                ? RoboMouseButtonLeft
                                                : RoboMouseButtonNone,
                                            buttons);

        if (status != RoboOk) {
            return RoboInputDiag(diag, RoboInputDomainTouch, status,
                                 "the contact's position could not be expressed as a pointer payload");
        }
    }

    if (contact->hasPrevious &&
        (contact->phase == RoboInputTouchPhaseMove || contact->phase == RoboInputTouchPhaseEnd)) {
        if (InputPointerDataSetPrevious(out, contact->previous) != RoboOk) {
            /* A previous position more than a delta can hold is refused by that door; the movement is then
             * simply not stated, which is a smaller loss than a wrapped one. */
            out->hasPrevious = 0;
            out->previous = contact->position;
        }
    }
    out->flags = contact->flags;
    if (isPrimary) {
        out->flags |= (uint32_t)RoboInputPointerFlagPrimary;
    }
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

/* ============================================================================================== *
 * Diagnostics
 * ============================================================================================== */

Result InputTouchContactWrite(const InputTouchContact *contact,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              InputDiag *diag)
{
    InputWriter text;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                             "a dump needs a buffer, a capacity and somewhere to put the length");
    }
    RiWriterInit(&text, buffer, capacity);
    if (!contact) {
        RiWriterText(&text, "touch <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "touch #");
    RiWriterU64(&text, (uint64_t)contact->id);
    RiWriterFieldText(&text, " phase", InputTouchPhaseName(contact->phase));
    RiWriterFieldU64(&text, " device", (uint64_t)contact->device);
    RiWriterFieldText(&text, " space", InputSpaceName(contact->space));
    RiWriterFieldPoint(&text, " at", (int32_t)contact->position.x, (int32_t)contact->position.y);
    RiWriterFieldU64(&text, " previous", (uint64_t)(contact->hasPrevious ? 1u : 0u));
    RiWriterFieldU64(&text, " pen", (uint64_t)(contact->pen ? 1u : 0u));
    RiWriterFieldU64(&text, " time", contact->timestampMs);
    RiWriterChar(&text, '\n');
    return RiWriterFinish(&text, outLength, diag);
}

Result InputTouchStateWrite(const InputTouchState *state,
                            char *buffer,
                            size_t capacity,
                            size_t *outLength,
                            InputDiag *diag)
{
    InputWriter text;
    uint32_t index;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                             "a dump needs a buffer, a capacity and somewhere to put the length");
    }
    RiWriterInit(&text, buffer, capacity);
    if (!state) {
        RiWriterText(&text, "touches <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "touches count=");
    RiWriterU64(&text, (uint64_t)state->count);
    RiWriterFieldU64(&text, " sequences", (uint64_t)state->sequences);
    RiWriterFieldU64(&text, " max", (uint64_t)state->maxSeen);
    RiWriterFieldU64(&text, " refused", (uint64_t)state->refused);
    RiWriterChar(&text, '\n');
    for (index = 0u; index < RoboInputMaxTouches; index++) {
        char line[256];
        size_t lineLength = 0u;

        if (state->contacts[index].device == RoboInputDeviceIdNone) {
            continue;
        }
        if (InputTouchContactWrite(&state->contacts[index], line, sizeof(line), &lineLength, NULL) !=
            RoboOk) {
            RiWriterText(&text, "  touch ?\n");
            continue;
        }
        RiWriterText(&text, "  ");
        RiWriterText(&text, line);
    }
    return RiWriterFinish(&text, outLength, diag);
}
