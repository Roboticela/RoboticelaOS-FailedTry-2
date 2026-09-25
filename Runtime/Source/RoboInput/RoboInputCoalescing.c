/*
 * Event coalescing (Phase 13.47) — see `RoboInputCoalescing.h`.
 *
 * The file is three pieces:
 *
 *   **the rule**    `InputEventTypeMayCoalesce`, one `switch` over three names, plus the table the suite
 *                   iterates. The rule is deliberately *not* "everything with a position": a wheel carries
 *                   a position and a delta and a screen must scroll by the sum, which is a different
 *                   question with a different answer (13.19's own normalizer).
 *
 *   **the table**   fixed-capacity slots keyed on (device, pointer, type), so two fingers are two slots and
 *                   never one. Nothing here allocates, and the table is embedded in the caller's coalescer
 *                   rather than pointed at by it.
 *
 *   **the flush**   slot order in, queue order out, and a queue with no room stops it with the remainder
 *                   still held. This is the only place in the module where an event can be *lost*, so the
 *                   failure path is the one that is written first.
 */

#include "RoboInput/RoboInputCoalescing.h"

#include "RoboInputInternal.h"

#include <string.h>

static const char *const RiCoalesceActionNames[RoboInputCoalesceActionCount] = {
    "emit",
    "hold",
    "flush-first"
};

const InputEventType InputCoalesceTypes[RoboInputCoalesceTypeCount] = {
    RoboInputEventPointerMove,
    RoboInputEventTouchMove,
    RoboInputEventPenMove
};

int InputCoalesceActionIsKnown(InputCoalesceAction action)
{
    return (int)action >= 0 && (size_t)action < (size_t)RoboInputCoalesceActionCount;
}

const char *InputCoalesceActionName(InputCoalesceAction action)
{
    if (!InputCoalesceActionIsKnown(action)) {
        return "unknown";
    }
    return RiCoalesceActionNames[(size_t)action];
}

int InputEventTypeMayCoalesce(InputEventType type)
{
    return type == RoboInputEventPointerMove || type == RoboInputEventTouchMove ||
           type == RoboInputEventPenMove;
}

/* ------------------------------------------------------------------ */
/* The key                                                             */
/* ------------------------------------------------------------------ */

/**
 * The pointer sample @p event carries, or NULL when its payload is not the one its type promises.
 *
 * A pen's sample lives inside the pen record (13.22 nests the pointer half rather than duplicating it), so
 * this is the one place that knows which payload a coalescible type uses. A caller that filled a
 * `PEN_MOVE` with a plain pointer payload gets NULL and a refusal: taking it would mean merging a record
 * whose pressure and tilt fields are whatever was in the caller's struct, and a wrong number carried
 * forward is worse than a refusal.
 */
static InputPointerData *RiCoalescePointerIn(InputEvent *event)
{
    if (!event) {
        return NULL;
    }
    switch (event->type) {
    case RoboInputEventPointerMove:
    case RoboInputEventTouchMove:
        return &event->payload.pointer;
    case RoboInputEventPenMove:
        return &event->payload.pen.pointer;
    default:
        return NULL;
    }
}

/** Index of the slot @p device/@p pointer/@p type is already held in, or -1. */
static int RiCoalesceFind(const InputCoalescer *coalescer,
                          InputDeviceId device,
                          InputPointerId pointer,
                          InputEventType type)
{
    uint32_t index;

    for (index = 0u; index < RoboInputCoalesceSlots; index++) {
        const InputCoalesceSlot *slot = &coalescer->slots[index];

        if (!slot->active) {
            continue;
        }
        if (slot->device == device && slot->pointer == pointer && slot->type == type) {
            return (int)index;
        }
    }
    return -1;
}

/** Index of a free slot, or -1 when the table is full. */
static int RiCoalesceFreeSlot(const InputCoalescer *coalescer)
{
    uint32_t index;

    for (index = 0u; index < RoboInputCoalesceSlots; index++) {
        if (!coalescer->slots[index].active) {
            return (int)index;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

void InputCoalescerInit(InputCoalescer *coalescer)
{
    if (!coalescer) {
        return;
    }
    memset(coalescer, 0, sizeof(*coalescer));
}

uint32_t InputCoalescerHeld(const InputCoalescer *coalescer)
{
    return coalescer ? coalescer->live : 0u;
}

int InputCoalescerIsEmpty(const InputCoalescer *coalescer)
{
    return InputCoalescerHeld(coalescer) == 0u;
}

/* ------------------------------------------------------------------ */
/* The push                                                            */
/* ------------------------------------------------------------------ */

Result InputCoalescerPush(InputCoalescer *coalescer,
                          const InputEvent *event,
                          InputCoalesceAction *outAction,
                          InputDiag *diag)
{
    InputPointerData *pointer;
    int slotIndex;

    if (!coalescer || !event || !outAction) {
        return RoboInputDiag(diag, RoboInputDomainSession, RoboInvalidArgument,
                             "a coalescing push needs a coalescer, an event and an answer");
    }
    *outAction = RoboInputCoalesceEmit;

    if (!InputEventIsValid(event)) {
        coalescer->refusedTotal++;
        return RoboInputDiag(diag, RoboInputDomainSession, RoboInvalidArgument,
                             "an invalid event is not merged");
    }

    /* A barrier. Nothing is taken; the caller is told to drain first when something is held, which is the
     * whole ordering rule -- a press may not overtake the moves it happened after. */
    if (!InputEventTypeMayCoalesce(event->type)) {
        if (coalescer->live > 0u) {
            coalescer->flushTotal++;
            *outAction = RoboInputCoalesceFlushFirst;
            return RoboOk;
        }
        return RoboOk;
    }

    pointer = RiCoalescePointerIn((InputEvent *)event);
    if (!pointer) {
        coalescer->refusedTotal++;
        return RoboInputDiag(diag, RoboInputDomainSession, RoboInvalidArgument,
                             "a coalescible type needs the pointer payload it promises");
    }

    slotIndex = RiCoalesceFind(coalescer, event->device.id, pointer->pointer, event->type);
    if (slotIndex >= 0) {
        InputCoalesceSlot *slot = &coalescer->slots[(size_t)slotIndex];
        int64_t sumX = (int64_t)slot->event.payload.pointer.deltaX + (int64_t)pointer->deltaX;
        int64_t sumY = (int64_t)slot->event.payload.pointer.deltaY + (int64_t)pointer->deltaY;
        InputPointerData *merged;

        /* A sum a delta cannot hold is not a delta. Rather than wrap a movement into its opposite, the
         * event is handed back: the caller drains, an empty slot takes it, and the two movements stay two
         * events. Reached only after 2^31 pixels of movement between two frames, and still stated. */
        if (sumX > (int64_t)INT32_MAX || sumX < (int64_t)INT32_MIN || sumY > (int64_t)INT32_MAX ||
            sumY < (int64_t)INT32_MIN) {
            coalescer->flushTotal++;
            *outAction = RoboInputCoalesceFlushFirst;
            return RoboOk;
        }

        /* The newest event, whole -- its position, mask, flags, plane and wheel are the answers a frame
         * draws -- with the deltas replaced by the window's sum. `previous` is left as the newest reported
         * it, so `position - previous` keeps saying "the last step" (this header's rule). */
        slot->event = *event;
        merged = RiCoalescePointerIn(&slot->event);
        merged->deltaX = (int32_t)sumX;
        merged->deltaY = (int32_t)sumY;
        slot->merged++;
        slot->lastMs = event->timestampMs;
        coalescer->mergedTotal++;
        coalescer->heldTotal++;
        *outAction = RoboInputCoalesceHold;
        return RoboOk;
    }

    slotIndex = RiCoalesceFreeSlot(coalescer);
    if (slotIndex < 0) {
        /* No room, so nothing is taken: the caller drains and offers this event again. A drop here would
         * turn a slow frame into a lost pointer. */
        coalescer->flushTotal++;
        *outAction = RoboInputCoalesceFlushFirst;
        return RoboOk;
    }

    {
        InputCoalesceSlot *slot = &coalescer->slots[(size_t)slotIndex];

        memset(slot, 0, sizeof(*slot));
        slot->active = 1;
        slot->device = event->device.id;
        slot->pointer = pointer->pointer;
        slot->type = event->type;
        slot->event = *event;
        slot->merged = 1u;
        slot->firstMs = event->timestampMs;
        slot->lastMs = event->timestampMs;
        coalescer->live++;
        coalescer->heldTotal++;
        *outAction = RoboInputCoalesceHold;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The flush                                                           */
/* ------------------------------------------------------------------ */

Result InputCoalescerFlush(InputCoalescer *coalescer,
                           InputQueue *queue,
                           uint32_t *outEmitted,
                           InputDiag *diag)
{
    uint32_t emitted = 0u;
    uint32_t index;

    if (outEmitted) {
        *outEmitted = 0u;
    }
    if (!coalescer || !queue) {
        return RoboInputDiag(diag, RoboInputDomainSession, RoboInvalidArgument,
                             "a flush needs a coalescer and a queue");
    }

    for (index = 0u; index < RoboInputCoalesceSlots; index++) {
        InputCoalesceSlot *slot = &coalescer->slots[index];
        Result result;

        if (!slot->active) {
            continue;
        }
        result = InputQueuePush(queue, &slot->event, diag);
        if (result != RoboOk) {
            /* The queue filled. What was pushed stays pushed and this slot stays held, so a later flush
             * finishes the job rather than the movement being lost. */
            if (outEmitted) {
                *outEmitted = emitted;
            }
            return result;
        }
        memset(slot, 0, sizeof(*slot));
        coalescer->live--;
        coalescer->emittedTotal++;
        emitted++;
    }

    if (outEmitted) {
        *outEmitted = emitted;
    }
    return RoboOk;
}

void InputCoalescerClear(InputCoalescer *coalescer)
{
    uint32_t index;

    if (!coalescer) {
        return;
    }
    for (index = 0u; index < RoboInputCoalesceSlots; index++) {
        if (coalescer->slots[index].active) {
            coalescer->refusedTotal++;
        }
        memset(&coalescer->slots[index], 0, sizeof(coalescer->slots[index]));
    }
    coalescer->live = 0u;
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

Result InputCoalescerWrite(const InputCoalescer *coalescer,
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
    if (!coalescer) {
        RiWriterText(&text, "coalescer <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "coalescer slots=");
    RiWriterU64(&text, (uint64_t)RoboInputCoalesceSlots);
    RiWriterFieldU64(&text, " live", (uint64_t)coalescer->live);
    RiWriterFieldU64(&text, " held", coalescer->heldTotal);
    RiWriterFieldU64(&text, " flush", coalescer->flushTotal);
    RiWriterFieldU64(&text, " merged", coalescer->mergedTotal);
    RiWriterFieldU64(&text, " emitted", coalescer->emittedTotal);
    RiWriterFieldU64(&text, " refused", coalescer->refusedTotal);
    RiWriterChar(&text, '\n');

    for (index = 0u; index < RoboInputCoalesceSlots; index++) {
        const InputCoalesceSlot *slot = &coalescer->slots[index];
        char line[RoboInputDumpLineMax];
        size_t lineLength = 0u;

        if (!slot->active) {
            continue;
        }
        RiWriterText(&text, "  slot ");
        RiWriterU64(&text, (uint64_t)index);
        RiWriterFieldU64(&text, " device", (uint64_t)slot->device);
        RiWriterFieldU64(&text, " pointer", (uint64_t)slot->pointer);
        RiWriterFieldText(&text, " type", InputEventTypeName(slot->type));
        RiWriterFieldU64(&text, " merged", (uint64_t)slot->merged);
        RiWriterFieldU64(&text, " first", slot->firstMs);
        RiWriterFieldU64(&text, " last", slot->lastMs);
        RiWriterChar(&text, ' ');
        if (InputEventWrite(&slot->event, line, sizeof(line), &lineLength, NULL) == RoboOk) {
            RiWriterText(&text, line);
        } else {
            /* The event dump's bound is asserted by its own suite, so this is unreachable while that holds;
             * saying so is cheaper than writing half a line and calling it a record. */
            RiWriterText(&text, "<event line does not fit its own bound>");
        }
        RiWriterChar(&text, '\n');
    }
    return RiWriterFinish(&text, outLength, diag);
}
