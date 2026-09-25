/*
 * Pointer state (Phase 13.14--13.19) — see `RoboInputPointerState.h`.
 *
 * Five pieces, in the order a caller meets them: the wheel conversion, the live pointer table, the click
 * counter, capture, and hover. They share one property that is the reason they are one file rather than
 * five: **none of them routes or hit-tests**. Each is a pure function of state plus a caller's statement
 * ("this pointer is at P", "this pointer is over T"), which is what makes them provable without a window.
 */

#include "RoboInput/RoboInputPointerState.h"

#include "RoboInputInternal.h"

#include <string.h>

/* ============================================================================================== *
 * Wheel normalization
 * ============================================================================================== */

InputWheelPixels InputWheelToPixels(const InputWheelData *wheel,
                                    int32_t lineHeightPx,
                                    int32_t pageHeightPx)
{
    InputWheelPixels pixels;

    pixels.deltaX = 0;
    pixels.deltaY = 0;
    pixels.unit = RoboMouseScrollUnitLine;
    if (!wheel) {
        return pixels;
    }

    pixels.unit = wheel->unit;
    if (lineHeightPx < 0) {
        lineHeightPx = 0;
    }
    if (pageHeightPx < 0) {
        pageHeightPx = 0;
    }

    switch (wheel->unit) {
    case RoboMouseScrollUnitPixel:
        pixels.deltaX = wheel->deltaX;
        pixels.deltaY = wheel->deltaY;
        break;

    case RoboMouseScrollUnitLine:
        pixels.deltaX = wheel->deltaX * lineHeightPx;
        pixels.deltaY = wheel->deltaY * lineHeightPx;
        break;

    default:
        break;
    }

    /*
     * A page has no numeric unit of its own: RoboWindow defines two, so a page delta is expressed as a
     * line delta the platform has already multiplied. Dividing by the line height and multiplying by the
     * page height would be right for a platform that sends raw notches and wrong for one that sends
     * pixels. The honest answer is the pass-through above plus this note: a `PAGE` unit is not a unit this
     * build defines, `InputScrollUnitIsKnown` says so, and a caller that meets one scales it itself.
     */
    (void)pageHeightPx;
    return pixels;
}

/* ============================================================================================== *
 * The live pointer table
 * ============================================================================================== */

void InputPointerStateInit(InputPointerState *state)
{
    if (!state) {
        return;
    }
    memset(state, 0, sizeof(*state));
}

static InputPointerTrack *RiPointerTrackFind(InputPointerState *state, InputPointerId pointer)
{
    uint32_t index;

    if (!state) {
        return NULL;
    }
    for (index = 0u; index < RoboInputMaxPointers; index++) {
        if (state->tracks[index].active && state->tracks[index].pointer == pointer) {
            return &state->tracks[index];
        }
    }
    return NULL;
}

static InputPointerTrack *RiPointerTrackFree(InputPointerState *state)
{
    uint32_t index;

    for (index = 0u; index < RoboInputMaxPointers; index++) {
        if (!state->tracks[index].active) {
            return &state->tracks[index];
        }
    }
    return NULL;
}

Result InputPointerStateApply(InputPointerState *state,
                              InputDeviceId device,
                              InputDeviceType type,
                              const InputPointerData *data,
                              InputSpace space,
                              InputModifiers modifiers,
                              InputPointerTrack *outTrack,
                              InputDiag *diag)
{
    InputPointerTrack *track;

    if (outTrack) {
        memset(outTrack, 0, sizeof(*outTrack));
    }
    if (!state) {
        return RoboInputDiag(diag, RoboInputDomainPointer, RoboInvalidArgument,
                             "a pointer state is required");
    }
    if (!data) {
        return RoboInputDiag(diag, RoboInputDomainPointer, RoboInvalidArgument,
                             "a pointer event's payload is required");
    }
    if (!InputSpaceIsKnown(space)) {
        return RoboInputDiag(diag, RoboInputDomainPointer, RoboInvalidArgument,
                             "a pointer position needs a known space");
    }

    track = RiPointerTrackFind(state, data->pointer);
    if (!track) {
        track = RiPointerTrackFree(state);
    }
    if (!track) {
        /* Every slot is live and this is a new pointer. Counted, so a caller can see it happening. */
        state->overflowed++;
        return RoboInputDiag(diag, RoboInputDomainPointer, RoboOutOfMemory,
                             "the pointer table is full");
    }

    if (!track->active) {
        memset(track, 0, sizeof(*track));
        track->active = 1;
        track->pointer = data->pointer;
        state->count++;
    }

    track->type = type;
    track->device = device;
    track->buttons = data->buttons;
    track->position = data->position;
    track->space = space;
    track->modifiers = modifiers;
    track->flags = data->flags;
    if (outTrack) {
        *outTrack = *track;
    }
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

const InputPointerTrack *InputPointerStateFind(const InputPointerState *state, InputPointerId pointer)
{
    uint32_t index;

    if (!state) {
        return NULL;
    }
    for (index = 0u; index < RoboInputMaxPointers; index++) {
        if (state->tracks[index].active && state->tracks[index].pointer == pointer) {
            return &state->tracks[index];
        }
    }
    return NULL;
}

int InputPointerStateIsButtonDown(const InputPointerState *state,
                                  InputPointerId pointer,
                                  MouseButton button)
{
    const InputPointerTrack *track = InputPointerStateFind(state, pointer);

    if (!track) {
        return 0;
    }
    return InputButtonMaskHas(track->buttons, button);
}

InputModifiers InputPointerStateModifiers(const InputPointerState *state, InputPointerId pointer)
{
    const InputPointerTrack *track = InputPointerStateFind(state, pointer);

    return track ? track->modifiers : RoboInputModifiersNone;
}

int InputPointerStateRemove(InputPointerState *state, InputPointerId pointer)
{
    InputPointerTrack *track = RiPointerTrackFind(state, pointer);

    if (!track) {
        return 0;
    }
    memset(track, 0, sizeof(*track));
    if (state->count > 0u) {
        state->count--;
    }
    return 1;
}

uint32_t InputPointerStateRemoveDevice(InputPointerState *state, InputDeviceId device)
{
    uint32_t index;
    uint32_t removed = 0u;

    if (!state || device == RoboInputDeviceIdNone) {
        return 0u;
    }
    for (index = 0u; index < RoboInputMaxPointers; index++) {
        if (state->tracks[index].active && state->tracks[index].device == device) {
            memset(&state->tracks[index], 0, sizeof(state->tracks[index]));
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

uint32_t InputPointerStateClear(InputPointerState *state)
{
    uint32_t removed;

    if (!state) {
        return 0u;
    }
    removed = state->count;
    memset(state->tracks, 0, sizeof(state->tracks));
    state->count = 0u;
    return removed;
}

/* ============================================================================================== *
 * Click counting
 * ============================================================================================== */

InputClickConfig InputClickConfigDefault(void)
{
    InputClickConfig config;

    config.doubleClickTimeMs = 500u;
    config.doubleClickDistance = 4.0f;
    return config;
}

void InputClickStateInit(InputClickState *state)
{
    if (!state) {
        return;
    }
    memset(state, 0, sizeof(*state));
}

static InputClickSequence *RiClickFind(InputClickState *state,
                                       InputPointerId pointer,
                                       MouseButton button)
{
    uint32_t index;

    if (!state) {
        return NULL;
    }
    for (index = 0u; index < RoboInputMaxPointers * 3u; index++) {
        if (state->sequences[index].active && state->sequences[index].pointer == pointer &&
            state->sequences[index].button == button) {
            return &state->sequences[index];
        }
    }
    return NULL;
}

static InputClickSequence *RiClickSlot(InputClickState *state)
{
    uint32_t index;
    uint32_t oldest = 0u;

    for (index = 0u; index < RoboInputMaxPointers * 3u; index++) {
        if (!state->sequences[index].active) {
            return &state->sequences[index];
        }
    }
    /* Full: recycle the sequence touched longest ago. The table is small and a stale pair is the one a
     *  reader is least likely to be asking about. */
    for (index = 1u; index < RoboInputMaxPointers * 3u; index++) {
        if (state->sequences[index].lastTime < state->sequences[oldest].lastTime) {
            oldest = index;
        }
    }
    return &state->sequences[oldest];
}

int InputClickStatePress(InputClickState *state,
                         InputPointerId pointer,
                         MouseButton button,
                         GfxPoint position,
                         uint64_t timestamp,
                         const InputClickConfig *config)
{
    InputClickConfig effective = config ? *config : InputClickConfigDefault();
    InputClickSequence *sequence;
    int continues = 0;

    if (!state) {
        return 0;
    }
    if (button == RoboMouseButtonNone) {
        /* A move is not a press. It still counts as activity for nothing: it changes no sequence. */
        return 0;
    }
    sequence = RiClickFind(state, pointer, button);

    if (sequence && effective.doubleClickTimeMs > 0u) {
        float dx;
        float dy;
        float distance;

        if (timestamp >= sequence->lastTime &&
            (timestamp - sequence->lastTime) <= (uint64_t)effective.doubleClickTimeMs) {
            dx = position.x - sequence->lastPosition.x;
            dy = position.y - sequence->lastPosition.y;
            distance = (dx < 0.0f ? -dx : dx) + (dy < 0.0f ? -dy : dy);
            continues = distance <= effective.doubleClickDistance ? 1 : 0;
        }
    }

    if (!sequence) {
        sequence = RiClickSlot(state);
        memset(sequence, 0, sizeof(*sequence));
        sequence->active = 1;
        sequence->pointer = pointer;
        sequence->button = button;
        state->count++;
    } else if (!continues) {
        sequence->count = 0;
    }

    sequence->count = continues ? sequence->count + 1 : 1;
    sequence->lastTime = timestamp;
    sequence->lastPosition = position;
    return sequence->count;
}

int InputClickStateReset(InputClickState *state, InputPointerId pointer, MouseButton button)
{
    InputClickSequence *sequence = RiClickFind(state, pointer, button);

    if (!sequence) {
        return 0;
    }
    memset(sequence, 0, sizeof(*sequence));
    if (state->count > 0u) {
        state->count--;
    }
    return 1;
}

uint32_t InputClickStateResetPointer(InputClickState *state, InputPointerId pointer)
{
    uint32_t index;
    uint32_t removed = 0u;

    if (!state) {
        return 0u;
    }
    for (index = 0u; index < RoboInputMaxPointers * 3u; index++) {
        if (state->sequences[index].active && state->sequences[index].pointer == pointer) {
            memset(&state->sequences[index], 0, sizeof(state->sequences[index]));
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

int InputClickStateCount(const InputClickState *state, InputPointerId pointer, MouseButton button)
{
    uint32_t index;

    if (!state) {
        return 0;
    }
    for (index = 0u; index < RoboInputMaxPointers * 3u; index++) {
        if (state->sequences[index].active && state->sequences[index].pointer == pointer &&
            state->sequences[index].button == button) {
            return state->sequences[index].count;
        }
    }
    return 0;
}

/* ============================================================================================== *
 * Pointer capture
 * ============================================================================================== */

void InputPointerCaptureInit(InputPointerCapture *capture)
{
    if (!capture) {
        return;
    }
    memset(capture, 0, sizeof(*capture));
}

static InputCapture *RiCaptureFind(InputPointerCapture *capture, InputPointerId pointer)
{
    uint32_t index;

    if (!capture) {
        return NULL;
    }
    for (index = 0u; index < RoboInputMaxCaptures; index++) {
        if (capture->captures[index].active && capture->captures[index].pointer == pointer) {
            return &capture->captures[index];
        }
    }
    return NULL;
}

Result InputPointerCaptureSet(InputPointerCapture *capture,
                              InputPointerId pointer,
                              const InputTarget *target,
                              InputDiag *diag)
{
    InputCapture *slot;

    if (!capture) {
        return RoboInputDiag(diag, RoboInputDomainPointer, RoboInvalidArgument,
                             "a capture table is required");
    }
    if (!InputTargetIsValid(target)) {
        capture->refused++;
        return RoboInputDiag(diag, RoboInputDomainPointer, RoboInvalidArgument,
                             "only a valid target may hold a pointer");
    }

    slot = RiCaptureFind(capture, pointer);
    if (!slot) {
        uint32_t index;

        for (index = 0u; index < RoboInputMaxCaptures; index++) {
            if (!capture->captures[index].active) {
                slot = &capture->captures[index];
                break;
            }
        }
    }
    if (!slot) {
        capture->refused++;
        return RoboInputDiag(diag, RoboInputDomainPointer, RoboOutOfMemory,
                             "the capture table is full");
    }

    if (!slot->active) {
        slot->active = 1;
        slot->pointer = pointer;
        capture->count++;
    }
    slot->target = *target;
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

int InputPointerCaptureRelease(InputPointerCapture *capture,
                               InputPointerId pointer,
                               const InputTarget *target)
{
    InputCapture *slot = RiCaptureFind(capture, pointer);

    if (!slot) {
        return 0;
    }
    /*
     * The condition, not a courtesy: a target that lost the pointer to a newer holder must not be able to
     * release the newer holder's capture when its own stale `up` finally arrives. A NULL target means "the
     * layer releasing on a pointer's behalf" and always wins, because the layer is the one that saw the
     * pointer end.
     */
    if (target && !InputTargetEqual(&slot->target, target)) {
        return 0;
    }
    memset(slot, 0, sizeof(*slot));
    if (capture->count > 0u) {
        capture->count--;
    }
    return 1;
}

const InputTarget *InputPointerCaptureTarget(const InputPointerCapture *capture, InputPointerId pointer)
{
    uint32_t index;

    if (!capture) {
        return NULL;
    }
    for (index = 0u; index < RoboInputMaxCaptures; index++) {
        if (capture->captures[index].active && capture->captures[index].pointer == pointer) {
            return &capture->captures[index].target;
        }
    }
    return NULL;
}

int InputPointerCaptureHolds(const InputPointerCapture *capture,
                             InputPointerId pointer,
                             const InputTarget *target)
{
    const InputTarget *held = InputPointerCaptureTarget(capture, pointer);

    return held && InputTargetEqual(held, target) ? 1 : 0;
}

uint32_t InputPointerCaptureReleaseTarget(InputPointerCapture *capture, const InputTarget *target)
{
    uint32_t index;
    uint32_t removed = 0u;

    if (!capture || !target) {
        return 0u;
    }
    for (index = 0u; index < RoboInputMaxCaptures; index++) {
        if (capture->captures[index].active &&
            InputTargetEqual(&capture->captures[index].target, target)) {
            memset(&capture->captures[index], 0, sizeof(capture->captures[index]));
            removed++;
        }
    }
    if (removed > capture->count) {
        capture->count = 0u;
    } else {
        capture->count -= removed;
    }
    return removed;
}

uint32_t InputPointerCaptureReleaseWindow(InputPointerCapture *capture, WindowId window)
{
    uint32_t index;
    uint32_t removed = 0u;

    if (!capture || window == 0u) {
        return 0u;
    }
    for (index = 0u; index < RoboInputMaxCaptures; index++) {
        if (capture->captures[index].active &&
            InputTargetIsWindow(&capture->captures[index].target, window)) {
            memset(&capture->captures[index], 0, sizeof(capture->captures[index]));
            removed++;
        }
    }
    if (removed > capture->count) {
        capture->count = 0u;
    } else {
        capture->count -= removed;
    }
    return removed;
}

uint32_t InputPointerCaptureReleaseAll(InputPointerCapture *capture,
                                       InputPointerId pointer,
                                       InputTarget *outTargets,
                                       uint32_t capacity)
{
    uint32_t index;
    uint32_t removed = 0u;

    if (!capture) {
        return 0u;
    }
    for (index = 0u; index < RoboInputMaxCaptures; index++) {
        if (capture->captures[index].active && capture->captures[index].pointer == pointer) {
            if (outTargets && removed < capacity) {
                outTargets[removed] = capture->captures[index].target;
            }
            memset(&capture->captures[index], 0, sizeof(capture->captures[index]));
            removed++;
        }
    }
    if (removed > capture->count) {
        capture->count = 0u;
    } else {
        capture->count -= removed;
    }
    return removed;
}

uint32_t InputPointerCaptureClear(InputPointerCapture *capture)
{
    uint32_t removed;

    if (!capture) {
        return 0u;
    }
    removed = capture->count;
    memset(capture->captures, 0, sizeof(capture->captures));
    capture->count = 0u;
    return removed;
}

/* ============================================================================================== *
 * Hover
 * ============================================================================================== */

void InputHoverStateInit(InputHoverState *state)
{
    if (!state) {
        return;
    }
    memset(state, 0, sizeof(*state));
}

static InputHover *RiHoverFind(InputHoverState *state, InputPointerId pointer)
{
    uint32_t index;

    if (!state) {
        return NULL;
    }
    for (index = 0u; index < RoboInputMaxPointers; index++) {
        if (state->hovers[index].active && state->hovers[index].pointer == pointer) {
            return &state->hovers[index];
        }
    }
    return NULL;
}

int InputHoverSet(InputHoverState *state,
                  InputPointerId pointer,
                  const InputTarget *target,
                  InputTarget *outLeave,
                  InputTarget *outEnter)
{
    InputHover *hover;
    int events = 0;
    int reported = 0;

    if (!state) {
        return -1;
    }
    if (target) {
        reported = InputTargetIsValid(target) ? 1 : 0;
        if (!reported) {
            state->deduplicated++;
            return 0;
        }
    }

    hover = RiHoverFind(state, pointer);
    if (!hover) {
        if (!target) {
            /* Over nothing, and nothing was recorded: no transition. */
            return 0;
        }
        hover = NULL;
        for (uint32_t index = 0u; index < RoboInputMaxPointers; index++) {
            if (!state->hovers[index].active) {
                hover = &state->hovers[index];
                break;
            }
        }
        if (!hover) {
            state->deduplicated++;
            return 0;
        }
        hover->active = 1;
        hover->pointer = pointer;
        hover->target = *target;
        state->count++;
        if (outEnter) {
            *outEnter = *target;
        }
        return 1;
    }

    if (InputTargetEqual(&hover->target, target)) {
        /* Already there. Reporting it again produces nothing, which is the rule the dedup counter names. */
        state->deduplicated++;
        return 0;
    }

    /* Leave first, then enter: a reader replaying the stream sees the pointer over nothing in between. */
    if (outLeave) {
        *outLeave = hover->target;
    }
    events = 1;

    if (target) {
        hover->target = *target;
        if (outEnter) {
            *outEnter = *target;
        }
        events = 2;
    } else {
        memset(hover, 0, sizeof(*hover));
        if (state->count > 0u) {
            state->count--;
        }
    }
    return events;
}

const InputTarget *InputHoverTarget(const InputHoverState *state, InputPointerId pointer)
{
    uint32_t index;

    if (!state) {
        return NULL;
    }
    for (index = 0u; index < RoboInputMaxPointers; index++) {
        if (state->hovers[index].active && state->hovers[index].pointer == pointer) {
            return &state->hovers[index].target;
        }
    }
    return NULL;
}

int InputHoverIsOver(const InputHoverState *state, InputPointerId pointer, const InputTarget *target)
{
    const InputTarget *over = InputHoverTarget(state, pointer);

    return over && InputTargetEqual(over, target) ? 1 : 0;
}

int InputHoverStateRemove(InputHoverState *state, InputPointerId pointer)
{
    InputHover *hover = RiHoverFind(state, pointer);

    if (!hover) {
        return 0;
    }
    memset(hover, 0, sizeof(*hover));
    if (state->count > 0u) {
        state->count--;
    }
    return 1;
}

uint32_t InputHoverStateClear(InputHoverState *state)
{
    uint32_t removed;

    if (!state) {
        return 0u;
    }
    removed = state->count;
    memset(state->hovers, 0, sizeof(state->hovers));
    state->count = 0u;
    return removed;
}

/* ============================================================================================== *
 * Diagnostics
 * ============================================================================================== */

Result InputPointerTrackWrite(const InputPointerTrack *track,
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
    if (!track) {
        RiWriterText(&text, "pointer <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "pointer #");
    RiWriterU64(&text, (uint64_t)track->pointer);
    RiWriterText(&text, " active=");
    RiWriterU64(&text, (uint64_t)(track->active ? 1u : 0u));
    RiWriterFieldText(&text, " type", InputDeviceTypeName(track->type));
    RiWriterFieldU64(&text, " device", (uint64_t)track->device);
    RiWriterFieldText(&text, " space", InputSpaceName(track->space));
    RiWriterFieldPoint(&text, " at", (int32_t)track->position.x, (int32_t)track->position.y);
    {
        char mask[16];
        size_t maskLength = 0u;

        if (InputButtonMaskWrite(track->buttons, mask, sizeof(mask), &maskLength) != RoboOk) {
            RiTextCopy(mask, sizeof(mask), "?");
        }
        RiWriterFieldText(&text, " buttons", mask);
    }
    RiWriterChar(&text, '\n');
    return RiWriterFinish(&text, outLength, diag);
}

Result InputPointerStateWrite(const InputPointerState *state,
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
        RiWriterText(&text, "pointers <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "pointers count=");
    RiWriterU64(&text, (uint64_t)state->count);
    RiWriterFieldU64(&text, " overflowed", state->overflowed);
    RiWriterChar(&text, '\n');
    for (index = 0u; index < RoboInputMaxPointers; index++) {
        char line[RoboInputPointerLineMax];
        size_t lineLength = 0u;

        if (!state->tracks[index].active) {
            continue;
        }
        if (InputPointerTrackWrite(&state->tracks[index], line, sizeof(line), &lineLength, NULL) !=
            RoboOk) {
            RiWriterText(&text, "pointer ?\n");
            continue;
        }
        RiWriterText(&text, "  ");
        RiWriterText(&text, line);
    }
    return RiWriterFinish(&text, outLength, diag);
}

Result InputPointerCaptureWrite(const InputPointerCapture *capture,
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
    if (!capture) {
        RiWriterText(&text, "captures <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "captures count=");
    RiWriterU64(&text, (uint64_t)capture->count);
    RiWriterFieldU64(&text, " refused", capture->refused);
    RiWriterChar(&text, '\n');
    for (index = 0u; index < RoboInputMaxCaptures; index++) {
        if (!capture->captures[index].active) {
            continue;
        }
        RiWriterText(&text, "  capture pointer=#");
        RiWriterU64(&text, (uint64_t)capture->captures[index].pointer);
        RiWriterFieldText(&text, " kind", InputTargetKindName(capture->captures[index].target.kind));
        RiWriterFieldU64(&text, " window", (uint64_t)capture->captures[index].target.window);
        RiWriterChar(&text, '\n');
    }
    return RiWriterFinish(&text, outLength, diag);
}

Result InputHoverStateWrite(const InputHoverState *state,
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
        RiWriterText(&text, "hover <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "hover count=");
    RiWriterU64(&text, (uint64_t)state->count);
    RiWriterFieldU64(&text, " deduplicated", state->deduplicated);
    RiWriterChar(&text, '\n');
    for (index = 0u; index < RoboInputMaxPointers; index++) {
        if (!state->hovers[index].active) {
            continue;
        }
        RiWriterText(&text, "  over pointer=#");
        RiWriterU64(&text, (uint64_t)state->hovers[index].pointer);
        RiWriterFieldText(&text, " kind", InputTargetKindName(state->hovers[index].target.kind));
        RiWriterFieldU64(&text, " window", (uint64_t)state->hovers[index].target.window);
        RiWriterChar(&text, '\n');
    }
    return RiWriterFinish(&text, outLength, diag);
}

Result InputClickStateWrite(const InputClickState *state,
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
        RiWriterText(&text, "clicks <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "clicks count=");
    RiWriterU64(&text, (uint64_t)state->count);
    RiWriterChar(&text, '\n');
    for (index = 0u; index < RoboInputMaxPointers * 3u; index++) {
        if (!state->sequences[index].active) {
            continue;
        }
        RiWriterText(&text, "  click pointer=#");
        RiWriterU64(&text, (uint64_t)state->sequences[index].pointer);
        RiWriterFieldU64(&text, " button", (uint64_t)state->sequences[index].button);
        RiWriterFieldU64(&text, " count", (uint64_t)state->sequences[index].count);
        RiWriterFieldU64(&text, " time", state->sequences[index].lastTime);
        RiWriterChar(&text, '\n');
    }
    return RiWriterFinish(&text, outLength, diag);
}
