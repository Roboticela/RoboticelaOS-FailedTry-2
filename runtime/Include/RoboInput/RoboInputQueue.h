/*
 * RoboInput queue (Phase 12.1) — an owned FIFO of events, and nothing else.
 *
 * ## Why a queue at all, when the window layer already has one
 *
 * RoboWindow's queue is **one per runtime** and lives inside the module (7.9): `robo_window_event_push`,
 * `robo_window_event_poll`, and three global functions that answer "is it empty" about *the* queue. That is
 * the right shape for a window system whose backends deliver into one place, and it is the wrong shape here,
 * for two reasons:
 *
 *   **A queue is now an object.** Input has more than one possible consumer -- a dispatcher draining one per
 *   frame, a test holding a scripted sequence, a replay harness draining a recorded one -- and a single
 *   module-wide queue would force all of them to share a cursor and to be loaded in a fixed order. So a
 *   queue is created, owned, and destroyed by whoever wants it, and this module holds **no global state
 *   whatsoever**: there is no `robo_input_init` to call, no process-wide queue to clear between tests, and
 *   no lock, because two queues simply do not see each other.
 *
 *   **Ownership is explicit.** `push` copies the event *into* the queue and `pop` copies one *out*, so a
 *   caller may reuse, mutate or free its own event the instant `push` returns. The event model is a value
 *   (RoboInputEvent.h), which is what makes that a copy rather than a lifetime agreement, and the same
 *   philosophy RoboPaint's records and the display list already use.
 *
 * ## Overflow: refuse, and count it
 *
 * A full queue returns `ROBO_BUSY` and the event is **not** stored -- the same answer RoboWindow's queue
 * gives when its depth is reached, and the only one that does not lose information silently. A queue that
 * overwrote the oldest event would be a runtime that drops a keypress while nobody is looking, and the
 * debugging session it produces is the worst kind: everything looks fine and something is missing.
 *
 * So the refusal is *loud in the statistics*: `robo_input_queue_stats_t.dropped` counts every push that was
 * refused, which is how a consumer tells the difference between "no input arrived" and "I am not draining
 * fast enough". Nothing is ever evicted to make room.
 *
 * ## Bounds
 *
 * Capacity is chosen at creation (`ROBO_INPUT_QUEUE_CAPACITY_DEFAULT` is 128, matching the window layer's
 * own depth; `ROBO_INPUT_QUEUE_CAPACITY_MAX` is 1024 and bounds one allocation rather than expressing a
 * policy). Zero means the default, and a capacity above the maximum is refused rather than clamped: a
 * caller that asked for 10,000 events and silently got 1,024 would have a bounded queue it believed was
 * unbounded.
 *
 * ## Ordering and determinism
 *
 * Strict FIFO, no priorities, no coalescing, no filtering, and no clock: the queue never looks at an event's
 * timestamp, so a sequence pushed in an order comes out in that order regardless of the times it carries. A
 * sequence number is **not** stamped on events either -- the queue's own `pushed`/`popped` counters are what
 * a caller compares when it wants to prove which events it has seen, and stamping an event would make two
 * identical events unequal, which breaks the one property tests rely on most.
 */
#ifndef ROBO_INPUT_QUEUE_H
#define ROBO_INPUT_QUEUE_H

#include "RoboBase/RoboError.h"

#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputEvent.h"
#include "RoboInput/RoboInputTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** An owned FIFO of input events. Opaque: the storage and the cursors are the module's business. */
typedef struct robo_input_queue robo_input_queue_t;

/** How a queue is created. */
typedef struct robo_input_queue_config {
    /** Events the queue can hold; 0 means @ref ROBO_INPUT_QUEUE_CAPACITY_DEFAULT. */
    uint32_t capacity;
} robo_input_queue_config_t;

/** The default configuration: the default capacity, refusal on overflow. */
void robo_input_queue_config_init(robo_input_queue_config_t *config);

/**
 * What a queue has done, since it was created.
 *
 * All counters are monotonic except @ref count / @ref capacity, which are read through
 * `robo_input_queue_count` and `robo_input_queue_capacity`; these two are repeated here so that one call
 * answers "how is it doing" for a developer. Nothing here is time-based, so two runs that push the same
 * sequence of events report identical statistics -- which is what makes them usable in a determinism test.
 */
typedef struct robo_input_queue_stats {
    /** Capacity in events. */
    uint32_t capacity;
    /** Events currently held. */
    uint32_t count;
    /** Events accepted, ever. */
    uint64_t pushed;
    /** Events returned by `pop`, ever. */
    uint64_t popped;
    /** Pushes refused because the queue was full, ever. */
    uint64_t dropped;
    /** Events discarded by `clear`, ever. */
    uint64_t cleared;
    /** The largest @ref count ever reached. */
    uint32_t high_water;
} robo_input_queue_stats_t;

/**
 * Create a queue.
 *
 * @param config may be NULL (the default configuration); a capacity above
 *               `ROBO_INPUT_QUEUE_CAPACITY_MAX` is refused with `ROBO_INVALID_ARGUMENT` and a `QUEUE`
 *               diagnostic naming the bound.
 * @param out    non-NULL; receives the queue, or NULL on failure.
 */
robo_result_t robo_input_queue_create(const robo_input_queue_config_t *config,
                                      robo_input_queue_t **out,
                                      robo_input_diag_t *diag);

/** Destroy a queue and its storage. NULL is a no-op. Queued events are value copies: nothing else is freed
 *  and no event is "notified" of anything. */
void robo_input_queue_destroy(robo_input_queue_t *queue);

/** Non-zero when @p queue is a live queue. Every door below tolerates NULL by refusing. */
int robo_input_queue_is_live(const robo_input_queue_t *queue);

/* ---- Capacity and contents ---- */

/** Events the queue can hold; 0 for NULL. */
uint32_t robo_input_queue_capacity(const robo_input_queue_t *queue);

/** Events currently held; 0 for NULL. */
uint32_t robo_input_queue_count(const robo_input_queue_t *queue);

/** Non-zero when the queue holds nothing. **A NULL queue reads as empty**, which is the safe answer for a
 *  caller probing before it commits to draining. */
int robo_input_queue_is_empty(const robo_input_queue_t *queue);

/** Non-zero when the queue holds its capacity. A NULL queue reads as not full. */
int robo_input_queue_is_full(const robo_input_queue_t *queue);

/** Fill @p out with the queue's statistics. A NULL @p queue or @p out leaves @p out untouched and returns
 *  `ROBO_INVALID_ARGUMENT`. */
robo_result_t robo_input_queue_stats(const robo_input_queue_t *queue, robo_input_queue_stats_t *out);

/* ---- The three operations, plus a look ---- */

/**
 * Copy @p event onto the back of the queue.
 *
 * Refused with `ROBO_INVALID_ARGUMENT` and an `EVENT` diagnostic for an event that is not valid, with
 * `ROBO_BUSY` and a `QUEUE` diagnostic for a full queue (and `dropped` is incremented, which is the only
 * trace of it -- nothing is evicted).
 */
robo_result_t robo_input_queue_push(robo_input_queue_t *queue,
                                    const robo_input_event_t *event,
                                    robo_input_diag_t *diag);

/**
 * Copy the front event into @p out and remove it. Empty -> `ROBO_NOT_FOUND` and @p out is left untouched
 * (so a caller cannot mistake a stale value for a dequeued one).
 */
robo_result_t robo_input_queue_pop(robo_input_queue_t *queue,
                                   robo_input_event_t *out,
                                   robo_input_diag_t *diag);

/** Copy the front event into @p out **without** removing it. Empty -> `ROBO_NOT_FOUND`. */
robo_result_t robo_input_queue_peek(const robo_input_queue_t *queue,
                                    robo_input_event_t *out,
                                    robo_input_diag_t *diag);

/**
 * Copy the event @p index places behind the front into @p out, without removing anything.
 *
 * Indexing from the *front* rather than into the storage, so the number a caller writes is the position in
 * the FIFO order it sees: `0` is what `peek` answers, and an index past the last event is `ROBO_NOT_FOUND`.
 * This exists for the debug dump and for tests that want to state a whole sequence -- a caller that wants to
 * consume events uses `pop`.
 */
robo_result_t robo_input_queue_peek_at(const robo_input_queue_t *queue,
                                       uint32_t index,
                                       robo_input_event_t *out,
                                       robo_input_diag_t *diag);

/**
 * Discard every event, leaving the capacity and the counters' history intact (`cleared` grows).
 *
 * A NULL queue is a no-op. Nothing is copied or freed per event: the events are values.
 */
void robo_input_queue_clear(robo_input_queue_t *queue);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_QUEUE_H */
