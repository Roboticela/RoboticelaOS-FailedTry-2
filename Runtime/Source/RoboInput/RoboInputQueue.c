/*
 * RoboInput queue (Phase 12.1) — see RoboInputQueue.h.
 *
 * A ring buffer of events, and three decisions that are the whole of it:
 *
 *   **the storage is one allocation, made once.** A queue's capacity is fixed at creation, so `push` never
 *   allocates and can never fail for lack of memory -- which matters because a push happens on the path
 *   where an input event has just arrived and the honest answer to "there is no room" is `RoboBusy`, not a
 *   half-translated event;
 *
 *   **a full queue refuses.** Nothing is evicted, and the refusal is counted, so a consumer that is behind
 *   can see that it is behind. Silently dropping the oldest event is how a runtime loses a keystroke nobody
 *   can prove was ever sent;
 *
 *   **counters never lie about what happened.** `pushed`, `popped`, `dropped` and `cleared` count events
 *   that crossed the boundary in each direction; `highWater` is the largest the queue ever held. All five
 *   are functions of the calls made, so two runs of the same sequence report identical statistics, which is
 *   what the determinism case in the suite asserts.
 */

#include "RoboInput/RoboInputQueue.h"

#include "RoboInputInternal.h"

#include "RoboMem/RoboMem.h"

#include <stddef.h>
#include <stdint.h>

struct RoboInputQueue {
    /** Owned: `capacity` events, allocated once at creation. */
    InputEvent *events;
    uint32_t capacity;
    /** Index of the front event; the ring's only moving parts are these two and @ref count. */
    uint32_t head;
    uint32_t count;
    uint64_t pushed;
    uint64_t popped;
    uint64_t dropped;
    uint64_t cleared;
    uint32_t highWater;
};

void InputQueueConfigInit(InputQueueConfig *config)
{
    if (!config) {
        return;
    }
    config->capacity = RoboInputQueueCapacityDefault;
}

int InputQueueIsLive(const InputQueue *queue)
{
    return queue != NULL && queue->events != NULL && queue->capacity > 0u;
}

Result InputQueueCreate(const InputQueueConfig *config,
                                      InputQueue **out,
                                      InputDiag *diag)
{
    InputQueue *queue;
    uint32_t capacity = RoboInputQueueCapacityDefault;

    if (!out) {
        return RoboInputDiag(diag, RoboInputDomainQueue, RoboInvalidArgument,
                               "a queue needs somewhere to be put");
    }
    *out = NULL;
    if (config && config->capacity > 0u) {
        capacity = config->capacity;
    }
    if (capacity > RoboInputQueueCapacityMax) {
        /* Refused rather than clamped: a caller that asked for more and got the maximum would have a
         * bounded queue it believed was unbounded. */
        LogDebug("robo_input: queue capacity %u refused, maximum is %u", (unsigned)capacity,
                       (unsigned)RoboInputQueueCapacityMax);
        return RoboInputDiag(diag, RoboInputDomainQueue, RoboInvalidArgument,
                               "queue capacity is above this build's maximum");
    }

    queue = (InputQueue *)RoboCalloc(1u, sizeof(InputQueue));
    if (!queue) {
        return RoboInputDiag(diag, RoboInputDomainQueue, RoboOutOfMemory,
                               "the queue record could not be allocated");
    }
    queue->events = (InputEvent *)RoboCalloc((size_t)capacity, sizeof(InputEvent));
    if (!queue->events) {
        RoboFree(queue);
        return RoboInputDiag(diag, RoboInputDomainQueue, RoboOutOfMemory,
                               "the queue storage could not be allocated");
    }
    queue->capacity = capacity;
    *out = queue;
    LogDebug("robo_input: queue created capacity=%u event_bytes=%u", (unsigned)capacity,
                   (unsigned)sizeof(InputEvent));
    return RoboOk;
}

void InputQueueDestroy(InputQueue *queue)
{
    if (!queue) {
        return;
    }
    RoboFree(queue->events);
    RoboFree(queue);
}

uint32_t InputQueueCapacity(const InputQueue *queue)
{
    return queue ? queue->capacity : 0u;
}

uint32_t InputQueueCount(const InputQueue *queue)
{
    return queue ? queue->count : 0u;
}

int InputQueueIsEmpty(const InputQueue *queue)
{
    return queue == NULL || queue->count == 0u;
}

int InputQueueIsFull(const InputQueue *queue)
{
    return queue != NULL && queue->count == queue->capacity;
}

Result GetInputQueueStats(const InputQueue *queue, InputQueueStats *out)
{
    if (!queue || !out) {
        return RoboInvalidArgument;
    }
    out->capacity = queue->capacity;
    out->count = queue->count;
    out->pushed = queue->pushed;
    out->popped = queue->popped;
    out->dropped = queue->dropped;
    out->cleared = queue->cleared;
    out->highWater = queue->highWater;
    return RoboOk;
}

Result InputQueuePush(InputQueue *queue,
                                    const InputEvent *event,
                                    InputDiag *diag)
{
    uint32_t slot;

    if (!InputQueueIsLive(queue) || !event) {
        return RoboInputDiag(diag, RoboInputDomainQueue, RoboInvalidArgument,
                               "a push needs a live queue and an event");
    }
    if (!InputEventIsValid(event)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                               "the event is not one this build may queue");
    }
    if (queue->count >= queue->capacity) {
        queue->dropped++;
        LogDebug("robo_input: queue full, event dropped type=%s capacity=%u",
                       InputEventTypeName(event->type), (unsigned)queue->capacity);
        return RoboInputDiag(diag, RoboInputDomainQueue, RoboBusy, "the queue is full");
    }

    slot = (queue->head + queue->count) % queue->capacity;
    /* A *copy*: the caller may reuse its event the instant this returns (the ownership answer the module
     * states), and the queue owns the copy from here on. */
    queue->events[slot] = *event;
    queue->count++;
    queue->pushed++;
    if (queue->count > queue->highWater) {
        queue->highWater = queue->count;
    }
    return RoboOk;
}

Result InputQueuePop(InputQueue *queue,
                                   InputEvent *out,
                                   InputDiag *diag)
{
    if (!InputQueueIsLive(queue) || !out) {
        return RoboInputDiag(diag, RoboInputDomainQueue, RoboInvalidArgument,
                               "a pop needs a live queue and somewhere to put the event");
    }
    if (queue->count == 0u) {
        return RoboInputDiag(diag, RoboInputDomainQueue, RoboNotFound, "the queue is empty");
    }
    *out = queue->events[queue->head];
    queue->head = (queue->head + 1u) % queue->capacity;
    queue->count--;
    queue->popped++;
    return RoboOk;
}

Result InputQueuePeek(const InputQueue *queue,
                                    InputEvent *out,
                                    InputDiag *diag)
{
    if (!InputQueueIsLive(queue) || !out) {
        return RoboInputDiag(diag, RoboInputDomainQueue, RoboInvalidArgument,
                               "a peek needs a live queue and somewhere to put the event");
    }
    if (queue->count == 0u) {
        return RoboInputDiag(diag, RoboInputDomainQueue, RoboNotFound, "the queue is empty");
    }
    *out = queue->events[queue->head];
    return RoboOk;
}

Result InputQueuePeekAt(const InputQueue *queue,
                                       uint32_t index,
                                       InputEvent *out,
                                       InputDiag *diag)
{
    if (!InputQueueIsLive(queue) || !out) {
        return RoboInputDiag(diag, RoboInputDomainQueue, RoboInvalidArgument,
                               "a peek needs a live queue and somewhere to put the event");
    }
    if (index >= queue->count) {
        return RoboInputDiag(diag, RoboInputDomainQueue, RoboNotFound,
                               "there is no queued event at that position");
    }
    *out = queue->events[(queue->head + index) % queue->capacity];
    return RoboOk;
}

void InputQueueClear(InputQueue *queue)
{
    if (!InputQueueIsLive(queue)) {
        return;
    }
    queue->cleared += (uint64_t)queue->count;
    queue->head = 0u;
    queue->count = 0u;
}
