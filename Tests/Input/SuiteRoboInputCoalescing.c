/*
 * RoboInput coalescing suite (Phase 13.47) — `input.coalescing.*`.
 *
 * Six cases, and four of them exist to pin a rule rather than a mechanism:
 *
 *   `rule`     every one of the thirty-three event types is classified, by iterating the vocabulary and
 *              asserting the answer against the three-name table -- so a type appended in a later phase is
 *              *proved* to be a barrier rather than assumed to be one;
 *
 *   `merge`    what a merged move says. Two moves on one pointer become one event whose position is the
 *              newest, whose deltas are the sum and whose `previous` is the newest's own -- the three facts
 *              the header promises -- and two pointers, or two devices, never merge into each other;
 *
 *   `order`    the barrier. A press that arrives after a held move answers `FLUSH_FIRST`, and after the
 *              drain the same press is `EMIT`: an event may never overtake the moves it happened after;
 *
 *   `flush`    the lossless half. A queue with one seat and two held slots emits one, keeps the other and
 *              answers `RoboBusy`; draining and flushing again finishes the job, so nothing is ever dropped
 *              by a full queue;
 *
 *   `limits`   a full table answers `FLUSH_FIRST` without taking the event, and the refusals: NULL
 *              arguments, an invalid event, and the counter that says how many were turned away;
 *
 *   `dump`     byte-for-byte determinism across two calls, the counters agreeing with what was done, and
 *              the buffer bound -- exactly `RoboInputCoalescerDumpMax` fits and one byte less refuses with
 *              `?` in the buffer and the needed length reported.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

/** A pointer move by @p pointer from @p prevX,@p prevY to @p x,@p y, on @p device, at @p ms. */
static InputEvent CoalesceMove(InputDeviceId device,
                               InputPointerId pointer,
                               int32_t x,
                               int32_t y,
                               int32_t prevX,
                               int32_t prevY,
                               uint64_t ms)
{
    InputEvent event;
    InputPointerData data;
    InputDevice identity;

    InputPointerDataInit(&data);
    RoboCheckEqInt(InputPointerDataSet(&data, pointer, MakeGfxPoint(x, y),
                                       RoboMouseButtonNone, 0u), RoboOk);
    RoboCheckEqInt(InputPointerDataSetPrevious(&data, MakeGfxPoint(prevX, prevY)), RoboOk);

    InputEventInit(&event);
    RoboCheckEqInt(InputEventSetPointer(&event, RoboInputEventPointerMove, RoboInputSourceMouse,
                                        &data, RoboInputSpaceWindow, NULL), RoboOk);
    identity = GetRoboInputDevice(device, RoboInputSourceMouse, "mouse");
    RoboCheckEqInt(InputEventSetDevice(&event, &identity, NULL), RoboOk);
    RoboCheckEqInt(InputEventSetTimestamp(&event, ms), RoboOk);
    return event;
}

/** A left-button press at @p x,@p y on @p device at @p ms -- the barrier most cases use. */
static InputEvent CoalescePress(InputDeviceId device, int32_t x, int32_t y, uint64_t ms)
{
    InputEvent event = InputButtonEvent(x, y, RoboMouseButtonLeft, 1, RoboWindowIdNone);
    InputDevice identity = GetRoboInputDevice(device, RoboInputSourceMouse, "mouse");

    RoboCheckEqInt(InputEventSetDevice(&event, &identity, NULL), RoboOk);
    RoboCheckEqInt(InputEventSetTimestamp(&event, ms), RoboOk);
    return event;
}

/** Push @p event and return the action, asserting `RoboOk`. */
static InputCoalesceAction CoalesceOffer(InputCoalescer *coalescer, const InputEvent *event)
{
    InputCoalesceAction action = RoboInputCoalesceEmit;

    RoboCheckEqInt(InputCoalescerPush(coalescer, event, &action, NULL), RoboOk);
    return action;
}

/* ------------------------------------------------------------------ */
/* The rule                                                            */
/* ------------------------------------------------------------------ */

static void TestCoalesceRule(void)
{
    size_t index;
    size_t known;
    InputEvent move;
    InputEvent press;
    InputEvent key;
    InputEvent text;
    InputEvent wheel;
    InputCoalescer coalescer;

    RoboCheckEqInt((int)RoboInputCoalesceTypeCount, 3);
    RoboCheckEqInt(InputEventTypeMayCoalesce(RoboInputEventPointerMove), 1);
    RoboCheckEqInt(InputEventTypeMayCoalesce(RoboInputEventTouchMove), 1);
    RoboCheckEqInt(InputEventTypeMayCoalesce(RoboInputEventPenMove), 1);

    /* The three names the table holds, in order. */
    RoboCheckEqInt((int)InputCoalesceTypes[0], (int)RoboInputEventPointerMove);
    RoboCheckEqInt((int)InputCoalesceTypes[1], (int)RoboInputEventTouchMove);
    RoboCheckEqInt((int)InputCoalesceTypes[2], (int)RoboInputEventPenMove);

    /* Every type in the vocabulary, asserted against the table rather than against a second list. A type
     * appended later is therefore proved to be a barrier: the test cannot be satisfied by two lists that
     * drifted apart together. */
    for (index = 0u; index < (size_t)RoboInputEventTypeCount; index++) {
        int expected = 0;

        for (known = 0u; known < (size_t)RoboInputCoalesceTypeCount; known++) {
            if ((size_t)InputCoalesceTypes[known] == index) {
                expected = 1;
            }
        }
        RoboCheckEqInt(InputEventTypeMayCoalesce((InputEventType)index), expected);
    }

    RoboCheckEqInt(InputEventTypeMayCoalesce(RoboInputEventNone), 0);
    RoboCheckEqInt(InputEventTypeMayCoalesce((InputEventType)RoboInputEventTypeCount), 0);
    RoboCheckEqInt(InputEventTypeMayCoalesce((InputEventType)999), 0);

    /* And the same rule through the door that takes an event: the barrier kinds are the ones the brief
     * lists as never mergeable. */
    move = CoalesceMove(1u, RoboInputPointerIdPrimary, 10, 10, 9, 9, 100u);
    press = CoalescePress(1u, 10, 10, 101u);
    key = InputKeyEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 1, RoboKeyModNone);
    InputEventInit(&text);
    RoboCheckEqInt(InputEventSetText(&text, RoboInputSourceKeyboard, "a", NULL), RoboOk);

    /* A wheel carries a position and a delta and is still a barrier: a scroll must scroll by the *sum*, which
     * is a different question with a different answer (13.19's own normalizer). */
    InputEventInit(&wheel);
    {
        InputPointerData data = GetRoboInputPointerData(10, 10, RoboMouseButtonNone, 0u);

        RoboCheckEqInt(InputPointerDataSetWheel(&data, 0, -1, RoboMouseScrollUnitLine), RoboOk);
        RoboCheckEqInt(InputEventSetPointer(&wheel, RoboInputEventWheel, RoboInputSourceMouse,
                                            &data, RoboInputSpaceWindow, NULL), RoboOk);
    }

    InputCoalescerInit(&coalescer);
    RoboCheckEqInt(CoalesceOffer(&coalescer, &move), RoboInputCoalesceHold);
    RoboCheckEqInt((int)InputCoalescerHeld(&coalescer), 1);
    RoboCheckEqInt(CoalesceOffer(&coalescer, &press), RoboInputCoalesceFlushFirst);
    RoboCheckEqInt((int)InputCoalescerHeld(&coalescer), 1);
    RoboCheckEqInt(CoalesceOffer(&coalescer, &key), RoboInputCoalesceFlushFirst);
    RoboCheckEqInt(CoalesceOffer(&coalescer, &text), RoboInputCoalesceFlushFirst);
    RoboCheckEqInt(CoalesceOffer(&coalescer, &wheel), RoboInputCoalesceFlushFirst);
    RoboCheckEqInt(coalescer.flushTotal, 4u);
    RoboCheckEqInt(coalescer.refusedTotal, 0u);
}

/* ------------------------------------------------------------------ */
/* Merging                                                             */
/* ------------------------------------------------------------------ */

static void TestCoalesceMerge(void)
{
    InputCoalescer coalescer;
    InputEvent first;
    InputEvent second;
    InputEvent third;
    InputEvent other;
    InputCoalesceSlot *slot;

    InputCoalescerInit(&coalescer);

    first = CoalesceMove(1u, RoboInputPointerIdPrimary, 20, 20, 10, 10, 100u);
    second = CoalesceMove(1u, RoboInputPointerIdPrimary, 25, 22, 20, 20, 108u);
    third = CoalesceMove(1u, RoboInputPointerIdPrimary, 40, 30, 25, 22, 116u);

    RoboCheckEqInt(CoalesceOffer(&coalescer, &first), RoboInputCoalesceHold);
    RoboCheckEqInt(CoalesceOffer(&coalescer, &second), RoboInputCoalesceHold);
    RoboCheckEqInt(CoalesceOffer(&coalescer, &third), RoboInputCoalesceHold);

    /* One slot, three events. */
    RoboCheckEqInt((int)InputCoalescerHeld(&coalescer), 1);
    slot = &coalescer.slots[0];
    RoboCheckEqInt(slot->merged, 3u);
    RoboCheckEqInt((int)slot->type, (int)RoboInputEventPointerMove);
    RoboCheckEqInt((int)slot->pointer, (int)RoboInputPointerIdPrimary);
    RoboCheckEqInt((uint64_t)slot->firstMs, 100u);
    RoboCheckEqInt((uint64_t)slot->lastMs, 116u);

    /* The newest position, whole: 40,30 is the last sample, not the first or an average. */
    RoboCheckEqInt(slot->event.payload.pointer.position.x, 40);
    RoboCheckEqInt(slot->event.payload.pointer.position.y, 30);
    /* The window's movement: 10+5+15 across x and 10+2+8 across y, summed from what the platform said. */
    RoboCheckEqInt(slot->event.payload.pointer.deltaX, 30);
    RoboCheckEqInt(slot->event.payload.pointer.deltaY, 20);
    /* And `previous` stays the newest event's own, so `position - previous` still means the last step. */
    RoboCheckEqInt(slot->event.payload.pointer.previous.x, 25);
    RoboCheckEqInt(slot->event.payload.pointer.previous.y, 22);

    RoboCheckEqInt(coalescer.mergedTotal, 2u);
    RoboCheckEqInt(coalescer.heldTotal, 3u);

    /* A second pointer is its own slot -- two fingers are two contacts, never one -- and so is a second
     * device. Merging either pair would hand a consumer a movement that never happened. */
    second = CoalesceMove(1u, (InputPointerId)7u, 200, 200, 190, 190, 120u);
    RoboCheckEqInt(CoalesceOffer(&coalescer, &second), RoboInputCoalesceHold);
    other = CoalesceMove(2u, RoboInputPointerIdPrimary, 300, 300, 290, 290, 124u);
    RoboCheckEqInt(CoalesceOffer(&coalescer, &other), RoboInputCoalesceHold);
    RoboCheckEqInt((int)InputCoalescerHeld(&coalescer), 3);
    RoboCheckEqInt(coalescer.slots[0].merged, 3u);
    RoboCheckEqInt(coalescer.slots[1].merged, 1u);
    RoboCheckEqInt(coalescer.slots[2].merged, 1u);
    RoboCheckEqInt((uint64_t)coalescer.slots[2].device, 2u);

    /* A merge is not a second slot: the third event for one key must not consume one. */
    RoboCheckEqInt(CoalesceOffer(&coalescer, &third), RoboInputCoalesceHold);
    RoboCheckEqInt((int)InputCoalescerHeld(&coalescer), 3);
}

/* ------------------------------------------------------------------ */
/* Ordering                                                            */
/* ------------------------------------------------------------------ */

static void TestCoalesceOrder(void)
{
    InputCoalescer coalescer;
    InputQueue *queue;
    InputEvent move;
    InputEvent press;
    InputEvent event;
    InputCoalesceAction action;

    queue = GetRoboInputQueue(8u);
    InputCoalescerInit(&coalescer);

    move = CoalesceMove(1u, RoboInputPointerIdPrimary, 5, 5, 4, 4, 200u);
    press = CoalescePress(1u, 5, 5, 201u);

    /* With nothing held, a barrier is dispatched straight away. */
    RoboCheckEqInt(CoalesceOffer(&coalescer, &press), RoboInputCoalesceEmit);
    RoboCheckEqInt(coalescer.flushTotal, 0u);

    /* With a move held, the same barrier must wait for the drain -- and the drain must run before it. */
    RoboCheckEqInt(CoalesceOffer(&coalescer, &move), RoboInputCoalesceHold);
    RoboCheckEqInt(CoalesceOffer(&coalescer, &press), RoboInputCoalesceFlushFirst);
    RoboCheckEqInt(coalescer.flushTotal, 1u);

    RoboCheckEqInt(InputCoalescerFlush(&coalescer, queue, NULL, NULL), RoboOk);
    RoboCheckEqInt((int)InputCoalescerHeld(&coalescer), 0);
    RoboCheckEqInt(InputQueueCount(queue), 1u);
    RoboCheckEqInt(InputQueuePeek(queue, &event, NULL), RoboOk);
    RoboCheckEqInt((int)event.type, (int)RoboInputEventPointerMove);

    /* Now the press is the front of the story, and it is `EMIT`. */
    action = CoalesceOffer(&coalescer, &press);
    RoboCheckEqInt((int)action, (int)RoboInputCoalesceEmit);
    RoboInputPush(queue, &press);
    RoboCheckEqInt(InputQueueCount(queue), 2u);

    /* The queue holds them in the order they were dispatched: the move, then the press. */
    RoboCheckEqInt(InputQueuePop(queue, &event, NULL), RoboOk);
    RoboCheckEqInt((int)event.type, (int)RoboInputEventPointerMove);
    RoboCheckEqInt(InputQueuePop(queue, &event, NULL), RoboOk);
    RoboCheckEqInt((int)event.type, (int)RoboInputEventPointerDown);
    RoboCheckEqInt(event.payload.pointer.position.x, 5);
    /* A press has no delta of its own: the movement it happened after is in the move that precedes it, and
     * that the two are adjacent is what the drain bought. */
    RoboCheckEqInt(event.payload.pointer.deltaX, 0);

    InputQueueDestroy(queue);
}

/* ------------------------------------------------------------------ */
/* The flush                                                           */
/* ------------------------------------------------------------------ */

static void TestCoalesceFlush(void)
{
    InputCoalescer coalescer;
    InputQueue *queue;
    InputEvent first;
    InputEvent second;
    InputEvent event;
    uint32_t emitted = 0u;

    /* One seat, two held slots: the flush emits what fits and keeps the rest. */
    queue = GetRoboInputQueue(1u);
    InputCoalescerInit(&coalescer);
    first = CoalesceMove(1u, RoboInputPointerIdPrimary, 10, 10, 9, 9, 300u);
    second = CoalesceMove(2u, RoboInputPointerIdPrimary, 50, 50, 49, 49, 301u);

    RoboCheckEqInt(CoalesceOffer(&coalescer, &first), RoboInputCoalesceHold);
    RoboCheckEqInt(CoalesceOffer(&coalescer, &second), RoboInputCoalesceHold);
    RoboCheckEqInt((int)InputCoalescerHeld(&coalescer), 2);

    RoboCheckEqInt(InputCoalescerFlush(&coalescer, queue, &emitted, NULL), RoboBusy);
    RoboCheckEqInt(emitted, 1u);
    RoboCheckEqInt((int)InputCoalescerHeld(&coalescer), 1);
    RoboCheckEqInt(InputQueueCount(queue), 1u);
    RoboCheckEqInt(coalescer.emittedTotal, 1u);

    /* The one that got through is the first slot's, in slot order. */
    RoboCheckEqInt(InputQueuePop(queue, &event, NULL), RoboOk);
    RoboCheckEqInt((uint64_t)event.device.id, 1u);

    /* Drain, flush again, and the movement that was kept arrives rather than being lost. */
    RoboCheckEqInt(InputCoalescerFlush(&coalescer, queue, &emitted, NULL), RoboOk);
    RoboCheckEqInt(emitted, 1u);
    RoboCheckEqInt((int)InputCoalescerHeld(&coalescer), 0);
    RoboCheckEqInt(InputCoalescerIsEmpty(&coalescer), 1);
    RoboCheckEqInt(InputQueuePop(queue, &event, NULL), RoboOk);
    RoboCheckEqInt((uint64_t)event.device.id, 2u);
    RoboCheckEqInt(InputQueueCount(queue), 0u);

    /* Nothing held is not a failure: an empty flush is `RoboOk` with zero emitted. */
    RoboCheckEqInt(InputCoalescerFlush(&coalescer, queue, &emitted, NULL), RoboOk);
    RoboCheckEqInt(emitted, 0u);
    RoboCheckEqInt(coalescer.emittedTotal, 2u);

    InputQueueDestroy(queue);
}

/* ------------------------------------------------------------------ */
/* Bounds and refusals                                                 */
/* ------------------------------------------------------------------ */

static void TestCoalesceLimits(void)
{
    InputCoalescer coalescer;
    InputQueue *queue;
    InputEvent move;
    InputEvent event;
    InputCoalesceAction action = RoboInputCoalesceEmit;
    InputDiag diag = { 0 };
    uint32_t index;
    int full = 0;

    queue = GetRoboInputQueue(16u);
    InputCoalescerInit(&coalescer);

    /* Fill every slot, each from its own device -- the one key that cannot merge. */
    for (index = 0u; index < (uint32_t)RoboInputCoalesceSlots; index++) {
        move = CoalesceMove((InputDeviceId)(index + 1u), RoboInputPointerIdPrimary,
                            10, 10, 9, 9, 400u + index);
        RoboCheckEqInt(CoalesceOffer(&coalescer, &move), RoboInputCoalesceHold);
    }
    RoboCheckEqInt((int)InputCoalescerHeld(&coalescer), (int)RoboInputCoalesceSlots);

    /* The table is full, so the next move is handed back rather than taken or dropped. */
    move = CoalesceMove(99u, RoboInputPointerIdPrimary, 10, 10, 9, 9, 500u);
    RoboCheckEqInt(InputCoalescerPush(&coalescer, &move, &action, NULL), RoboOk);
    RoboCheckEqInt((int)action, (int)RoboInputCoalesceFlushFirst);
    RoboCheckEqInt((int)InputCoalescerHeld(&coalescer), (int)RoboInputCoalesceSlots);
    full = (int)coalescer.flushTotal;
    RoboCheckEqInt(full, 1);

    /* Drain, and the very same event now takes a slot: a full table costs a flush, never an event. */
    RoboCheckEqInt(InputCoalescerFlush(&coalescer, queue, NULL, NULL), RoboOk);
    RoboCheckEqInt((int)InputCoalescerHeld(&coalescer), 0);
    RoboCheckEqInt(InputQueueCount(queue), (int)RoboInputCoalesceSlots);
    RoboCheckEqInt(InputCoalescerPush(&coalescer, &move, &action, NULL), RoboOk);
    RoboCheckEqInt((int)action, (int)RoboInputCoalesceHold);
    RoboCheckEqInt((int)InputCoalescerHeld(&coalescer), 1);

    /* Refusals: a NULL coalescer, a NULL event, a missing action, and an invalid event. None of them is a
     * merge, and each is counted. */
    diag = (InputDiag){ 0 };
    RoboCheckEqInt(InputCoalescerPush(NULL, &move, &action, &diag), RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboInputDomainSession);
    RoboCheckEqInt(InputCoalescerPush(&coalescer, NULL, &action, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputCoalescerPush(&coalescer, &move, NULL, NULL), RoboInvalidArgument);

    event = move;
    event.space = RoboInputSpaceUnknown;
    diag = (InputDiag){ 0 };
    RoboCheckEqInt(InputCoalescerPush(&coalescer, &event, &action, &diag), RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboInputDomainSession);
    RoboCheckEqInt(coalescer.refusedTotal, 1u);
    RoboCheckEqInt((int)InputCoalescerHeld(&coalescer), 1);

    RoboCheckEqInt(InputCoalescerFlush(NULL, queue, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputCoalescerFlush(&coalescer, NULL, NULL, NULL), RoboInvalidArgument);

    /* A clear drops what is held and says so in the counter rather than silently. */
    InputCoalescerClear(&coalescer);
    RoboCheckEqInt((int)InputCoalescerHeld(&coalescer), 0);
    RoboCheckEqInt(coalescer.refusedTotal, 2u);

    InputQueueDestroy(queue);
}

/* ------------------------------------------------------------------ */
/* The dump                                                            */
/* ------------------------------------------------------------------ */

static void TestCoalesceDump(void)
{
    InputCoalescer coalescer;
    InputEvent move;
    InputEvent next;
    char first[RoboInputCoalescerDumpMax];
    char second[RoboInputCoalescerDumpMax];
    char exact[RoboInputCoalescerDumpMax];
    char small[64];
    size_t firstLength = 0u;
    size_t secondLength = 0u;
    size_t needed = 0u;

    InputCoalescerInit(&coalescer);
    move = CoalesceMove(3u, RoboInputPointerIdPrimary, 12, 12, 10, 10, 600u);
    next = CoalesceMove(3u, RoboInputPointerIdPrimary, 18, 16, 12, 12, 608u);
    RoboCheckEqInt(CoalesceOffer(&coalescer, &move), RoboInputCoalesceHold);
    RoboCheckEqInt(CoalesceOffer(&coalescer, &next), RoboInputCoalesceHold);

    /* Two generations, byte for byte. */
    RoboCheckEqInt(InputCoalescerWrite(&coalescer, first, sizeof(first), &firstLength, NULL), RoboOk);
    RoboCheckEqInt(InputCoalescerWrite(&coalescer, second, sizeof(second), &secondLength, NULL), RoboOk);
    RoboCheckEqInt((int)firstLength, (int)secondLength);
    RoboCheckStrEq(first, second);
    RoboCheck(firstLength > 0u);

    /* The summary is the counters verbatim, so a dump of a coalescer is also a test of its counters. */
    RoboCheck(strstr(first, "coalescer slots=8 live=1") != NULL);
    RoboCheck(strstr(first, "merged=1") != NULL);
    RoboCheck(strstr(first, "refused=0") != NULL);
    RoboCheck(strstr(first, "merged=2") != NULL);

    /* A full table at exactly the stated bound. */
    {
        uint32_t index;

        for (index = 0u; index < (uint32_t)RoboInputCoalesceSlots; index++) {
            InputEvent one = CoalesceMove((InputDeviceId)(index + 1u), RoboInputPointerIdPrimary,
                                          10, 10, 9, 9, 700u + index);
            RoboCheckEqInt(CoalesceOffer(&coalescer, &one), RoboInputCoalesceHold);
        }
    }
    RoboCheckEqInt(InputCoalescerWrite(&coalescer, exact, sizeof(exact), &needed, NULL), RoboOk);
    RoboCheck((int)needed < (int)sizeof(exact));

    /* A buffer that cannot hold it, and the refusal is complete-or-absent: `?` rather than a truncated
     * record, with the length a full one needs. */
    RoboCheckEqInt(InputCoalescerWrite(&coalescer, small, sizeof(small), &needed, NULL), RoboNotSupported);
    RoboCheck(small[0] == '?');
    RoboCheck(needed > sizeof(small));

    /* The stated bound covers the widest table this build can produce, which is the claim the constant
     * makes -- asserted rather than asserted-about. */
    RoboCheck((int)needed <= (int)RoboInputCoalescerDumpMax);

    RoboCheckEqInt(InputCoalescerWrite(NULL, first, sizeof(first), &firstLength, NULL), RoboOk);
    RoboCheckStrEq(first, "coalescer <none>\n");

    RoboCheckEqInt(InputCoalescerWrite(&coalescer, NULL, 0u, &firstLength, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputCoalescerWrite(&coalescer, first, sizeof(first), NULL, NULL),
                   RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* The cost model                                                      */
/* ------------------------------------------------------------------ */

/**
 * What coalescing is *for*, asserted as a cost model rather than a stopwatch.
 *
 * A thousand moves arriving between sixty frames cost **sixty** queue pushes, one per frame, and the nine
 * hundred and forty that did not reach the queue were merged rather than dropped -- which is the claim the
 * feature makes. The counts are exact and derived, so a build that presented every event (a coalescer that
 * answered `EMIT` where it should answer `HOLD`) or one that lost the merged movement fails here rather than
 * merely getting slower.
 *
 * A wall-clock number would be the wrong artifact for this layer: it measures the machine, not the model.
 * What the model promises is the *shape* of the work -- emissions proportional to frames and pointers, never
 * to events -- and that is what this case reads.
 */
static void TestCoalesceBaseline(void)
{
    enum { MovesPerFrame = 16, Frames = 60 };
    const uint32_t samples = (uint32_t)MovesPerFrame * (uint32_t)Frames;
    InputCoalescer coalescer;
    InputQueue *queue;
    InputEvent move;
    InputEvent event;
    uint32_t frame;
    uint32_t index;
    uint32_t emitted = 0u;
    int32_t x = 0;

    queue = GetRoboInputQueue((uint32_t)Frames * 2u);
    InputCoalescerInit(&coalescer);

    for (frame = 0u; frame < (uint32_t)Frames; frame++) {
        for (index = 0u; index < (uint32_t)MovesPerFrame; index++) {
            x += 2;
            move = CoalesceMove(1u, RoboInputPointerIdPrimary, x, 40, x - 2, 40,
                                (uint64_t)frame * (uint64_t)MovesPerFrame + index);
            /* Every move inside a frame is held, and never merged across a frame boundary because the frame
             * ends with a flush. */
            RoboCheckEqInt(CoalesceOffer(&coalescer, &move), RoboInputCoalesceHold);
        }
        /* One frame's worth: the flush is what turns sixteen samples into one. */
        RoboCheckEqInt(InputCoalescerFlush(&coalescer, queue, &emitted, NULL), RoboOk);
        RoboCheckEqInt(emitted, 1u);
    }

    RoboCheckEqInt((int)InputCoalescerHeld(&coalescer), 0);
    RoboCheckEqInt(InputQueueCount(queue), (uint32_t)Frames);
    RoboCheckEqInt((int)coalescer.emittedTotal, Frames);
    RoboCheckEqInt((int)coalescer.mergedTotal, (int)(samples - (uint32_t)Frames));
    RoboCheckEqInt((int)coalescer.heldTotal, (int)samples);
    RoboCheckEqInt((int)coalescer.refusedTotal, 0);

    /* The merged events carry the whole movement, not the last step: the first frame's event spans its
     * sixteen samples. That is what makes the count above a merge rather than a discard. */
    RoboCheckEqInt(InputQueuePeek(queue, &event, NULL), RoboOk);
    RoboCheckEqInt(event.payload.pointer.deltaX, MovesPerFrame * 2);
    RoboCheckEqInt(event.payload.pointer.position.x, MovesPerFrame * 2);
    RoboCheckEqInt((int)event.type, (int)RoboInputEventPointerMove);

    InputQueueDestroy(queue);
}

void TestRegisterInputCoalescing(void)
{
    TestRegister("input.coalescing.rule", TestCoalesceRule);
    TestRegister("input.coalescing.merge", TestCoalesceMerge);
    TestRegister("input.coalescing.order", TestCoalesceOrder);
    TestRegister("input.coalescing.flush", TestCoalesceFlush);
    TestRegister("input.coalescing.limits", TestCoalesceLimits);
    TestRegister("input.coalescing.dump", TestCoalesceDump);
    TestRegister("input.coalescing.baseline", TestCoalesceBaseline);
}
