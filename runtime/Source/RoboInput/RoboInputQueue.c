/*
 * RoboInput queue (Phase 12.1) — see RoboInputQueue.h.
 *
 * A ring buffer of events, and three decisions that are the whole of it:
 *
 *   **the storage is one allocation, made once.** A queue's capacity is fixed at creation, so `push` never
 *   allocates and can never fail for lack of memory -- which matters because a push happens on the path
 *   where an input event has just arrived and the honest answer to "there is no room" is `ROBO_BUSY`, not a
 *   half-translated event;
 *
 *   **a full queue refuses.** Nothing is evicted, and the refusal is counted, so a consumer that is behind
 *   can see that it is behind. Silently dropping the oldest event is how a runtime loses a keystroke nobody
 *   can prove was ever sent;
 *
 *   **counters never lie about what happened.** `pushed`, `popped`, `dropped` and `cleared` count events
 *   that crossed the boundary in each direction; `high_water` is the largest the queue ever held. All five
 *   are functions of the calls made, so two runs of the same sequence report identical statistics, which is
 *   what the determinism case in the suite asserts.
 */

#include "RoboInput/RoboInputQueue.h"

#include "RoboInputInternal.h"

#include "RoboMem/RoboMem.h"

#include <stddef.h>
#include <stdint.h>

struct robo_input_queue {
    /** Owned: `capacity` events, allocated once at creation. */
    robo_input_event_t *events;
    uint32_t capacity;
    /** Index of the front event; the ring's only moving parts are these two and @ref count. */
    uint32_t head;
    uint32_t count;
    uint64_t pushed;
    uint64_t popped;
    uint64_t dropped;
    uint64_t cleared;
    uint32_t high_water;
};

void robo_input_queue_config_init(robo_input_queue_config_t *config)
{
    if (!config) {
        return;
    }
    config->capacity = ROBO_INPUT_QUEUE_CAPACITY_DEFAULT;
}

int robo_input_queue_is_live(const robo_input_queue_t *queue)
{
    return queue != NULL && queue->events != NULL && queue->capacity > 0u;
}

robo_result_t robo_input_queue_create(const robo_input_queue_config_t *config,
                                      robo_input_queue_t **out,
                                      robo_input_diag_t *diag)
{
    robo_input_queue_t *queue;
    uint32_t capacity = ROBO_INPUT_QUEUE_CAPACITY_DEFAULT;

    if (!out) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_QUEUE, ROBO_INVALID_ARGUMENT,
                               "a queue needs somewhere to be put");
    }
    *out = NULL;
    if (config && config->capacity > 0u) {
        capacity = config->capacity;
    }
    if (capacity > ROBO_INPUT_QUEUE_CAPACITY_MAX) {
        /* Refused rather than clamped: a caller that asked for more and got the maximum would have a
         * bounded queue it believed was unbounded. */
        robo_log_debug("robo_input: queue capacity %u refused, maximum is %u", (unsigned)capacity,
                       (unsigned)ROBO_INPUT_QUEUE_CAPACITY_MAX);
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_QUEUE, ROBO_INVALID_ARGUMENT,
                               "queue capacity is above this build's maximum");
    }

    queue = (robo_input_queue_t *)robo_calloc(1u, sizeof(robo_input_queue_t));
    if (!queue) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_QUEUE, ROBO_OUT_OF_MEMORY,
                               "the queue record could not be allocated");
    }
    queue->events = (robo_input_event_t *)robo_calloc((size_t)capacity, sizeof(robo_input_event_t));
    if (!queue->events) {
        robo_free(queue);
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_QUEUE, ROBO_OUT_OF_MEMORY,
                               "the queue storage could not be allocated");
    }
    queue->capacity = capacity;
    *out = queue;
    robo_log_debug("robo_input: queue created capacity=%u event_bytes=%u", (unsigned)capacity,
                   (unsigned)sizeof(robo_input_event_t));
    return ROBO_OK;
}

void robo_input_queue_destroy(robo_input_queue_t *queue)
{
    if (!queue) {
        return;
    }
    robo_free(queue->events);
    robo_free(queue);
}

uint32_t robo_input_queue_capacity(const robo_input_queue_t *queue)
{
    return queue ? queue->capacity : 0u;
}

uint32_t robo_input_queue_count(const robo_input_queue_t *queue)
{
    return queue ? queue->count : 0u;
}

int robo_input_queue_is_empty(const robo_input_queue_t *queue)
{
    return queue == NULL || queue->count == 0u;
}

int robo_input_queue_is_full(const robo_input_queue_t *queue)
{
    return queue != NULL && queue->count == queue->capacity;
}

robo_result_t robo_input_queue_stats(const robo_input_queue_t *queue, robo_input_queue_stats_t *out)
{
    if (!queue || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->capacity = queue->capacity;
    out->count = queue->count;
    out->pushed = queue->pushed;
    out->popped = queue->popped;
    out->dropped = queue->dropped;
    out->cleared = queue->cleared;
    out->high_water = queue->high_water;
    return ROBO_OK;
}

robo_result_t robo_input_queue_push(robo_input_queue_t *queue,
                                    const robo_input_event_t *event,
                                    robo_input_diag_t *diag)
{
    uint32_t slot;

    if (!robo_input_queue_is_live(queue) || !event) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_QUEUE, ROBO_INVALID_ARGUMENT,
                               "a push needs a live queue and an event");
    }
    if (!robo_input_event_is_valid(event)) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_EVENT, ROBO_INVALID_ARGUMENT,
                               "the event is not one this build may queue");
    }
    if (queue->count >= queue->capacity) {
        queue->dropped++;
        robo_log_debug("robo_input: queue full, event dropped type=%s capacity=%u",
                       robo_input_event_type_name(event->type), (unsigned)queue->capacity);
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_QUEUE, ROBO_BUSY, "the queue is full");
    }

    slot = (queue->head + queue->count) % queue->capacity;
    /* A *copy*: the caller may reuse its event the instant this returns (the ownership answer the module
     * states), and the queue owns the copy from here on. */
    queue->events[slot] = *event;
    queue->count++;
    queue->pushed++;
    if (queue->count > queue->high_water) {
        queue->high_water = queue->count;
    }
    return ROBO_OK;
}

robo_result_t robo_input_queue_pop(robo_input_queue_t *queue,
                                   robo_input_event_t *out,
                                   robo_input_diag_t *diag)
{
    if (!robo_input_queue_is_live(queue) || !out) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_QUEUE, ROBO_INVALID_ARGUMENT,
                               "a pop needs a live queue and somewhere to put the event");
    }
    if (queue->count == 0u) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_QUEUE, ROBO_NOT_FOUND, "the queue is empty");
    }
    *out = queue->events[queue->head];
    queue->head = (queue->head + 1u) % queue->capacity;
    queue->count--;
    queue->popped++;
    return ROBO_OK;
}

robo_result_t robo_input_queue_peek(const robo_input_queue_t *queue,
                                    robo_input_event_t *out,
                                    robo_input_diag_t *diag)
{
    if (!robo_input_queue_is_live(queue) || !out) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_QUEUE, ROBO_INVALID_ARGUMENT,
                               "a peek needs a live queue and somewhere to put the event");
    }
    if (queue->count == 0u) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_QUEUE, ROBO_NOT_FOUND, "the queue is empty");
    }
    *out = queue->events[queue->head];
    return ROBO_OK;
}

robo_result_t robo_input_queue_peek_at(const robo_input_queue_t *queue,
                                       uint32_t index,
                                       robo_input_event_t *out,
                                       robo_input_diag_t *diag)
{
    if (!robo_input_queue_is_live(queue) || !out) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_QUEUE, ROBO_INVALID_ARGUMENT,
                               "a peek needs a live queue and somewhere to put the event");
    }
    if (index >= queue->count) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_QUEUE, ROBO_NOT_FOUND,
                               "there is no queued event at that position");
    }
    *out = queue->events[(queue->head + index) % queue->capacity];
    return ROBO_OK;
}

void robo_input_queue_clear(robo_input_queue_t *queue)
{
    if (!robo_input_queue_is_live(queue)) {
        return;
    }
    queue->cleared += (uint64_t)queue->count;
    queue->head = 0u;
    queue->count = 0u;
}
