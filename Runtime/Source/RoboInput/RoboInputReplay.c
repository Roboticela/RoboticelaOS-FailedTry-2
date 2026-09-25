/*
 * Recording, replay and injection (Phase 13.49--13.51) — see `RoboInputReplay.h`.
 *
 * The file is four pieces:
 *
 *   **the ranks**     the vocabulary, the comparison and the table that says what each event type needs;
 *   **the recording** a bounded by-value stream whose privilege only ever *rises*;
 *   **the replay**    one event at a time, gated on the actor matching the recording's rank;
 *   **the injector**  the door a synthetic event goes through, with its refusals counted and named.
 *
 * The table is the security boundary, so it is written as one `switch` with the reasoning in the comments
 * rather than as a default that swallows what nobody thought about: the default is `SYSTEM`, which refuses
 * everything a lower rank asks for.
 */

#include "RoboInput/RoboInputReplay.h"

#include "RoboInputInternal.h"

#include <string.h>

/* ============================================================================================== *
 * Ranks
 * ============================================================================================== */

static const char *const RiPrivilegeNames[RoboInputInjectionPrivilegeCount] = {
    "none", "application", "accessibility", "automation", "system"
};

int InputInjectionPrivilegeIsKnown(InputInjectionPrivilege privilege)
{
    return (int)privilege >= 0 && (size_t)privilege < (size_t)RoboInputInjectionPrivilegeCount;
}

const char *InputInjectionPrivilegeName(InputInjectionPrivilege privilege)
{
    if (!InputInjectionPrivilegeIsKnown(privilege)) {
        return "unknown";
    }
    return RiPrivilegeNames[(size_t)privilege];
}

int InputInjectionPrivilegeRank(InputInjectionPrivilege privilege)
{
    if (!InputInjectionPrivilegeIsKnown(privilege)) {
        return -1;
    }
    return (int)privilege;
}

int InputInjectionPrivilegeAllows(InputInjectionPrivilege actor, InputInjectionPrivilege required)
{
    int actorRank = InputInjectionPrivilegeRank(actor);
    int requiredRank = InputInjectionPrivilegeRank(required);

    if (actorRank < 0 || requiredRank < 0) {
        return 0;
    }
    return actorRank >= requiredRank ? 1 : 0;
}

InputInjectionPrivilege InputInjectionPrivilegeRequiredFor(InputEventType type)
{
    switch (type) {
    /* What an application may synthesize on its own surface: a click, a drag, a wheel, a tap, a stylus, a
     * controller. Confined by the target it names, which is the caller's job to keep honest -- the rank says
     * "this is a thing an application is allowed to make up", not "it may aim it at another window". */
    case RoboInputEventPointerMove:
    case RoboInputEventPointerDown:
    case RoboInputEventPointerUp:
    case RoboInputEventPointerCancel:
    case RoboInputEventPointerEnter:
    case RoboInputEventPointerLeave:
    case RoboInputEventWheel:
    case RoboInputEventTouchBegin:
    case RoboInputEventTouchMove:
    case RoboInputEventTouchEnd:
    case RoboInputEventTouchCancel:
    case RoboInputEventPenDown:
    case RoboInputEventPenMove:
    case RoboInputEventPenUp:
    case RoboInputEventPenCancel:
    case RoboInputEventGamepadButtonDown:
    case RoboInputEventGamepadButtonUp:
    case RoboInputEventGamepadAxisChanged:
        return RoboInputPrivilegeApplication;

    /*
     * What reaches whatever holds focus -- which may be another application's editor. An application that
     * could fabricate a keystroke could type into somebody else's field, so the rank that may is the one
     * assistive tools hold.
     */
    case RoboInputEventKeyDown:
    case RoboInputEventKeyUp:
    case RoboInputEventKeyRepeat:
    case RoboInputEventTextInput:
    case RoboInputEventCompositionStart:
    case RoboInputEventCompositionUpdate:
    case RoboInputEventCompositionCommit:
    case RoboInputEventCompositionCancel:
        return RoboInputPrivilegeAccessibility;

    /*
     * The system's own transitions: focus moving, a window activating, a device arriving or leaving. An
     * application that could fabricate them could take focus from anybody, or release the state of a device
     * nobody touched.
     */
    case RoboInputEventFocusIn:
    case RoboInputEventFocusOut:
    case RoboInputEventWindowActivate:
    case RoboInputEventWindowDeactivate:
    case RoboInputEventDeviceConnected:
    case RoboInputEventDeviceDisconnected:
        return RoboInputPrivilegeSystem;

    /* Including `NONE`: an event this layer cannot name is not one it will let a caller inject. */
    case RoboInputEventNone:
    default:
        return RoboInputPrivilegeSystem;
    }
}

/* ============================================================================================== *
 * A recording
 * ============================================================================================== */

void InputRecordingInit(InputRecording *recording)
{
    if (!recording) {
        return;
    }
    memset(recording, 0, sizeof(*recording));
    recording->privilege = RoboInputPrivilegeNone;
    recording->serial = 1u;
}

Result InputRecordingRecord(InputRecording *recording,
                            const InputEvent *event,
                            uint64_t timestampMs,
                            InputInjectionPrivilege privilege,
                            InputDiag *diag)
{
    if (!recording) {
        return RoboInputDiag(diag, RoboInputDomainReplay, RoboInvalidArgument,
                             "a recording is required");
    }
    if (!event) {
        return RoboInputDiag(diag, RoboInputDomainReplay, RoboInvalidArgument,
                             "an event is required");
    }
    if (!InputInjectionPrivilegeIsKnown(privilege)) {
        return RoboInputDiag(diag, RoboInputDomainReplay, RoboInvalidArgument,
                             "a recording names a defined privilege");
    }
    /*
     * The rank **rises** and never falls: a stream that contained one system event stays system, because the
     * question a replay asks is "was anything in here more than I may do?" -- and the highest answer decides.
     */
    if (InputInjectionPrivilegeRank(privilege) > InputInjectionPrivilegeRank(recording->privilege)) {
        recording->privilege = privilege;
    }
    if (recording->count >= RoboInputRecordingMax) {
        /* Counted, never silent: the alternative is a recording of a long session that *looks* complete. */
        recording->dropped++;
        return RoboInputDiag(diag, RoboInputDomainReplay, RoboOutOfMemory,
                             "the recording is full");
    }

    recording->events[recording->count] = *event;
    recording->timestamps[recording->count] = timestampMs;
    if (recording->count == 0u) {
        recording->firstMs = timestampMs;
    }
    recording->lastMs = timestampMs;
    recording->count++;
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

uint32_t InputRecordingCount(const InputRecording *recording)
{
    return recording ? recording->count : 0u;
}

Result InputRecordingAt(const InputRecording *recording,
                        uint32_t index,
                        InputEvent *outEvent,
                        uint64_t *outTimestamp)
{
    if (!recording || index >= recording->count) {
        return RoboNotFound;
    }
    if (outEvent) {
        *outEvent = recording->events[index];
    }
    if (outTimestamp) {
        *outTimestamp = recording->timestamps[index];
    }
    return RoboOk;
}

Result InputRecordingWrite(const InputRecording *recording,
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
    if (!recording) {
        RiWriterText(&text, "recording <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "recording serial=");
    RiWriterU64(&text, (uint64_t)recording->serial);
    RiWriterFieldU64(&text, " events", (uint64_t)recording->count);
    RiWriterFieldU64(&text, " dropped", (uint64_t)recording->dropped);
    RiWriterFieldText(&text, " privilege", InputInjectionPrivilegeName(recording->privilege));
    RiWriterFieldU64(&text, " first", recording->count > 0u ? recording->firstMs : 0u);
    RiWriterFieldU64(&text, " last", recording->count > 0u ? recording->lastMs : 0u);
    RiWriterChar(&text, '\n');
    return RiWriterFinish(&text, outLength, diag);
}

/* ============================================================================================== *
 * A replay
 * ============================================================================================== */

void InputReplayInit(InputReplayState *state)
{
    if (!state) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->actor = RoboInputPrivilegeNone;
    state->finished = 1;
}

Result InputReplayBegin(InputReplayState *state,
                        const InputRecording *recording,
                        InputInjectionPrivilege actor,
                        InputDiag *diag)
{
    if (!state) {
        return RoboInputDiag(diag, RoboInputDomainReplay, RoboInvalidArgument,
                             "a replay state is required");
    }
    if (!recording) {
        return RoboInputDiag(diag, RoboInputDomainReplay, RoboInvalidArgument,
                             "a recording is required");
    }
    if (!InputInjectionPrivilegeIsKnown(actor)) {
        return RoboInputDiag(diag, RoboInputDomainReplay, RoboInvalidArgument,
                             "a replay names a defined privilege");
    }
    /*
     * The rule that makes a recording a capability: an actor may not replay a stream that came from a rank
     * above its own. A recording of device connections is the system's, and an application that found the
     * file may look at it and not press play.
     */
    if (!InputInjectionPrivilegeAllows(actor, recording->privilege)) {
        return RoboInputDiag(diag, RoboInputDomainReplay, RoboNotSupported,
                             "the actor ranks below the recording's privilege");
    }

    state->recording = recording;
    state->index = 0u;
    state->actor = actor;
    state->serial = recording->serial;
    state->delivered = 0u;
    state->finished = recording->count == 0u ? 1 : 0;
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

Result InputReplayNext(InputReplayState *state, InputEvent *outEvent, uint64_t *outTimestamp)
{
    if (!state || !state->recording) {
        return RoboInvalidArgument;
    }
    /* Cleared or re-recorded since the replay began: the indices a caller holds no longer mean what they
     * meant, and saying so is better than handing out somebody else's event. */
    if (state->recording->serial != state->serial || state->recording->count < state->delivered) {
        state->finished = 1;
        return RoboNotFound;
    }
    if (state->index >= state->recording->count) {
        state->finished = 1;
        return RoboNotFound;
    }
    if (outEvent) {
        *outEvent = state->recording->events[state->index];
    }
    if (outTimestamp) {
        *outTimestamp = state->recording->timestamps[state->index];
    }
    state->index++;
    state->delivered++;
    if (state->index >= state->recording->count) {
        state->finished = 1;
    }
    return RoboOk;
}

uint32_t InputReplayRemaining(const InputReplayState *state)
{
    if (!state || !state->recording) {
        return 0u;
    }
    if (state->index >= state->recording->count) {
        return 0u;
    }
    return state->recording->count - state->index;
}

Result InputReplayStateWrite(const InputReplayState *state,
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
    if (!state) {
        RiWriterText(&text, "replay <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "replay actor=");
    RiWriterText(&text, InputInjectionPrivilegeName(state->actor));
    RiWriterFieldText(&text, " privilege",
                      state->recording ? InputInjectionPrivilegeName(state->recording->privilege) : "none");
    RiWriterFieldU64(&text, " delivered", (uint64_t)state->delivered);
    RiWriterFieldU64(&text, " remaining", (uint64_t)InputReplayRemaining(state));
    RiWriterFieldU64(&text, " finished", (uint64_t)(state->finished ? 1u : 0u));
    RiWriterChar(&text, '\n');
    return RiWriterFinish(&text, outLength, diag);
}

/* ============================================================================================== *
 * Injection
 * ============================================================================================== */

Result InputInjectorInit(InputInjector *injector, InputInjectionPrivilege privilege, InputDiag *diag)
{
    if (!injector) {
        return RoboInputDiag(diag, RoboInputDomainReplay, RoboInvalidArgument,
                             "an injector is required");
    }
    if (!InputInjectionPrivilegeIsKnown(privilege)) {
        return RoboInputDiag(diag, RoboInputDomainReplay, RoboInvalidArgument,
                             "an injector names a defined privilege");
    }
    memset(injector, 0, sizeof(*injector));
    injector->privilege = privilege;
    injector->lastRefusal = "";
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

int InputInjectorMayInject(const InputInjector *injector, InputEventType type)
{
    if (!injector) {
        return 0;
    }
    return InputInjectionPrivilegeAllows(injector->privilege, InputInjectionPrivilegeRequiredFor(type));
}

Result InputInjectorCheck(const InputInjector *injector, const InputEvent *event)
{
    if (!injector || !event) {
        return RoboInvalidArgument;
    }
    /*
     * A **cleared record** is not an event anybody injects, whatever their rank: `NONE` is in the type
     * vocabulary because it is the value a cleared record holds, not because it is something a producer may
     * hand to the pipeline. Refusing it here -- rather than letting the validator catch it later -- is what
     * makes "not an event" and "an event you may not make" two different answers.
     */
    if (!InputEventTypeIsKnown(event->type) || event->type == RoboInputEventNone) {
        return RoboInvalidArgument;
    }
    return InputInjectorMayInject(injector, event->type) ? RoboOk : RoboNotSupported;
}

Result InputInjectorInject(InputInjector *injector, const InputEvent *event, InputDiag *diag)
{
    Result check;

    if (!injector) {
        return RoboInputDiag(diag, RoboInputDomainReplay, RoboInvalidArgument,
                             "an injector is required");
    }
    /*
     * A NULL event is the *caller's* defect rather than an attempt to inject something, so it is refused
     * without touching the counters: `refused` counts attempts this boundary turned down, and a bug in a
     * caller is a different fact from a privilege that was not enough.
     */
    if (!event) {
        return RoboInputDiag(diag, RoboInputDomainReplay, RoboInvalidArgument,
                             "an event is required");
    }
    check = InputInjectorCheck(injector, event);
    if (check == RoboInvalidArgument) {
        injector->refused++;
        injector->lastRefusal = "the event is not one this build can express";
        return RoboInputDiag(diag, RoboInputDomainReplay, RoboInvalidArgument,
                             "the event is not one this build can express");
    }
    if (check != RoboOk) {
        injector->refused++;
        injector->lastRefusal = "the injector's privilege is below what the event requires";
        return RoboInputDiag(diag, RoboInputDomainReplay, RoboNotSupported,
                             "the injector's privilege is below what the event requires");
    }

    /*
     * A last sanity check on the record itself: an event that is *not* a record this build may carry is not
     * something a synthetic producer may hand to the pipeline either. There is no shortcut for synthetic
     * input -- it is the same queue and the same routing, so it passes the same validation.
     */
    if (!InputEventIsValid(event)) {
        injector->refused++;
        injector->lastRefusal = "the event failed the model's own validation";
        return RoboInputDiag(diag, RoboInputDomainReplay, RoboInvalidArgument,
                             "the event failed the model's own validation");
    }

    injector->injected++;
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

Result InputInjectorWrite(const InputInjector *injector,
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
    if (!injector) {
        RiWriterText(&text, "injector <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "injector privilege=");
    RiWriterText(&text, InputInjectionPrivilegeName(injector->privilege));
    RiWriterFieldU64(&text, " injected", (uint64_t)injector->injected);
    RiWriterFieldU64(&text, " refused", (uint64_t)injector->refused);
    RiWriterFieldText(&text, " last", injector->lastRefusal ? injector->lastRefusal : "");
    RiWriterChar(&text, '\n');
    return RiWriterFinish(&text, outLength, diag);
}
