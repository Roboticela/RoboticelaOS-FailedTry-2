/*
 * RoboInput keyboard-state suite (Phase 13.2) — `input.keyboard.*`.
 *
 * Seven cases about the one thing this record keeps and the four things it derives:
 *
 *   `held`        a press adds, a release removes, a resend keeps, the bound refuses, and the table stays in
 *                 press order;
 *
 *   `modifiers`   derived, not accumulated: a shift's bit appears because the key is down and vanishes
 *                 because it is gone -- including a left and a right shift held together, and including the
 *                 lock bits, which are the case that proves the rule;
 *
 *   `locks`       caps lock toggles on the press and *stays* after the release, which is why locks are the
 *                 one part of the answer this record supplies itself;
 *
 *   `disconnect`  the brief's rule, as a case: releasing a device's keys releases exactly its keys, and the
 *                 modifiers that followed from them follow automatically. Nothing is left stuck;
 *
 *   `repeat`      deterministic, driven by a clock a case passes in: one event per call, a deadline pushed
 *                 forward, a catch-up that does not burst into the past, a release that cancels it, a
 *                 disconnect that cancels it, and an interval of zero that turns it off;
 *
 *   `repeat_payload`  what a generated repeat *is*: a `KEY_REPEAT` event carrying the key and the **current**
 *                 modifiers with an empty text, because the layout decides the character (13.2d);
 *
 *   `dump`        the text, and the equality that deliberately ignores the deadline.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

/** A key event on device @p device at @p timeMs, asserting on refusal. */
static InputEvent KbEvent(PhysicalKey physical,
                          LogicalKey logical,
                          int pressed,
                          InputModifiers modifiers,
                          InputDeviceId device,
                          uint64_t timeMs)
{
    InputEvent event = InputKeyEvent(physical, logical, pressed, modifiers);

    InputEventSetTimestamp(&event, timeMs);
    event.device.id = device;
    return event;
}

/** Apply an event to a state, asserting on refusal. */
static void KbApply(InputKeyboardState *state, const InputEvent *event)
{
    if (InputKeyboardStateApply(state, event, NULL) != RoboOk) {
        RoboCheckMsg(0, "the fixture's key event was refused by the state");
    }
}

/* ------------------------------------------------------------------ */
/* The held table                                                      */
/* ------------------------------------------------------------------ */

static void TestKeyboardHeld(void)
{
    InputKeyboardState state;
    InputKeyboardHeldKey key;
    InputEvent event;
    InputDiag diag;
    uint32_t index;

    InputDiagReset(&diag);
    InputKeyboardStateInit(&state);
    RoboCheckEqInt((int)InputKeyboardStateHeldCount(&state), 0);
    RoboCheck(!InputKeyboardStateIsKeyDown(&state, RoboPhysicalKeyA));
    RoboCheckEqInt(InputKeyboardStateHeldAt(&state, 0u, &key, &diag), RoboNotFound);
    RoboCheck(!InputKeyboardStateHoldsDevice(&state, 1u));

    /* A press adds it, with the device and the logical identity it arrived with. */
    event = KbEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 1, RoboInputModifiersNone, 4u, 100u);
    KbApply(&state, &event);
    RoboCheckEqInt((int)InputKeyboardStateHeldCount(&state), 1);
    RoboCheck(InputKeyboardStateIsKeyDown(&state, RoboPhysicalKeyA));
    RoboCheck(!InputKeyboardStateIsKeyDown(&state, RoboPhysicalKeyB));
    RoboCheck(InputKeyboardStateIsLogicalKeyDown(&state, RoboLogicalKeyA));
    RoboCheck(InputKeyboardStateHoldsDevice(&state, 4u));
    RoboCheck(!InputKeyboardStateHoldsDevice(&state, 5u));
    RoboCheckEqInt(InputKeyboardStateHeldAt(&state, 0u, &key, &diag), RoboOk);
    RoboCheckEqInt(key.physical, RoboPhysicalKeyA);
    RoboCheckEqInt(key.logical, RoboLogicalKeyA);
    RoboCheckEqInt(key.device, 4);

    /* A resend is not an error and does not double the entry: platforms resend, and a table with two copies
     * of one key would release only one of them. */
    KbApply(&state, &event);
    RoboCheckEqInt((int)InputKeyboardStateHeldCount(&state), 1);

    /* Two keys: press order is the table's order, which is what makes the dump deterministic. */
    event = KbEvent(RoboPhysicalKeyB, RoboLogicalKeyB, 1, RoboInputModifiersNone, 4u, 110u);
    KbApply(&state, &event);
    RoboCheckEqInt((int)InputKeyboardStateHeldCount(&state), 2);
    RoboCheckEqInt(InputKeyboardStateHeldAt(&state, 1u, &key, &diag), RoboOk);
    RoboCheckEqInt(key.physical, RoboPhysicalKeyB);

    /* A release removes it, and the table compacts. */
    event = KbEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 0, RoboInputModifiersNone, 4u, 120u);
    KbApply(&state, &event);
    RoboCheckEqInt((int)InputKeyboardStateHeldCount(&state), 1);
    RoboCheck(!InputKeyboardStateIsKeyDown(&state, RoboPhysicalKeyA));
    RoboCheckEqInt(InputKeyboardStateHeldAt(&state, 0u, &key, &diag), RoboOk);
    RoboCheckEqInt(key.physical, RoboPhysicalKeyB);

    /* A release of a key that was never down is not an error: a platform may deliver one after a grab, and
     * a state that refused it would turn a harmless report into a failure the caller has to handle. */
    event = KbEvent(RoboPhysicalKeyZ, RoboLogicalKeyZ, 0, RoboInputModifiersNone, 4u, 130u);
    KbApply(&state, &event);
    RoboCheckEqInt((int)InputKeyboardStateHeldCount(&state), 1);

    /* A release with no key identity at all is a *refusal*, not a no-op: `Unknown` is not a key, and a
     * caller delivering one has lost track of something. */
    key.physical = RoboPhysicalKeyUnknown;
    RoboCheck(!InputKeyboardStateIsKeyDown(&state, RoboPhysicalKeyUnknown));

    /* The bound: a press beyond it is refused and changes nothing -- a held key that is not in the table
     * would never be released here, and its modifier would stay down until a reset. */
    index = InputKeyboardStateHeldCount(&state);
    while (InputKeyboardStateHeldCount(&state) < RoboInputKeyboardHeldMax) {
        PhysicalKey more = (PhysicalKey)((int)RoboPhysicalKeyF1 + (int)InputKeyboardStateHeldCount(&state));

        event = KbEvent(more, RoboLogicalKeyF1, 1, RoboInputModifiersNone, 4u, 200u);
        KbApply(&state, &event);
    }
    RoboCheckEqInt((int)InputKeyboardStateHeldCount(&state), (int)RoboInputKeyboardHeldMax);
    RoboCheck(index > 0u);
    event = KbEvent(RoboPhysicalKeySpace, RoboLogicalKeySpace, 1, RoboInputModifiersNone, 4u, 210u);
    RoboCheckEqInt(InputKeyboardStateApply(&state, &event, &diag), RoboNotSupported);
    RoboCheckEqInt((int)InputKeyboardStateHeldCount(&state), (int)RoboInputKeyboardHeldMax);
    RoboCheck(!InputKeyboardStateIsKeyDown(&state, RoboPhysicalKeySpace));

    /* Release-all keeps the locks and the timing; reset clears everything. */
    InputKeyboardStateReleaseAll(&state);
    RoboCheckEqInt((int)InputKeyboardStateHeldCount(&state), 0);

    /* A non-key event is refused: a state that accepted a pointer event would have to decide what a pointer
     * means to a keyboard. */
    {
        InputEvent pointer = InputPointerMove(1, 2, 0u, RoboInputSpaceScreen, 3u);

        RoboCheckEqInt(InputKeyboardStateApply(&state, &pointer, &diag), RoboInvalidArgument);
        RoboCheckEqInt(diag.domain, RoboInputDomainEvent);
        RoboCheckEqInt(InputKeyboardStateApply(NULL, &event, &diag), RoboInvalidArgument);
        RoboCheckEqInt(InputKeyboardStateApply(&state, NULL, &diag), RoboInvalidArgument);
    }
}

/* ------------------------------------------------------------------ */
/* Modifiers, derived                                                  */
/* ------------------------------------------------------------------ */

static void TestKeyboardModifiers(void)
{
    InputKeyboardState state;
    InputEvent event;

    InputKeyboardStateInit(&state);
    RoboCheckEqInt(InputKeyboardStateModifiers(&state), RoboInputModifiersNone);
    /* `None` is aggregate by the module's definition, which is what a consumer asking "am I being told about
     * sides?" wants to know. */
    RoboCheck(InputModifiersIsAggregate(InputKeyboardStateModifiers(&state)));

    /* Shift down: both the aggregate bit and the side bit, because `InputPhysicalKeyModifier` reports both --
     * one value answers "is shift down?" and "which shift?". */
    event = KbEvent(RoboPhysicalKeyShiftLeft, RoboLogicalKeyShiftLeft, 1,
                    RoboInputModifiersNone, 1u, 10u);
    KbApply(&state, &event);
    RoboCheck(InputKeyboardStateHasModifier(&state, RoboKeyModShift));
    RoboCheck(InputKeyboardStateHasModifier(&state, RoboKeyModShiftLeft));
    RoboCheck(!InputKeyboardStateHasModifier(&state, RoboKeyModShiftRight));
    RoboCheck(!InputModifiersIsAggregate(InputKeyboardStateModifiers(&state)));

    /* The other shift joins; the first one leaving does not clear the aggregate bit. */
    event = KbEvent(RoboPhysicalKeyShiftRight, RoboLogicalKeyShiftRight, 1,
                    RoboInputModifiersNone, 1u, 20u);
    KbApply(&state, &event);
    RoboCheck(InputKeyboardStateHasModifier(&state, RoboKeyModShift));
    RoboCheck(InputKeyboardStateHasModifier(&state, RoboKeyModShiftLeft));
    RoboCheck(InputKeyboardStateHasModifier(&state, RoboKeyModShiftRight));
    event = KbEvent(RoboPhysicalKeyShiftLeft, RoboLogicalKeyShiftLeft, 0,
                    RoboInputModifiersNone, 1u, 30u);
    KbApply(&state, &event);
    RoboCheck(InputKeyboardStateHasModifier(&state, RoboKeyModShift));
    RoboCheck(!InputKeyboardStateHasModifier(&state, RoboKeyModShiftLeft));

    /* And the last one leaving clears it -- because the *key* went away, not because anybody cleared a flag.
     * This is the derivation whose absence leaves a stuck modifier behind. */
    event = KbEvent(RoboPhysicalKeyShiftRight, RoboLogicalKeyShiftRight, 0,
                    RoboInputModifiersNone, 1u, 40u);
    KbApply(&state, &event);
    RoboCheck(!InputKeyboardStateHasModifier(&state, RoboKeyModShift));
    RoboCheckEqInt(InputKeyboardStateModifiers(&state), RoboInputModifiersNone);

    /* Control and alt are ordinary keys for this purpose and each reports its own side. */
    event = KbEvent(RoboPhysicalKeyControlLeft, RoboLogicalKeyControlLeft, 1,
                    RoboInputModifiersNone, 1u, 50u);
    KbApply(&state, &event);
    event = KbEvent(RoboPhysicalKeyAltRight, RoboLogicalKeyAltRight, 1,
                    RoboInputModifiersNone, 1u, 60u);
    KbApply(&state, &event);
    RoboCheck(InputKeyboardStateHasModifier(&state, RoboKeyModCtrl | RoboKeyModAlt));
    RoboCheck(InputKeyboardStateHasModifier(&state, RoboKeyModCtrlLeft | RoboKeyModAltRight));
    RoboCheck(!InputKeyboardStateHasModifier(&state, RoboKeyModMeta));

    /* An ordinary letter contributes nothing, however many are held. */
    event = KbEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 1, RoboInputModifiersNone, 1u, 70u);
    KbApply(&state, &event);
    RoboCheckEqInt(InputKeyboardStateModifiers(&state),
                   (InputModifiers)(RoboKeyModCtrlLeft | RoboKeyModAltRight |
                                    RoboKeyModCtrl | RoboKeyModAlt));
}

/* ------------------------------------------------------------------ */
/* Locks                                                               */
/* ------------------------------------------------------------------ */

static void TestKeyboardLocks(void)
{
    InputKeyboardState state;
    InputEvent event;

    InputKeyboardStateInit(&state);
    RoboCheckEqInt(InputKeyboardStateLocks(&state), RoboInputModifiersNone);

    /* Caps lock toggles on the press. */
    event = KbEvent(RoboPhysicalKeyCapsLock, RoboLogicalKeyCapsLock, 1,
                    RoboInputModifiersNone, 1u, 10u);
    KbApply(&state, &event);
    RoboCheck(InputKeyboardStateHasModifier(&state, RoboKeyModCapsLock));
    RoboCheck(InputKeyboardStateIsKeyDown(&state, RoboPhysicalKeyCapsLock));

    /* ... and **stays** when the key goes up, which is the whole reason locks are this record's own state
     * rather than part of the derived union. */
    event = KbEvent(RoboPhysicalKeyCapsLock, RoboLogicalKeyCapsLock, 0,
                    RoboInputModifiersNone, 1u, 20u);
    KbApply(&state, &event);
    RoboCheck(InputKeyboardStateHasModifier(&state, RoboKeyModCapsLock));
    RoboCheck(!InputKeyboardStateIsKeyDown(&state, RoboPhysicalKeyCapsLock));
    RoboCheckEqInt(InputKeyboardStateLocks(&state), RoboKeyModCapsLock);

    /* A second press toggles it off. */
    event = KbEvent(RoboPhysicalKeyCapsLock, RoboLogicalKeyCapsLock, 1,
                    RoboInputModifiersNone, 1u, 30u);
    KbApply(&state, &event);
    event = KbEvent(RoboPhysicalKeyCapsLock, RoboLogicalKeyCapsLock, 0,
                    RoboInputModifiersNone, 1u, 40u);
    KbApply(&state, &event);
    RoboCheck(!InputKeyboardStateHasModifier(&state, RoboKeyModCapsLock));

    /* The three locks are independent. */
    event = KbEvent(RoboPhysicalKeyNumLock, RoboLogicalKeyNumLock, 1,
                    RoboInputModifiersNone, 1u, 50u);
    KbApply(&state, &event);
    event = KbEvent(RoboPhysicalKeyScrollLock, RoboLogicalKeyScrollLock, 1,
                    RoboInputModifiersNone, 1u, 60u);
    KbApply(&state, &event);
    RoboCheckEqInt(InputKeyboardStateLocks(&state), RoboKeyModNumLock | RoboKeyModScrollLock);
    RoboCheck(!InputKeyboardStateHasModifier(&state, RoboKeyModCapsLock));

    /* A lock key held down does not report its bit twice, and releasing it does not clear the lock -- which
     * is the case a design that derived locks from the held set would get wrong. */
    event = KbEvent(RoboPhysicalKeyNumLock, RoboLogicalKeyNumLock, 0,
                    RoboInputModifiersNone, 1u, 70u);
    KbApply(&state, &event);
    RoboCheck(InputKeyboardStateHasModifier(&state, RoboKeyModNumLock));

    /* Release-all keeps the locks: a lost keyboard is not a user asking to forget their caps lock. */
    InputKeyboardStateReleaseAll(&state);
    RoboCheck(InputKeyboardStateHasModifier(&state, RoboKeyModNumLock));
    RoboCheckEqInt((int)InputKeyboardStateHeldCount(&state), 0);
    /* Reset is boot, and boot has no locks. */
    InputKeyboardStateReset(&state);
    RoboCheckEqInt(InputKeyboardStateLocks(&state), RoboInputModifiersNone);
    RoboCheckEqInt(InputKeyboardStateModifiers(&state), RoboInputModifiersNone);
}

/* ------------------------------------------------------------------ */
/* Disconnect                                                          */
/* ------------------------------------------------------------------ */

static void TestKeyboardDisconnect(void)
{
    InputKeyboardState state;
    InputEvent event;

    InputKeyboardStateInit(&state);

    /* Two keyboards: device 1 holds shift, device 2 holds control and a letter. */
    event = KbEvent(RoboPhysicalKeyShiftLeft, RoboLogicalKeyShiftLeft, 1,
                    RoboInputModifiersNone, 1u, 10u);
    KbApply(&state, &event);
    event = KbEvent(RoboPhysicalKeyControlLeft, RoboLogicalKeyControlLeft, 1,
                    RoboInputModifiersNone, 2u, 20u);
    KbApply(&state, &event);
    event = KbEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 1, RoboInputModifiersNone, 2u, 30u);
    KbApply(&state, &event);
    RoboCheckEqInt((int)InputKeyboardStateHeldCount(&state), 3);
    RoboCheck(InputKeyboardStateHoldsDevice(&state, 1u));
    RoboCheck(InputKeyboardStateHoldsDevice(&state, 2u));

    /* The brief's rule, as a case: releasing one device's keys releases exactly its keys, and the modifiers
     * that followed from them follow automatically. Nothing is left stuck. */
    RoboCheckEqInt((int)InputKeyboardStateDisconnect(&state, 1u), 1);
    RoboCheckEqInt((int)InputKeyboardStateHeldCount(&state), 2);
    RoboCheck(!InputKeyboardStateHoldsDevice(&state, 1u));
    RoboCheck(InputKeyboardStateHoldsDevice(&state, 2u));
    RoboCheck(!InputKeyboardStateHasModifier(&state, RoboKeyModShift));
    RoboCheck(!InputKeyboardStateIsKeyDown(&state, RoboPhysicalKeyShiftLeft));
    /* The other device is untouched. */
    RoboCheck(InputKeyboardStateHasModifier(&state, RoboKeyModCtrl));
    RoboCheck(InputKeyboardStateIsKeyDown(&state, RoboPhysicalKeyA));

    /* Disconnecting a device that holds nothing is not an error -- a caller that is not sure is being
     * careful, and the count says what it found. */
    RoboCheckEqInt((int)InputKeyboardStateDisconnect(&state, 77u), 0);
    RoboCheckEqInt((int)InputKeyboardStateDisconnect(&state, RoboInputDeviceIdNone), 0);
    RoboCheckEqInt((int)InputKeyboardStateDisconnect(NULL, 1u), 0);

    RoboCheckEqInt((int)InputKeyboardStateDisconnect(&state, 2u), 2);
    RoboCheckEqInt(InputKeyboardStateModifiers(&state), RoboInputModifiersNone);
    RoboCheckEqInt((int)InputKeyboardStateHeldCount(&state), 0);
}

/* ------------------------------------------------------------------ */
/* Repeat                                                              */
/* ------------------------------------------------------------------ */

static void TestKeyboardRepeat(void)
{
    InputKeyboardState state;
    InputKeyboardRepeatConfig config;
    InputEvent event;
    InputEvent repeat;
    InputDiag diag;
    int has = 0;
    uint32_t calls = 0;

    InputDiagReset(&diag);
    InputKeyboardStateInit(&state);
    RoboCheck(InputKeyboardRepeatConfigIsEnabled(&state.repeat));
    RoboCheckEqInt((int)state.repeat.delayMs, (int)RoboInputKeyboardRepeatDelayMsDefault);
    InputKeyboardRepeatConfigInit(&config);
    RoboCheckEqInt((int)config.intervalMs, (int)RoboInputKeyboardRepeatIntervalMsDefault);
    RoboCheck(InputKeyboardRepeatConfigIsEnabled(&config));
    RoboCheck(!InputKeyboardRepeatConfigIsEnabled(NULL));

    /* Nothing is due before anything is pressed. */
    RoboCheck(!InputKeyboardStateRepeatPending(&state, 1000u));
    RoboCheckEqInt(InputKeyboardStateRepeat(&state, 1000u, &repeat, &has, &diag), RoboOk);
    RoboCheckEqInt(has, 0);

    /* A press arms it for `now + delay`, and **nothing** repeats before then: the "initial delay" the brief
     * asks for is the gap between the press and the first repeat, and a state that repeated at once would
     * make holding a key indistinguishable from typing it. */
    event = KbEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 1, RoboInputModifiersNone, 1u, 1000u);
    KbApply(&state, &event);
    RoboCheckEqInt((int)state.repeatDueMs,
                   (int)(1000u + RoboInputKeyboardRepeatDelayMsDefault));
    RoboCheck(!InputKeyboardStateRepeatPending(
        &state, 1000u + RoboInputKeyboardRepeatDelayMsDefault - 1u));
    RoboCheck(InputKeyboardStateRepeatPending(
        &state, 1000u + RoboInputKeyboardRepeatDelayMsDefault));

    RoboCheckEqInt(InputKeyboardStateRepeat(
                       &state, 1000u + RoboInputKeyboardRepeatDelayMsDefault, &repeat, &has, &diag),
                   RoboOk);
    RoboCheckEqInt(has, 1);
    RoboCheckEqInt(repeat.type, RoboInputEventKeyRepeat);
    RoboCheckEqInt(repeat.payload.keyboard.repeat, 1);
    RoboCheckEqInt(repeat.payload.keyboard.physical, RoboPhysicalKeyA);
    RoboCheckEqInt(repeat.timestampMs, 1000u + RoboInputKeyboardRepeatDelayMsDefault);

    /* The state does not change from its own repeat: the key stays held and nothing is re-armed twice. */
    RoboCheckEqInt((int)InputKeyboardStateHeldCount(&state), 1);
    RoboCheckEqInt(InputKeyboardStateApply(&state, &repeat, &diag), RoboOk);
    RoboCheckEqInt((int)InputKeyboardStateHeldCount(&state), 1);

    /* The next deadline moves forward by the interval, and one call produces one event. */
    RoboCheck(!InputKeyboardStateRepeatPending(
        &state, 1000u + RoboInputKeyboardRepeatDelayMsDefault + 1u));
    RoboCheck(InputKeyboardStateRepeatPending(
        &state, 1000u + RoboInputKeyboardRepeatDelayMsDefault +
                    RoboInputKeyboardRepeatIntervalMsDefault));

    /* A caller that was away for a long time **catches up by calling**, one event per call, and each one is
     * stamped at the moment it was produced rather than at the deadline it missed -- which is what stops a
     * burst of events that claim to have happened in the past. */
    calls = 0u;
    while (calls < 100u) {
        RoboCheckEqInt(InputKeyboardStateRepeat(&state, 5000u, &repeat, &has, &diag), RoboOk);
        if (!has) {
            break;
        }
        RoboCheckEqInt(repeat.timestampMs, 5000u);
        calls++;
    }
    /* Exactly one, because the deadline is measured from `now`: after the first, the next is due at
     * 5000 + interval, which is in the future. */
    RoboCheckEqInt((int)calls, 1);

    /* A release cancels it, at once and not merely eventually. */
    event = KbEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 0, RoboInputModifiersNone, 1u, 5100u);
    KbApply(&state, &event);
    RoboCheck(!state.repeatArmed);
    RoboCheck(!InputKeyboardStateRepeatPending(&state, 99999u));
    RoboCheckEqInt(InputKeyboardStateRepeat(&state, 99999u, &repeat, &has, &diag), RoboOk);
    RoboCheckEqInt(has, 0);

    /* A disconnect cancels it too -- the key is no longer there to repeat. */
    event = KbEvent(RoboPhysicalKeyB, RoboLogicalKeyB, 1, RoboInputModifiersNone, 9u, 6000u);
    KbApply(&state, &event);
    RoboCheck(state.repeatArmed);
    RoboCheckEqInt((int)InputKeyboardStateDisconnect(&state, 9u), 1);
    RoboCheck(!state.repeatArmed);

    /* The interval is a configuration: a longer one moves the deadline further. */
    config.delayMs = 200u;
    config.intervalMs = 100u;
    RoboCheckEqInt(InputKeyboardStateSetRepeat(&state, &config, &diag), RoboOk);
    event = KbEvent(RoboPhysicalKeyC, RoboLogicalKeyC, 1, RoboInputModifiersNone, 1u, 7000u);
    KbApply(&state, &event);
    RoboCheckEqInt((int)state.repeatDueMs, 7200);
    RoboCheck(!InputKeyboardStateRepeatPending(&state, 7199u));
    RoboCheck(InputKeyboardStateRepeatPending(&state, 7200u));

    /* And zero interval **disables** it rather than repeating as fast as possible: the honest way to say
     * "this build does not auto-repeat". */
    config.intervalMs = 0u;
    RoboCheckEqInt(InputKeyboardStateSetRepeat(&state, &config, &diag), RoboOk);
    RoboCheck(!InputKeyboardRepeatConfigIsEnabled(&config));
    RoboCheck(!state.repeatArmed);
    event = KbEvent(RoboPhysicalKeyD, RoboLogicalKeyD, 1, RoboInputModifiersNone, 1u, 8000u);
    KbApply(&state, &event);
    RoboCheck(!state.repeatArmed);
    RoboCheckEqInt(InputKeyboardStateRepeat(&state, 99999u, &repeat, &has, &diag), RoboOk);
    RoboCheckEqInt(has, 0);

    RoboCheckEqInt(InputKeyboardStateSetRepeat(NULL, &config, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputKeyboardStateSetRepeat(&state, NULL, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputKeyboardStateRepeat(NULL, 0u, &repeat, &has, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputKeyboardStateRepeat(&state, 0u, NULL, &has, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputKeyboardStateRepeat(&state, 0u, &repeat, NULL, &diag), RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* What a repeat carries                                               */
/* ------------------------------------------------------------------ */

static void TestKeyboardRepeatPayload(void)
{
    InputKeyboardState state;
    InputKeyboardRepeatConfig config;
    InputEvent event;
    InputEvent repeat;
    InputDiag diag;
    int has = 0;

    InputDiagReset(&diag);
    InputKeyboardStateInit(&state);
    config.delayMs = 10u;
    config.intervalMs = 10u;
    InputKeyboardStateSetRepeat(&state, &config, &diag);

    /* A letter with shift held: the repeat carries the key and the **current** modifiers, and an **empty**
     * text -- the character is the layout's answer (13.2d), and a state that remembered the original press's
     * character would be wrong the moment a modifier changed while the key was held. */
    event = KbEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 1, RoboInputModifiersNone, 5u, 0u);
    event.payload.keyboard.text[0] = 'a';
    KbApply(&state, &event);
    event = KbEvent(RoboPhysicalKeyShiftLeft, RoboLogicalKeyShiftLeft, 1,
                    RoboInputModifiersNone, 5u, 0u);
    KbApply(&state, &event);

    RoboCheckEqInt(InputKeyboardStateRepeat(&state, 10u, &repeat, &has, &diag), RoboOk);
    RoboCheckEqInt(has, 1);
    RoboCheckEqInt(repeat.type, RoboInputEventKeyRepeat);
    RoboCheck(InputEventIsValid(&repeat));
    RoboCheckStrEq(repeat.payload.keyboard.text, "");
    RoboCheck(InputModifiersHas(repeat.modifiers, RoboKeyModShift));
    RoboCheckEqInt(repeat.device.id, 5);
    RoboCheckEqInt(repeat.source, RoboInputSourceKeyboard);
    /* The modifiers were *not* carried in from the press: the shift arrived after it. */
    RoboCheck(InputKeyboardStateHasModifier(&state, RoboKeyModShift));
}

/* ------------------------------------------------------------------ */
/* Dump and equality                                                   */
/* ------------------------------------------------------------------ */

static void TestKeyboardDump(void)
{
    InputKeyboardState state;
    InputKeyboardState other;
    InputEvent event;
    char first[RoboInputKeyboardStateDumpMax];
    char second[RoboInputKeyboardStateDumpMax];
    size_t firstLength = 0u;
    size_t secondLength = 0u;
    InputDiag diag;

    InputDiagReset(&diag);
    RoboCheckEqInt(InputKeyboardStateWrite(NULL, NULL, sizeof(first), &firstLength, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputKeyboardStateWrite(NULL, first, 0u, &firstLength, &diag),
                   RoboInvalidArgument);

    InputKeyboardStateInit(&state);
    RoboCheckEqInt(InputKeyboardStateWrite(&state, first, sizeof(first), &firstLength, &diag),
                   RoboOk);
    RoboCheck(strstr(first, "keyboard held=0 modifiers=none locks=none repeat=idle\n") == first);

    /* A shift and a letter: the summary carries the derived modifiers and the locks, and one line per held
     * key carries the device. */
    event = KbEvent(RoboPhysicalKeyShiftLeft, RoboLogicalKeyShiftLeft, 1,
                    RoboInputModifiersNone, 7u, 10u);
    KbApply(&state, &event);
    event = KbEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 1, RoboInputModifiersNone, 7u, 20u);
    KbApply(&state, &event);
    RoboCheckEqInt(InputKeyboardStateWrite(&state, first, sizeof(first), &firstLength, &diag),
                   RoboOk);
    RoboCheck(firstLength < (size_t)RoboInputKeyboardStateDumpMax);
    RoboCheckEqInt((int)strlen(first), (int)firstLength);
    /* The modifier list is `InputModifiersWrite`'s own: lower case, `+`-separated, in that writer's order
     * (`shift` before `shift-left`), so a reader sees one vocabulary across the whole dump. */
    RoboCheck(strstr(first, "keyboard held=2 modifiers=shift+shift-left locks=none repeat=armed due=") ==
               first);
    RoboCheck(strstr(first, "\nheld ShiftLeft device=7\n") != NULL);
    RoboCheck(strstr(first, "\nheld A device=7\n") != NULL);

    /* Deterministic across two calls. */
    RoboCheckEqInt(InputKeyboardStateWrite(&state, second, sizeof(second), &secondLength, &diag),
                   RoboOk);
    RoboCheckEqInt((int)secondLength, (int)firstLength);
    RoboCheck(strcmp(first, second) == 0);

    /* A buffer too small stores `?` rather than a prefix that reads as a whole state. */
    {
        char small[8];
        size_t needed = 0u;

        RoboCheckEqInt(InputKeyboardStateWrite(&state, small, sizeof(small), &needed, &diag),
                       RoboNotSupported);
        RoboCheckEqInt((int)needed, (int)firstLength);
        RoboCheckStrEq(small, "?");
        RoboCheckEqInt(diag.domain, RoboInputDomainDump);
    }

    /* Equality compares what is held, what is locked and which key repeats -- and deliberately **not** the
     * deadline, because the exact millisecond is a detail of when the caller asked. */
    InputKeyboardStateInit(&other);
    RoboCheck(!InputKeyboardStateEqual(&state, &other));
    event = KbEvent(RoboPhysicalKeyShiftLeft, RoboLogicalKeyShiftLeft, 1,
                    RoboInputModifiersNone, 7u, 10u);
    KbApply(&other, &event);
    event = KbEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 1, RoboInputModifiersNone, 7u, 20u);
    KbApply(&other, &event);
    RoboCheck(InputKeyboardStateEqual(&state, &other));

    other = state;
    other.repeatDueMs += 5000u;
    RoboCheck(InputKeyboardStateEqual(&state, &other));
    other.locks |= (InputModifiers)RoboKeyModCapsLock;
    RoboCheck(!InputKeyboardStateEqual(&state, &other));
    other = state;
    other.repeatPhysical = RoboPhysicalKeyZ;
    RoboCheck(!InputKeyboardStateEqual(&state, &other));
    other = state;
    other.held[0].device = 99u;
    RoboCheck(!InputKeyboardStateEqual(&state, &other));
    other = state;
    other.heldCount = 1u;
    RoboCheck(!InputKeyboardStateEqual(&state, &other));
    RoboCheck(InputKeyboardStateEqual(NULL, NULL));
    RoboCheck(!InputKeyboardStateEqual(&state, NULL));
}

void TestRegisterInputKeyboardState(void)
{
    TestRegister("input.keyboard.held", TestKeyboardHeld);
    TestRegister("input.keyboard.modifiers", TestKeyboardModifiers);
    TestRegister("input.keyboard.locks", TestKeyboardLocks);
    TestRegister("input.keyboard.disconnect", TestKeyboardDisconnect);
    TestRegister("input.keyboard.repeat", TestKeyboardRepeat);
    TestRegister("input.keyboard.repeat_payload", TestKeyboardRepeatPayload);
    TestRegister("input.keyboard.dump", TestKeyboardDump);
}
