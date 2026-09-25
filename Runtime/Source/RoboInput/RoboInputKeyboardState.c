/*
 * RoboInput keyboard state (Phase 13.2) — see `RoboInputKeyboardState.h`.
 *
 * Three pieces, in the order a key meets them:
 *
 *   **the held table**   one array of `(physical, logical, device)`, in press order, and the five doors
 *                        that read it: is this key down, is any key with this meaning down, how many, which
 *                        one, and does a device hold any. Everything else about a keyboard is computed from
 *                        it, which is what makes a disconnect unable to strand a key.
 *
 *   **the derivation**   `InputKeyboardStateModifiers` unions `InputPhysicalKeyModifier` over the held
 *                        keys, masks the lock bits out, and ORs in the locks this record keeps -- because a
 *                        lock is on after its key is released and a modifier is not.
 *
 *   **the repeat**       arming on a press, disarming on a release and a disconnect, and the one door that
 *                        turns a clock into `KEY_REPEAT` events with a deadline it pushes forward.
 *
 * A refused press changes nothing at all: each door validates before it writes, so a caller that ignored a
 * refusal is in the state it was in rather than in a half-updated one.
 */

#include "RoboInput/RoboInputKeyboardState.h"

#include "RoboInputInternal.h"

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Repeat configuration                                                */
/* ------------------------------------------------------------------ */

void InputKeyboardRepeatConfigInit(InputKeyboardRepeatConfig *config)
{
    if (!config) {
        return;
    }
    config->delayMs = RoboInputKeyboardRepeatDelayMsDefault;
    config->intervalMs = RoboInputKeyboardRepeatIntervalMsDefault;
}

int InputKeyboardRepeatConfigIsEnabled(const InputKeyboardRepeatConfig *config)
{
    return config != NULL && config->intervalMs != 0u;
}

/* ------------------------------------------------------------------ */
/* Construction and the held table                                     */
/* ------------------------------------------------------------------ */

void InputKeyboardStateInit(InputKeyboardState *state)
{
    if (!state) {
        return;
    }
    InputKeyboardStateReset(state);
}

void InputKeyboardStateReset(InputKeyboardState *state)
{
    if (!state) {
        return;
    }
    InputKeyboardStateReleaseAll(state);
    state->locks = RoboInputModifiersNone;
    /* Boot has the default cadence. Without this the record's `repeat` would be whatever the caller's stack
     * happened to hold, and the first press would arm a deadline in 1970 -- which is exactly what
     * `input.keyboard.repeat` caught when this call was missing. */
    InputKeyboardRepeatConfigInit(&state->repeat);
}

void InputKeyboardStateReleaseAll(InputKeyboardState *state)
{
    uint32_t index;

    if (!state) {
        return;
    }
    for (index = 0u; index < RoboInputKeyboardHeldMax; index++) {
        state->held[index].physical = RoboPhysicalKeyUnknown;
        state->held[index].logical = RoboLogicalKeyUnknown;
        state->held[index].device = RoboInputDeviceIdNone;
    }
    state->heldCount = 0u;
    state->repeatArmed = 0;
    state->repeatPhysical = RoboPhysicalKeyUnknown;
    state->repeatLogical = RoboLogicalKeyUnknown;
    state->repeatDevice = RoboInputDeviceIdNone;
    state->repeatDueMs = 0u;
}

uint32_t InputKeyboardStateHeldCount(const InputKeyboardState *state)
{
    return state ? state->heldCount : 0u;
}

int InputKeyboardStateIsKeyDown(const InputKeyboardState *state, PhysicalKey physical)
{
    uint32_t index;

    if (!state || physical == RoboPhysicalKeyUnknown) {
        return 0;
    }
    for (index = 0u; index < state->heldCount; index++) {
        if (state->held[index].physical == physical) {
            return 1;
        }
    }
    return 0;
}

int InputKeyboardStateIsLogicalKeyDown(const InputKeyboardState *state, LogicalKey logical)
{
    uint32_t index;

    if (!state || logical == RoboLogicalKeyUnknown) {
        return 0;
    }
    for (index = 0u; index < state->heldCount; index++) {
        if (state->held[index].logical == logical) {
            return 1;
        }
    }
    return 0;
}

int InputKeyboardStateHoldsDevice(const InputKeyboardState *state, InputDeviceId device)
{
    uint32_t index;

    if (!state || device == RoboInputDeviceIdNone) {
        return 0;
    }
    for (index = 0u; index < state->heldCount; index++) {
        if (state->held[index].device == device) {
            return 1;
        }
    }
    return 0;
}

Result InputKeyboardStateHeldAt(const InputKeyboardState *state,
                                uint32_t index,
                                InputKeyboardHeldKey *out,
                                InputDiag *diag)
{
    if (!state || !out) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "a state and somewhere to put the key are both needed");
    }
    if (index >= state->heldCount) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboNotFound,
                             "no held key at that index");
    }
    *out = state->held[index];
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The derivation                                                      */
/* ------------------------------------------------------------------ */

InputModifiers InputKeyboardStateModifiers(const InputKeyboardState *state)
{
    InputModifiers modifiers = RoboInputModifiersNone;
    uint32_t index;

    if (!state) {
        return RoboInputModifiersNone;
    }
    for (index = 0u; index < state->heldCount; index++) {
        /* The union over the keys that are *down*: a shift released clears its bit because the key left the
         * table, not because anybody remembered to clear a flag. */
        modifiers |= InputPhysicalKeyModifier(state->held[index].physical);
    }
    /* A lock is on after its key is released, so the bits it owns come from the record rather than from the
     * held set -- which is why they are masked out of the derived union first. */
    modifiers &= ~RoboInputKeyboardLockMask;
    modifiers |= state->locks;
    return modifiers;
}

int InputKeyboardStateHasModifier(const InputKeyboardState *state, InputModifiers mask)
{
    return InputModifiersHas(InputKeyboardStateModifiers(state), mask);
}

InputModifiers InputKeyboardStateLocks(const InputKeyboardState *state)
{
    return state ? (InputModifiers)(state->locks & RoboInputKeyboardLockMask)
                 : RoboInputModifiersNone;
}

Result InputKeyboardStateSetRepeat(InputKeyboardState *state,
                                   const InputKeyboardRepeatConfig *config,
                                   InputDiag *diag)
{
    if (!state || !config) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "a state and a configuration are both needed");
    }
    state->repeat = *config;
    if (!InputKeyboardRepeatConfigIsEnabled(config)) {
        /* Turning repeat off disarms whatever was pending: a caller that disables it and then calls
         * `InputKeyboardStateRepeat` must not receive a repeat from before the change. */
        state->repeatArmed = 0;
        state->repeatPhysical = RoboPhysicalKeyUnknown;
        state->repeatLogical = RoboLogicalKeyUnknown;
        state->repeatDueMs = 0u;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Apply                                                               */
/* ------------------------------------------------------------------ */

/** Arm the repeat for a key, from the moment @p timestampMs. */
static void KbArmRepeat(InputKeyboardState *state,
                        PhysicalKey physical,
                        LogicalKey logical,
                        InputDeviceId device,
                        uint64_t timestampMs)
{
    if (!InputKeyboardRepeatConfigIsEnabled(&state->repeat)) {
        return;
    }
    state->repeatArmed = 1;
    state->repeatPhysical = physical;
    state->repeatLogical = logical;
    state->repeatDevice = device;
    state->repeatDueMs = timestampMs + (uint64_t)state->repeat.delayMs;
}

static void KbDisarmRepeat(InputKeyboardState *state)
{
    state->repeatArmed = 0;
    state->repeatPhysical = RoboPhysicalKeyUnknown;
    state->repeatLogical = RoboLogicalKeyUnknown;
    state->repeatDevice = RoboInputDeviceIdNone;
    state->repeatDueMs = 0u;
}

/** The index of @p physical in the held table, or -1. */
static int32_t KbHeldIndex(const InputKeyboardState *state, PhysicalKey physical)
{
    uint32_t index;

    if (physical == RoboPhysicalKeyUnknown) {
        return -1;
    }
    for (index = 0u; index < state->heldCount; index++) {
        if (state->held[index].physical == physical) {
            return (int32_t)index;
        }
    }
    return -1;
}

static void KbRemoveHeld(InputKeyboardState *state, uint32_t index)
{
    uint32_t move;

    for (move = index; move + 1u < state->heldCount; move++) {
        state->held[move] = state->held[move + 1u];
    }
    state->heldCount--;
    state->held[state->heldCount].physical = RoboPhysicalKeyUnknown;
    state->held[state->heldCount].logical = RoboLogicalKeyUnknown;
    state->held[state->heldCount].device = RoboInputDeviceIdNone;
}

Result InputKeyboardStateApply(InputKeyboardState *state,
                               const InputEvent *event,
                               InputDiag *diag)
{
    const InputKeyboardData *keyboard;
    PhysicalKey physical;

    if (!state || !event) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "a state and an event are both needed");
    }
    if (!InputEventTypeIsKey(event->type)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "only a key event changes a keyboard state");
    }
    if (!InputKeyboardDataIsValid(&event->payload.keyboard) ||
        !InputKeyboardDataActionMatches(&event->payload.keyboard, (int)event->type) ||
        !InputKeyboardDataRepeatMatchesEvent(&event->payload.keyboard, (int)event->type)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "the event's keyboard payload is not coherent with its type");
    }
    keyboard = &event->payload.keyboard;
    physical = keyboard->physical;

    /* A repeat sample changes nothing about what is held: the key is already in the table and the driver of
     * the cadence is `InputKeyboardStateRepeat`, not the events it produced. */
    if (event->type == RoboInputEventKeyRepeat) {
        return RoboOk;
    }

    if (event->type == RoboInputEventKeyUp) {
        int32_t index = KbHeldIndex(state, physical);

        if (index >= 0) {
            KbRemoveHeld(state, (uint32_t)index);
        }
        /* The repeat is cancelled when it was *this* key's, and the comparison is on the physical identity
         * because that is what "the same key" means to a finger. */
        if (state->repeatArmed && state->repeatPhysical == physical) {
            KbDisarmRepeat(state);
        }
        return RoboOk;
    }

    /* A press. A lock key toggles its bit and is *also* recorded as held, because it is: a caller asking
     * whether caps lock is down while the key is down wants the answer to be yes. */
    if (InputPhysicalKeyIsModifier(physical)) {
        InputModifiers bit = (InputModifiers)(InputPhysicalKeyModifier(physical) &
                                              RoboInputKeyboardLockMask);

        if (bit != RoboInputModifiersNone) {
            state->locks ^= bit;
        }
    }

    if (KbHeldIndex(state, physical) < 0) {
        if (state->heldCount >= RoboInputKeyboardHeldMax) {
            /* Refused rather than dropped: a key that is held but not in the table is a key that will never
             * be *released* here, and therefore a modifier that stays down until a reset -- the failure
             * this record is shaped to prevent. Nothing has been changed at this point. */
            return RoboInputDiag(diag, RoboInputDomainEvent, RoboNotSupported,
                                 "the held-key table is full");
        }
        state->held[state->heldCount].physical = physical;
        state->held[state->heldCount].logical = keyboard->logical;
        state->held[state->heldCount].device = event->device.id;
        state->heldCount++;
    }

    /* Every press re-arms, including a repeat of the same press from a platform that resends: the deadline
     * the user perceives is "this long after the last press", and a resend is a press. */
    KbArmRepeat(state, physical, keyboard->logical, event->device.id, event->timestampMs);
    return RoboOk;
}

uint32_t InputKeyboardStateDisconnect(InputKeyboardState *state, InputDeviceId device)
{
    uint32_t released = 0u;
    uint32_t index;

    if (!state || device == RoboInputDeviceIdNone) {
        return 0u;
    }
    index = 0u;
    while (index < state->heldCount) {
        if (state->held[index].device == device) {
            KbRemoveHeld(state, index);
            released++;
            continue;
        }
        index++;
    }
    if (state->repeatArmed && state->repeatDevice == device) {
        KbDisarmRepeat(state);
    }
    return released;
}

/* ------------------------------------------------------------------ */
/* Repeat                                                              */
/* ------------------------------------------------------------------ */

int InputKeyboardStateRepeatPending(const InputKeyboardState *state, uint64_t nowMs)
{
    return state != NULL && state->repeatArmed && nowMs >= state->repeatDueMs;
}

Result InputKeyboardStateRepeat(InputKeyboardState *state,
                                uint64_t nowMs,
                                InputEvent *outEvent,
                                int *outHas,
                                InputDiag *diag)
{
    InputKeyboardData keyboard;

    if (!state || !outEvent || !outHas) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "a state, somewhere for the event and somewhere for the answer are needed");
    }
    *outHas = 0;
    if (!InputKeyboardStateRepeatPending(state, nowMs)) {
        return RoboOk;
    }

    /* The repeat carries the key and the **current** modifiers, and an empty text: what the key *means* is
     * the layout's answer (13.2d), and a state that remembered the text of the original press would decide a
     * character a second time -- wrongly, the moment a modifier changed while the key was held. */
    InputKeyboardDataInit(&keyboard);
    if (InputKeyboardDataSet(&keyboard, state->repeatPhysical, state->repeatLogical,
                                     RoboKeyActionDown, 1, NULL) != RoboOk) {
        KbDisarmRepeat(state);
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboError,
                             "the armed repeat could not be built; it has been disarmed");
    }
    InputEventInit(outEvent);
    if (InputEventSetKeyRepeat(outEvent, RoboInputSourceKeyboard, &keyboard, diag) != RoboOk) {
        return diag ? diag->code : RoboError;
    }
    InputEventSetTimestamp(outEvent, nowMs);
    InputEventSetModifiers(outEvent, InputKeyboardStateModifiers(state), diag);
    outEvent->device.id = state->repeatDevice;

    /* One repeat per call, and the next deadline is measured from **now** rather than from the deadline it
     * missed, so a caller that was away for a second catches up by calling until `outHas` is 0 rather than
     * receiving a burst of events that claim to have happened in the past. */
    if (state->repeat.intervalMs == 0u) {
        KbDisarmRepeat(state);
    } else {
        state->repeatDueMs = nowMs + (uint64_t)state->repeat.intervalMs;
    }
    *outHas = 1;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Dump and comparison                                                 */
/* ------------------------------------------------------------------ */

Result InputKeyboardStateWrite(const InputKeyboardState *state,
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
        RiWriterText(&text, "keyboard <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "keyboard held=");
    RiWriterU64(&text, (uint64_t)state->heldCount);
    {
        char modifiers[96];
        size_t modifiersLength = 0u;

        if (InputModifiersWrite(InputKeyboardStateModifiers(state), modifiers,
                                sizeof(modifiers), &modifiersLength) != RoboOk) {
            RiTextCopy(modifiers, sizeof(modifiers), "?");
        }
        RiWriterFieldText(&text, " modifiers", modifiers);
        if (InputModifiersWrite(state->locks, modifiers, sizeof(modifiers),
                                &modifiersLength) == RoboOk) {
            RiWriterFieldText(&text, " locks", modifiers);
        } else {
            RiWriterText(&text, " locks=?");
        }
    }
    if (state->repeatArmed) {
        RiWriterText(&text, " repeat=armed due=");
        RiWriterU64(&text, state->repeatDueMs);
    } else {
        RiWriterText(&text, " repeat=idle");
    }
    RiWriterChar(&text, '\n');

    for (index = 0u; index < state->heldCount; index++) {
        RiWriterText(&text, "held ");
        RiWriterText(&text, PhysicalKeyName(state->held[index].physical));
        RiWriterFieldU64(&text, " device", (uint64_t)state->held[index].device);
        RiWriterChar(&text, '\n');
    }
    return RiWriterFinish(&text, outLength, diag);
}

int InputKeyboardStateEqual(const InputKeyboardState *a, const InputKeyboardState *b)
{
    uint32_t index;

    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    if (a->heldCount != b->heldCount || a->locks != b->locks) {
        return 0;
    }
    if (a->repeatArmed != b->repeatArmed) {
        return 0;
    }
    /* The deadline is deliberately not compared: two states that agree about what is held, what is locked
     * and which key will repeat are the same state, and the exact millisecond the next repeat lands on is a
     * detail of when the caller asked (this header's comment). */
    if (a->repeatArmed &&
        (a->repeatPhysical != b->repeatPhysical || a->repeatLogical != b->repeatLogical ||
         a->repeatDevice != b->repeatDevice)) {
        return 0;
    }
    if (a->repeat.delayMs != b->repeat.delayMs || a->repeat.intervalMs != b->repeat.intervalMs) {
        return 0;
    }
    for (index = 0u; index < a->heldCount; index++) {
        if (a->held[index].physical != b->held[index].physical ||
            a->held[index].logical != b->held[index].logical ||
            a->held[index].device != b->held[index].device) {
            return 0;
        }
    }
    return 1;
}
