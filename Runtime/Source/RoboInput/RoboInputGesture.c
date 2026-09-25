/*
 * Gestures (Phase 13.23/13.24) — see `RoboInputGesture.h`.
 *
 * Four pieces: the sample (and the two builders that make one from what 13.14--13.21 already produced), the
 * event, the arena, and the one recognizer this build ships. Nothing here reads a device, allocates, or
 * reaches above 13.21: a gesture is downstream of normalization and consumes nothing else.
 */

#include "RoboInput/RoboInputGesture.h"

#include "RoboInputInternal.h"

#include <string.h>

/**
 * A square root without `math.h`, which the freestanding target does not have.
 *
 * Newton's method from a power-of-two seed: eight iterations put a float32 at full precision, and the
 * magnitude inputs this file sees (positions in a few thousand units) converge in fewer. A negative input has
 * no root and answers zero rather than a NaN, because a NaN spread would reach a threshold comparison and make
 * every gesture defer forever.
 */
static float RiGestureSqrt(float value)
{
    float guess;
    uint32_t index;

    if (!(value > 0.0f)) {
        return 0.0f;
    }
    guess = value;
    for (index = 0u; index < 16u; index++) {
        guess = 0.5f * (guess + value / guess);
    }
    return guess;
}

static const char *const RiGestureTypeNames[RoboInputGestureTypeCount] = {
    "none", "tap", "double-tap", "long-press", "drag", "pan", "pinch", "rotate"
};

int InputGestureTypeIsKnown(InputGestureType type)
{
    return (int)type >= 0 && (size_t)type < (size_t)RoboInputGestureTypeCount;
}

const char *InputGestureTypeName(InputGestureType type)
{
    if (!InputGestureTypeIsKnown(type)) {
        return "unknown";
    }
    return RiGestureTypeNames[(size_t)type];
}

int InputGestureTypeIsMultiTouch(InputGestureType type)
{
    return type == RoboInputGesturePan || type == RoboInputGesturePinch ||
           type == RoboInputGestureRotate;
}

static const char *const RiGesturePhaseNames[RoboInputGesturePhaseCount] = {
    "begin", "update", "end", "cancel"
};

int InputGesturePhaseIsKnown(InputGesturePhase phase)
{
    return (int)phase >= 0 && (size_t)phase < (size_t)RoboInputGesturePhaseCount;
}

const char *InputGesturePhaseName(InputGesturePhase phase)
{
    if (!InputGesturePhaseIsKnown(phase)) {
        return "unknown";
    }
    return RiGesturePhaseNames[(size_t)phase];
}

int InputGesturePhaseIsTerminal(InputGesturePhase phase)
{
    return phase == RoboInputGesturePhaseEnd || phase == RoboInputGesturePhaseCancel;
}

static const char *const RiGestureVerdictNames[RoboInputGestureVerdictCount] = {
    "reject", "accept", "defer"
};

int InputGestureVerdictIsKnown(InputGestureVerdict verdict)
{
    return (int)verdict >= 0 && (size_t)verdict < (size_t)RoboInputGestureVerdictCount;
}

const char *InputGestureVerdictName(InputGestureVerdict verdict)
{
    if (!InputGestureVerdictIsKnown(verdict)) {
        return "unknown";
    }
    return RiGestureVerdictNames[(size_t)verdict];
}

/* ============================================================================================== *
 * The sample
 * ============================================================================================== */

void InputGestureSampleInit(InputGestureSample *sample)
{
    if (!sample) {
        return;
    }
    memset(sample, 0, sizeof(*sample));
    sample->type = RoboInputEventNone;
    sample->space = RoboInputSpaceUnknown;
    sample->contactCount = 1u;
}

Result InputGestureSampleFromPointer(InputPointerData *data,
                                     InputEventType type,
                                     InputSpace space,
                                     InputDeviceId device,
                                     uint64_t timestampMs,
                                     InputGestureSample *out,
                                     InputDiag *diag)
{
    if (!data || !out) {
        return RoboInputDiag(diag, RoboInputDomainGesture, RoboInvalidArgument,
                             "a sample needs a payload and somewhere to put it");
    }
    InputGestureSampleInit(out);
    if (type == RoboInputEventNone || !InputEventTypeIsKnown(type)) {
        return RoboInputDiag(diag, RoboInputDomainGesture, RoboInvalidArgument,
                             "a sample comes from a defined event type");
    }

    out->pointer = data->pointer;
    out->type = type;
    out->position = data->position;
    out->centroid = data->position;
    out->hasPosition = 1;
    out->space = space;
    out->device = device;
    out->timestampMs = timestampMs;
    out->contactCount = 1u;
    out->cancelled = type == RoboInputEventPointerCancel ? 1 : 0;

    if (data->hasPrevious) {
        out->deltaX = data->position.x - data->previous.x;
        out->deltaY = data->position.y - data->previous.y;
    } else if (data->deltaX != 0 || data->deltaY != 0) {
        /* The platform reported a movement without a previous position, which is the one case where its
         * own delta is better than anything derived. */
        out->deltaX = (float)data->deltaX;
        out->deltaY = (float)data->deltaY;
    }
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

Result InputGestureSampleFromTouches(const InputTouchState *touches,
                                     uint64_t timestampMs,
                                     InputGestureSample *out,
                                     InputDiag *diag)
{
    uint32_t index;
    uint32_t live = 0u;
    float sumX = 0.0f;
    float sumY = 0.0f;
    const InputTouchContact *primary;

    if (!out) {
        return RoboInputDiag(diag, RoboInputDomainGesture, RoboInvalidArgument,
                             "a sample needs somewhere to go");
    }
    InputGestureSampleInit(out);
    if (!touches) {
        out->contactCount = 0u;
        out->space = RoboInputSpaceUnknown;
        if (diag) {
            InputDiagReset(diag);
        }
        return RoboOk;
    }

    for (index = 0u; index < RoboInputMaxTouches; index++) {
        const InputTouchContact *contact = &touches->contacts[index];

        if (contact->device == RoboInputDeviceIdNone) {
            continue;
        }
        if ((contact->flags & (uint32_t)RoboInputPointerFlagCancelled) != 0u) {
            out->cancelled = 1;
            continue;
        }
        sumX += contact->position.x;
        sumY += contact->position.y;
        live++;
    }
    out->contactCount = live;
    out->timestampMs = timestampMs;
    if (live == 0u) {
        out->space = RoboInputSpaceUnknown;
        if (diag) {
            InputDiagReset(diag);
        }
        return RoboOk;
    }

    out->centroid = MakeGfxPoint(sumX / (float)live, sumY / (float)live);
    primary = InputTouchStatePrimary(touches);
    if (primary) {
        out->pointer = primary->id;
        out->device = primary->device;
        out->space = primary->space;
        out->position = primary->position;
        out->hasPosition = 1;
        out->type = InputTouchPhaseToEventType(primary->phase);
    }

    /* The spread and the angle: computed here, once, so a pinch and a rotate cannot disagree about what the
     * fingers were doing. */
    {
        float sumRadius = 0.0f;
        float sumSin = 0.0f;
        float sumCos = 0.0f;
        uint32_t counted = 0u;
        int firstAxis = 1;

        for (index = 0u; index < RoboInputMaxTouches; index++) {
            const InputTouchContact *contact = &touches->contacts[index];
            float dx;
            float dy;

            if (contact->device == RoboInputDeviceIdNone ||
                (contact->flags & (uint32_t)RoboInputPointerFlagCancelled) != 0u) {
                continue;
            }
            dx = contact->position.x - out->centroid.x;
            dy = contact->position.y - out->centroid.y;
            sumRadius += RiGestureSqrt(dx * dx + dy * dy);
            if (firstAxis) {
                sumSin = dy;
                sumCos = dx;
                firstAxis = 0;
            }
            counted++;
        }
        out->spread = counted > 0u ? sumRadius / (float)counted : 0.0f;
        /*
         * The axis: the direction from the centroid to the first live contact, normalized. With fewer than two
         * contacts there is no line to measure, and the axis stays zero rather than becoming a NaN a caller
         * would have to guard.
         */
        if (live >= 2u) {
            float length = RiGestureSqrt(sumSin * sumSin + sumCos * sumCos);

            if (length > 0.0f) {
                out->axisX = sumCos / length;
                out->axisY = sumSin / length;
            }
        }
    }

    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

/* ============================================================================================== *
 * The event
 * ============================================================================================== */

void InputGestureEventInit(InputGestureEvent *event)
{
    if (!event) {
        return;
    }
    memset(event, 0, sizeof(*event));
    event->type = RoboInputGestureNone;
    event->phase = RoboInputGesturePhaseBegin;
    event->scale = 1.0f;
    event->space = RoboInputSpaceUnknown;
}

int InputGestureEventIsValid(const InputGestureEvent *event)
{
    if (!event) {
        return 0;
    }
    if (!InputGestureTypeIsKnown(event->type) || event->type == RoboInputGestureNone) {
        return 0;
    }
    if (!InputGesturePhaseIsKnown(event->phase)) {
        return 0;
    }
    if (!InputSpaceIsKnown(event->space)) {
        return 0;
    }
    /* A multi-touch gesture with one contact is a record that disagrees with itself. */
    if (InputGestureTypeIsMultiTouch(event->type) && event->contactCount < 2u) {
        return 0;
    }
    if (event->scale <= 0.0f) {
        return 0;
    }
    return 1;
}

/* ============================================================================================== *
 * The arena
 * ============================================================================================== */

void InputGestureArenaInit(InputGestureArena *arena)
{
    if (!arena) {
        return;
    }
    memset(arena, 0, sizeof(*arena));
    arena->winner = -1;
}

int InputGestureArenaAdd(InputGestureArena *arena, const InputGestureRecognizer *recognizer)
{
    int index;

    if (!arena || !recognizer || !recognizer->offer) {
        return -1;
    }
    if (arena->count >= RoboInputMaxRecognizers) {
        return -1;
    }
    arena->entries[arena->count] = *recognizer;
    arena->entries[arena->count].offered = 0u;
    arena->entries[arena->count].accepted = 0u;
    arena->entries[arena->count].rejected = 0u;
    index = (int)arena->count;
    arena->count++;
    /* A new recognizer joins the running sequence only if nothing has won it yet: a recognizer registered
     * mid-gesture must not be able to hijack it. */
    return index;
}

void InputGestureArenaReset(InputGestureArena *arena)
{
    uint32_t index;

    if (!arena) {
        return;
    }
    for (index = 0u; index < arena->count; index++) {
        if (arena->entries[index].reset) {
            arena->entries[index].reset(arena->entries[index].userdata);
        }
    }
    if (arena->winner >= 0) {
        arena->sequences++;
    }
    arena->winner = -1;
}

int InputGestureArenaWinner(const InputGestureArena *arena)
{
    return arena ? arena->winner : -1;
}

/** Release the winner and reset every recognizer: the sequence is over, recognised or not. */
static void RiGestureArenaRelease(InputGestureArena *arena)
{
    uint32_t index;

    arena->winner = -1;
    for (index = 0u; index < arena->count; index++) {
        if (arena->entries[index].reset) {
            arena->entries[index].reset(arena->entries[index].userdata);
        }
    }
}

Result InputGestureArenaFeed(InputGestureArena *arena,
                             const InputGestureSample *sample,
                             InputGestureEvent *out,
                             uint32_t capacity,
                             uint32_t *outCount,
                             InputDiag *diag)
{
    InputGestureEvent scratch[RoboInputMaxGestureEvents];
    uint32_t emitted = 0u;
    uint32_t asked = 0u;
    uint32_t deferred = 0u;
    uint32_t index;
    int terminal = 0;

    if (outCount) {
        *outCount = 0u;
    }
    if (!arena || !sample || !out || !outCount) {
        return RoboInputDiag(diag, RoboInputDomainGesture, RoboInvalidArgument,
                             "an arena, a sample, a buffer and a count are required");
    }

    arena->samples++;

    /*
     * Every recognizer writes into the *scratch* buffer, and only the winner's events are copied out. A
     * shared destination would let a recognizer that lost the sequence leave its own events behind -- which
     * is how a rejected pan delivers a drag nobody claimed.
     */
    if (arena->winner >= 0) {
        /* The winner *owns* the sequence: feeding anyone else here is how a drag becomes a pan halfway
         * through. */
        InputGestureRecognizer *winner = &arena->entries[(uint32_t)arena->winner];
        uint32_t local = 0u;

        winner->offered++;
        asked = 1u;
        memset(scratch, 0, sizeof(scratch));
        (void)winner->offer(winner->userdata, sample, scratch, RoboInputMaxGestureEvents, &local);
        if (local > RoboInputMaxGestureEvents) {
            local = RoboInputMaxGestureEvents;
        }
        for (index = 0u; index < local && emitted < capacity; index++) {
            out[emitted] = scratch[index];
            if (InputGesturePhaseIsTerminal(scratch[index].phase)) {
                terminal = 1;
            }
            emitted++;
        }
        *outCount = emitted;
        if (terminal) {
            arena->sequences++;
            RiGestureArenaRelease(arena);
        }
        if (diag) {
            InputDiagReset(diag);
        }
        return RoboOk;
    }

    /*
     * No winner yet: ask everyone in the caller's priority order. `Accept` wins and stops the walk; `Reject`
     * drops an entry from this sequence; `Defer` leaves it in.
     */
    for (index = 0u; index < arena->count; index++) {
        InputGestureRecognizer *entry = &arena->entries[index];
        uint32_t local = 0u;
        InputGestureVerdict verdict;
        uint32_t slot;

        if (!entry->offer) {
            continue;
        }
        entry->offered++;
        asked++;
        memset(scratch, 0, sizeof(scratch));
        verdict = entry->offer(entry->userdata, sample, scratch, RoboInputMaxGestureEvents, &local);
        if (local > RoboInputMaxGestureEvents) {
            local = RoboInputMaxGestureEvents;
        }

        if (verdict == RoboInputGestureVerdictAccept) {
            entry->accepted++;
            arena->winner = (int)index;
            for (slot = 0u; slot < local && emitted < capacity; slot++) {
                out[emitted] = scratch[slot];
                if (InputGesturePhaseIsTerminal(scratch[slot].phase)) {
                    terminal = 1;
                }
                emitted++;
            }
            break;
        }
        if (verdict == RoboInputGestureVerdictReject) {
            entry->rejected++;
        } else {
            deferred++;
            /* A deferring recognizer's events, if any, are still honoured: a recognizer may report something
             * about a sequence without claiming it. */
            for (slot = 0u; slot < local && emitted < capacity; slot++) {
                out[emitted] = scratch[slot];
                emitted++;
            }
        }
    }

    *outCount = emitted;
    /*
     * Nobody claimed it *and* nobody was still unsure: that is the only state in which "this sequence is
     * nothing to anyone" is a true statement. A single deferring recognizer keeps the sequence open.
     */
    if (asked > 0u && arena->winner < 0 && deferred == 0u) {
        arena->unresolved++;
        RiGestureArenaRelease(arena);
    } else if (terminal) {
        arena->sequences++;
        RiGestureArenaRelease(arena);
    }
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

/* ============================================================================================== *
 * The heuristic recognizer
 * ============================================================================================== */

InputGestureHeuristicsConfig InputGestureHeuristicsConfigDefault(void)
{
    InputGestureHeuristicsConfig config;

    config.tapSlop = 8.0f;
    config.tapMaxMs = 300u;
    config.longPressMs = 500u;
    config.dragThreshold = 8.0f;
    config.panThreshold = 12.0f;
    config.pinchRatio = 1.25f;
    config.rotateSin = 0.1736f;
    config.doubleTap = 1;
    return config;
}

void InputGestureHeuristicsInit(InputGestureHeuristics *state,
                                const InputGestureHeuristicsConfig *config)
{
    if (!state) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->config = config ? *config : InputGestureHeuristicsConfigDefault();
}

void InputGestureHeuristicsReset(void *userdata)
{
    InputGestureHeuristics *state = (InputGestureHeuristics *)userdata;

    if (!state) {
        return;
    }
    state->armed = 0;
    state->reported = 0;
    state->type = RoboInputGestureNone;
    state->contacts = 0u;
    state->spread = 0.0f;
    state->axisX = 0.0f;
    state->axisY = 0.0f;
    state->ticks = 0u;
    /* The double-tap memory deliberately survives a reset: the second tap of a double tap arrives as a
     * *new* sequence, and clearing it here is exactly how a double tap would never be recognized. */
}

static float RiGestureAbs(float value)
{
    return value < 0.0f ? -value : value;
}

static void RiGestureEmit(InputGestureEvent *out,
                          InputGestureType type,
                          InputGesturePhase phase,
                          const InputGestureSample *sample,
                          const InputGestureHeuristics *state,
                          float totalX,
                          float totalY,
                          float scale,
                          float rotation)
{
    InputGestureEventInit(out);
    out->type = type;
    out->phase = phase;
    out->pointer = sample->pointer;
    out->position = sample->hasPosition ? sample->position : sample->centroid;
    out->totalX = totalX;
    out->totalY = totalY;
    out->deltaX = sample->deltaX;
    out->deltaY = sample->deltaY;
    out->contactCount = sample->contactCount;
    out->scale = scale;
    out->rotation = rotation;
    out->durationMs = sample->timestampMs >= state->startTime
                          ? (uint32_t)(sample->timestampMs - state->startTime)
                          : 0u;
    out->space = InputSpaceIsKnown(sample->space) ? sample->space : RoboInputSpaceWindow;
}

InputGestureVerdict InputGestureHeuristicsOffer(void *userdata,
                                                const InputGestureSample *sample,
                                                InputGestureEvent *out,
                                                uint32_t capacity,
                                                uint32_t *outCount)
{
    InputGestureHeuristics *state = (InputGestureHeuristics *)userdata;
    float totalX;
    float totalY;
    GfxPoint here;

    if (outCount) {
        *outCount = 0u;
    }
    if (!state || !sample || !out || capacity == 0u || !outCount) {
        return RoboInputGestureVerdictReject;
    }

    state->ticks++;

    /* A cancelled sequence is nobody's: report that whatever was running is over, and refuse. */
    if (sample->cancelled) {
        if (state->reported) {
            RiGestureEmit(&out[0], state->type, RoboInputGesturePhaseCancel, sample, state, 0.0f, 0.0f,
                          1.0f, 0.0f);
            *outCount = 1u;
            InputGestureHeuristicsReset(state);
        } else {
            state->armed = 0;
            state->reported = 0;
        }
        return RoboInputGestureVerdictReject;
    }

    if (!state->armed) {
        if (sample->tick) {
            /* A tick with no sequence is not a gesture's beginning. */
            return RoboInputGestureVerdictDefer;
        }
        if (sample->type != RoboInputEventPointerDown && sample->type != RoboInputEventTouchBegin) {
            /* A move with nothing armed is not a gesture: there is no beginning to measure from. */
            return RoboInputGestureVerdictDefer;
        }
        state->armed = 1;
        state->reported = 0;
        state->type = RoboInputGestureNone;
        state->start = sample->hasPosition ? sample->position : sample->centroid;
        state->startTime = sample->timestampMs;
        state->pointer = sample->pointer;
        state->contacts = sample->contactCount;
        state->spread = sample->spread;
        state->axisX = sample->axisX;
        state->axisY = sample->axisY;
        /* A tap is not recognized on the `DOWN`: it is only a tap if the finger *lifts* without travelling,
         * and a recognizer that accepted here would claim a drag's own beginning. */
        return RoboInputGestureVerdictDefer;
    }

    /* Where this sample is: the pointer's own position, or the centroid of the fingers when the sample is a
     * tick. Declared once because three branches below measure from it. */
    here = sample->hasPosition ? sample->position : sample->centroid;
    totalX = here.x - state->start.x;
    totalY = here.y - state->start.y;

    /* A multi-touch gesture is decided by the fingers' geometry, and only against the values the sequence
     * *began* with -- a pinch measured against the previous sample would drift with the fingers. */
    if (sample->contactCount >= 2u && state->contacts >= 2u) {
        float spread = sample->spread > 0.0f ? sample->spread : state->spread;
        float ratio = state->spread > 0.0f ? spread / state->spread : 1.0f;
        /* The sine of the turn: the cross product of the two unit axes, which is positive one way round and
         * negative the other -- exactly the information a rotate needs and no arc tangent required. */
        float rotation = state->axisX * sample->axisY - state->axisY * sample->axisX;

        if (state->reported) {
            float scale = state->spread > 0.0f ? spread / state->spread : 1.0f;

            RiGestureEmit(&out[0], state->type,
                          sample->type == RoboInputEventPointerUp ||
                                  sample->type == RoboInputEventTouchEnd
                              ? RoboInputGesturePhaseEnd
                              : RoboInputGesturePhaseUpdate,
                          sample, state, totalX, totalY, scale, rotation);
            *outCount = 1u;
            return RoboInputGestureVerdictAccept;
        }
        if (ratio >= state->config.pinchRatio || ratio <= 1.0f / state->config.pinchRatio) {
            state->reported = 1;
            state->type = RoboInputGesturePinch;
            RiGestureEmit(&out[0], RoboInputGesturePinch, RoboInputGesturePhaseBegin, sample, state,
                          totalX, totalY, ratio, rotation);
            *outCount = 1u;
            return RoboInputGestureVerdictAccept;
        }
        if (RiGestureAbs(rotation) >= state->config.rotateSin) {
            state->reported = 1;
            state->type = RoboInputGestureRotate;
            RiGestureEmit(&out[0], RoboInputGestureRotate, RoboInputGesturePhaseBegin, sample, state,
                          totalX, totalY, 1.0f, rotation);
            *outCount = 1u;
            return RoboInputGestureVerdictAccept;
        }
        if (RiGestureSqrt(totalX * totalX + totalY * totalY) >= state->config.panThreshold) {
            state->reported = 1;
            state->type = RoboInputGesturePan;
            RiGestureEmit(&out[0], RoboInputGesturePan, RoboInputGesturePhaseBegin, sample, state, totalX,
                          totalY, 1.0f, 0.0f);
            *outCount = 1u;
            return RoboInputGestureVerdictAccept;
        }
        return RoboInputGestureVerdictDefer;
    }

    /* One finger. */
    if (state->reported && state->type == RoboInputGestureDrag) {
        RiGestureEmit(&out[0], RoboInputGestureDrag,
                      sample->type == RoboInputEventPointerUp || sample->type == RoboInputEventTouchEnd
                          ? RoboInputGesturePhaseEnd
                          : RoboInputGesturePhaseUpdate,
                      sample, state, totalX, totalY, 1.0f, 0.0f);
        *outCount = 1u;
        return RoboInputGestureVerdictAccept;
    }

    if (RiGestureSqrt(totalX * totalX + totalY * totalY) >= state->config.dragThreshold) {
        state->reported = 1;
        state->type = RoboInputGestureDrag;
        RiGestureEmit(&out[0], RoboInputGestureDrag, RoboInputGesturePhaseBegin, sample, state, totalX,
                      totalY, 1.0f, 0.0f);
        *outCount = 1u;
        return RoboInputGestureVerdictAccept;
    }

    if (sample->tick && sample->timestampMs >= state->startTime + state->config.longPressMs) {
        state->reported = 1;
        state->type = RoboInputGestureLongPress;
        RiGestureEmit(&out[0], RoboInputGestureLongPress, RoboInputGesturePhaseBegin, sample, state,
                      totalX, totalY, 1.0f, 0.0f);
        *outCount = 1u;
        return RoboInputGestureVerdictAccept;
    }

    if (sample->type == RoboInputEventPointerUp || sample->type == RoboInputEventTouchEnd) {
        int isDouble;

        /* A tap: down and up, near where it started, inside the tap window. */
        if ((uint32_t)(sample->timestampMs - state->startTime) > state->config.tapMaxMs) {
            /* Held too long without a tick to promote it: nothing. */
            state->armed = 0;
            return RoboInputGestureVerdictReject;
        }
        isDouble = state->config.doubleTap && state->pendingTap &&
                   sample->timestampMs - state->lastTapTime <= 500u &&
                   RiGestureAbs(here.x - state->lastTapPosition.x) +
                           RiGestureAbs(here.y - state->lastTapPosition.y) <=
                       state->config.tapSlop;

        RiGestureEmit(&out[0], isDouble ? RoboInputGestureDoubleTap : RoboInputGestureTap,
                      RoboInputGesturePhaseEnd, sample, state, totalX, totalY, 1.0f, 0.0f);
        *outCount = 1u;
        state->reported = 1;
        state->type = isDouble ? RoboInputGestureDoubleTap : RoboInputGestureTap;
        state->pendingTap = !isDouble;
        state->lastTapPosition = here;
        state->lastTapTime = sample->timestampMs;
        state->armed = 0;
        return RoboInputGestureVerdictAccept;
    }

    return RoboInputGestureVerdictDefer;
}

/* ============================================================================================== *
 * Diagnostics
 * ============================================================================================== */

Result InputGestureEventWrite(const InputGestureEvent *event,
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
    if (!event) {
        RiWriterText(&text, "gesture <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "gesture ");
    RiWriterText(&text, InputGestureTypeName(event->type));
    RiWriterFieldText(&text, " phase", InputGesturePhaseName(event->phase));
    RiWriterFieldU64(&text, " pointer", (uint64_t)event->pointer);
    RiWriterFieldText(&text, " space", InputSpaceName(event->space));
    RiWriterFieldPoint(&text, " at", (int32_t)event->position.x, (int32_t)event->position.y);
    RiWriterFieldI32(&text, " totalX", (int32_t)event->totalX);
    RiWriterFieldI32(&text, " totalY", (int32_t)event->totalY);
    RiWriterFieldU64(&text, " contacts", (uint64_t)event->contactCount);
    RiWriterFieldU64(&text, " ms", (uint64_t)event->durationMs);
    RiWriterChar(&text, '\n');
    return RiWriterFinish(&text, outLength, diag);
}

Result InputGestureArenaWrite(const InputGestureArena *arena,
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
    if (!arena) {
        RiWriterText(&text, "gesture-arena <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "gesture-arena recognizers=");
    RiWriterU64(&text, (uint64_t)arena->count);
    RiWriterFieldU64(&text, " winner", (uint64_t)(arena->winner < 0 ? 0u : (uint64_t)arena->winner + 1u));
    RiWriterFieldU64(&text, " sequences", (uint64_t)arena->sequences);
    RiWriterFieldU64(&text, " unresolved", (uint64_t)arena->unresolved);
    RiWriterFieldU64(&text, " samples", (uint64_t)arena->samples);
    RiWriterChar(&text, '\n');
    for (index = 0u; index < arena->count; index++) {
        RiWriterText(&text, "  ");
        RiWriterU64(&text, (uint64_t)index);
        RiWriterFieldText(&text, " type", InputGestureTypeName(arena->entries[index].type));
        RiWriterFieldU64(&text, " offered", (uint64_t)arena->entries[index].offered);
        RiWriterFieldU64(&text, " accepted", (uint64_t)arena->entries[index].accepted);
        RiWriterFieldU64(&text, " rejected", (uint64_t)arena->entries[index].rejected);
        RiWriterChar(&text, '\n');
    }
    return RiWriterFinish(&text, outLength, diag);
}

Result InputGestureHeuristicsWrite(const InputGestureHeuristics *state,
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
        RiWriterText(&text, "gesture-heuristics <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "gesture-heuristics armed=");
    RiWriterU64(&text, (uint64_t)(state->armed ? 1u : 0u));
    RiWriterFieldText(&text, " type", InputGestureTypeName(state->type));
    RiWriterFieldU64(&text, " contacts", (uint64_t)state->contacts);
    RiWriterFieldU64(&text, " ticks", (uint64_t)state->ticks);
    RiWriterFieldU64(&text, " double", (uint64_t)(state->config.doubleTap ? 1u : 0u));
    RiWriterChar(&text, '\n');
    return RiWriterFinish(&text, outLength, diag);
}
