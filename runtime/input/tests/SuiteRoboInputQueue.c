/*
 * RoboInput queue suite (Phase 12.1) — `input.queue.*`.
 *
 * Strict FIFO, explicit ownership, deterministic statistics, and one behaviour at the bound: a full queue
 * **refuses** rather than evicting. Each of those is a case here, because each is a decision a caller can
 * depend on and therefore a decision that has to be pinned.
 *
 * The queue is the only object this module allocates, so the lifecycle case also reads the allocator's live
 * count: create/destroy round trips have to return exactly what they took, or an input queue would be a leak
 * in a runtime that runs for days.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* Creation and destruction                                            */
/* ------------------------------------------------------------------ */

static void test_queue_lifecycle(void)
{
    robo_input_queue_config_t config;
    robo_input_queue_t *queue = NULL;
    robo_input_queue_t *custom = NULL;
    robo_input_queue_stats_t stats;
    size_t baseline;

    robo_input_queue_config_init(&config);
    ROBO_CHECK_EQ_INT(config.capacity, ROBO_INPUT_QUEUE_CAPACITY_DEFAULT);
    robo_input_queue_config_init(NULL);

    /* A NULL configuration means the defaults, and a zero capacity does too. */
    ROBO_CHECK_EQ_INT(robo_input_queue_create(NULL, &queue, NULL), ROBO_OK);
    ROBO_CHECK(robo_input_queue_is_live(queue));
    ROBO_CHECK_EQ_INT(robo_input_queue_capacity(queue), ROBO_INPUT_QUEUE_CAPACITY_DEFAULT);
    ROBO_CHECK_EQ_INT(robo_input_queue_count(queue), 0u);
    ROBO_CHECK_EQ_INT(robo_input_queue_is_empty(queue), 1);
    ROBO_CHECK_EQ_INT(robo_input_queue_is_full(queue), 0);
    ROBO_CHECK_EQ_INT(robo_input_queue_stats(queue, &stats), ROBO_OK);
    ROBO_CHECK_EQ_INT(stats.capacity, ROBO_INPUT_QUEUE_CAPACITY_DEFAULT);
    ROBO_CHECK_EQ_INT(stats.count, 0u);
    ROBO_CHECK_EQ_INT(stats.pushed, 0u);
    ROBO_CHECK_EQ_INT(stats.popped, 0u);
    ROBO_CHECK_EQ_INT(stats.dropped, 0u);
    ROBO_CHECK_EQ_INT(stats.cleared, 0u);
    ROBO_CHECK_EQ_INT(stats.high_water, 0u);
    robo_input_queue_destroy(queue);

    /* A named capacity is honored; a capacity above the maximum is *refused*, not clamped, because a caller
     * that asked for more and silently got the maximum would have a bounded queue it believed was
     * unbounded. */
    config.capacity = 4u;
    ROBO_CHECK_EQ_INT(robo_input_queue_create(&config, &custom, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_input_queue_capacity(custom), 4u);
    robo_input_queue_destroy(custom);

    config.capacity = ROBO_INPUT_QUEUE_CAPACITY_MAX + 1u;
    custom = NULL;
    {
        robo_input_diag_t diag;

        diag = (robo_input_diag_t){ 0 };
        ROBO_CHECK_EQ_INT(robo_input_queue_create(&config, &custom, &diag), ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_QUEUE);
        ROBO_CHECK(custom == NULL);
    }
    config.capacity = ROBO_INPUT_QUEUE_CAPACITY_MAX;
    ROBO_CHECK_EQ_INT(robo_input_queue_create(&config, &custom, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_input_queue_capacity(custom), ROBO_INPUT_QUEUE_CAPACITY_MAX);
    robo_input_queue_destroy(custom);

    /* NULL arguments, a NULL queue everywhere, and destruction of nothing. */
    ROBO_CHECK_EQ_INT(robo_input_queue_create(&config, NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_queue_is_live(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_input_queue_capacity(NULL), 0u);
    ROBO_CHECK_EQ_INT(robo_input_queue_count(NULL), 0u);
    ROBO_CHECK_EQ_INT(robo_input_queue_is_empty(NULL), 1);
    ROBO_CHECK_EQ_INT(robo_input_queue_is_full(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_input_queue_stats(NULL, &stats), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_queue_stats(NULL, NULL), ROBO_INVALID_ARGUMENT);
    robo_input_queue_clear(NULL);
    robo_input_queue_destroy(NULL);

    /* Create/destroy round trips take and return exactly the same blocks. */
    baseline = input_live_blocks();
    {
        int round;

        for (round = 0; round < 4; round++) {
            robo_input_queue_t *cycle = input_queue(8u);

            ROBO_CHECK(robo_input_queue_is_live(cycle));
            robo_input_queue_destroy(cycle);
            cycle = input_queue(32u);
            ROBO_CHECK_EQ_INT(robo_input_queue_capacity(cycle), 32u);
            robo_input_queue_destroy(cycle);
        }
    }
    ROBO_CHECK_EQ_INT((int)input_live_blocks(), (int)baseline);
}

/* ------------------------------------------------------------------ */
/* FIFO order                                                          */
/* ------------------------------------------------------------------ */

static void test_queue_fifo(void)
{
    robo_input_queue_t *queue = input_queue(8u);
    robo_input_queue_stats_t stats;
    robo_input_event_t first = input_pointer_move(1, 2, 0u, ROBO_INPUT_SPACE_SCREEN, 3u);
    robo_input_event_t second = input_button_event(3, 4, ROBO_MOUSE_BUTTON_LEFT, 1, 3u);
    robo_input_event_t third = input_key_event(ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A, 1,
                                               ROBO_INPUT_MODIFIERS_NONE);
    robo_input_event_t out;
    robo_input_diag_t diag;

    /* An empty queue answers "nothing" in both directions, and leaves the caller's record untouched so a
     * stale value cannot be mistaken for a dequeued one. */
    out = first;
    ROBO_CHECK_EQ_INT(robo_input_queue_peek(queue, &out, &diag), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_QUEUE);
    ROBO_CHECK_EQ_INT(out.type, first.type);
    ROBO_CHECK_EQ_INT(robo_input_queue_pop(queue, &out, &diag), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(out.type, first.type);
    ROBO_CHECK_EQ_INT(robo_input_queue_peek_at(queue, 0u, &out, &diag), ROBO_NOT_FOUND);

    input_push(queue, &first);
    input_push(queue, &second);
    input_push(queue, &third);
    ROBO_CHECK_EQ_INT(robo_input_queue_count(queue), 3u);
    ROBO_CHECK_EQ_INT(robo_input_queue_is_empty(queue), 0);
    ROBO_CHECK_EQ_INT(robo_input_queue_stats(queue, &stats), ROBO_OK);
    ROBO_CHECK_EQ_INT(stats.pushed, 3u);
    ROBO_CHECK_EQ_INT(stats.high_water, 3u);

    /* Peek is a look, not a take: three different event types read back in the order they went in. */
    ROBO_CHECK_EQ_INT(robo_input_queue_peek(queue, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_input_event_equal(&out, &first));
    ROBO_CHECK_EQ_INT(robo_input_queue_count(queue), 3u);
    ROBO_CHECK_EQ_INT(robo_input_queue_peek_at(queue, 0u, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_input_event_equal(&out, &first));
    ROBO_CHECK_EQ_INT(robo_input_queue_peek_at(queue, 1u, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_input_event_equal(&out, &second));
    ROBO_CHECK_EQ_INT(robo_input_queue_peek_at(queue, 2u, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_input_event_equal(&out, &third));
    ROBO_CHECK_EQ_INT(robo_input_queue_peek_at(queue, 3u, &out, &diag), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_QUEUE);

    ROBO_CHECK_EQ_INT(robo_input_queue_pop(queue, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_input_event_equal(&out, &first));
    ROBO_CHECK_EQ_INT(robo_input_queue_pop(queue, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_input_event_equal(&out, &second));
    ROBO_CHECK_EQ_INT(robo_input_queue_pop(queue, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_input_event_equal(&out, &third));
    ROBO_CHECK_EQ_INT(robo_input_queue_count(queue), 0u);
    ROBO_CHECK_EQ_INT(robo_input_queue_is_empty(queue), 1);

    ROBO_CHECK_EQ_INT(robo_input_queue_stats(queue, &stats), ROBO_OK);
    ROBO_CHECK_EQ_INT(stats.popped, 3u);
    ROBO_CHECK_EQ_INT(stats.high_water, 3u);

    /* A ring's cursors wrap: pop, push, pop, push leaves the *arrival order* intact across the wrap. */
    {
        int index;

        for (index = 0; index < 6; index++) {
            robo_input_event_t event = input_pointer_move(index, index, 0u, ROBO_INPUT_SPACE_SCREEN, 3u);

            input_push(queue, &event);
            ROBO_CHECK_EQ_INT(robo_input_queue_pop(queue, &out, NULL), ROBO_OK);
            ROBO_CHECK_EQ_INT(out.payload.pointer.position.x, index);
        }
        ROBO_CHECK_EQ_INT(robo_input_queue_count(queue), 0u);
    }

    /* NULL arguments refuse, and a NULL output is refused rather than accepted-and-ignored. */
    ROBO_CHECK_EQ_INT(robo_input_queue_push(NULL, &first, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_queue_push(queue, NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_queue_pop(NULL, &out, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_queue_pop(queue, NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_queue_peek(queue, NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_queue_peek(NULL, &out, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_queue_peek_at(queue, 0u, NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_queue_peek_at(NULL, 0u, &out, NULL), ROBO_INVALID_ARGUMENT);

    robo_input_queue_destroy(queue);
}

/* ------------------------------------------------------------------ */
/* The bound                                                           */
/* ------------------------------------------------------------------ */

static void test_queue_full(void)
{
    robo_input_queue_t *queue = input_queue(2u);
    robo_input_queue_stats_t stats;
    robo_input_event_t first = input_pointer_move(1, 1, 0u, ROBO_INPUT_SPACE_SCREEN, 3u);
    robo_input_event_t second = input_pointer_move(2, 2, 0u, ROBO_INPUT_SPACE_SCREEN, 3u);
    robo_input_event_t third = input_pointer_move(3, 3, 0u, ROBO_INPUT_SPACE_SCREEN, 3u);
    robo_input_event_t out;
    robo_input_diag_t diag;

    input_push(queue, &first);
    input_push(queue, &second);
    ROBO_CHECK_EQ_INT(robo_input_queue_is_full(queue), 1);
    ROBO_CHECK_EQ_INT(robo_input_queue_count(queue), 2u);

    /* The third push is refused, counted, and **changes nothing**: the two events that are in are the two
     * that went in, in their order. A queue that evicted would have lost `first` here. */
    diag = (robo_input_diag_t){ 0 };
    ROBO_CHECK_EQ_INT(robo_input_queue_push(queue, &third, &diag), ROBO_BUSY);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_QUEUE);
    ROBO_CHECK_EQ_INT(robo_input_queue_count(queue), 2u);
    ROBO_CHECK_EQ_INT(robo_input_queue_stats(queue, &stats), ROBO_OK);
    ROBO_CHECK_EQ_INT(stats.pushed, 2u);
    ROBO_CHECK_EQ_INT(stats.dropped, 1u);
    ROBO_CHECK_EQ_INT(stats.high_water, 2u);
    ROBO_CHECK_EQ_INT(robo_input_queue_peek_at(queue, 0u, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_input_event_equal(&out, &first));

    /* Draining one makes room for exactly one more, and the refused event was not queued behind it. */
    ROBO_CHECK_EQ_INT(robo_input_queue_pop(queue, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_input_event_equal(&out, &first));
    ROBO_CHECK_EQ_INT(robo_input_queue_is_full(queue), 0);
    input_push(queue, &third);
    ROBO_CHECK_EQ_INT(robo_input_queue_count(queue), 2u);
    ROBO_CHECK_EQ_INT(robo_input_queue_peek_at(queue, 0u, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_input_event_equal(&out, &second));
    ROBO_CHECK_EQ_INT(robo_input_queue_peek_at(queue, 1u, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_input_event_equal(&out, &third));

    /* A queue of one event: the tightest bound, and the same behaviour. */
    robo_input_queue_destroy(queue);
    queue = input_queue(1u);
    input_push(queue, &first);
    ROBO_CHECK_EQ_INT(robo_input_queue_push(queue, &second, NULL), ROBO_BUSY);
    ROBO_CHECK_EQ_INT(robo_input_queue_pop(queue, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_input_event_equal(&out, &first));
    input_push(queue, &second);
    ROBO_CHECK_EQ_INT(robo_input_queue_pop(queue, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_input_event_equal(&out, &second));

    robo_input_queue_destroy(queue);
}

/* ------------------------------------------------------------------ */
/* Clearing, and refusing a malformed event                            */
/* ------------------------------------------------------------------ */

static void test_queue_clear_and_validate(void)
{
    robo_input_queue_t *queue = input_queue(4u);
    robo_input_queue_stats_t stats;
    robo_input_event_t event = input_pointer_move(9, 9, 0u, ROBO_INPUT_SPACE_SCREEN, 3u);
    robo_input_event_t out;
    robo_input_diag_t diag;

    input_push(queue, &event);
    input_push(queue, &event);
    ROBO_CHECK_EQ_INT(robo_input_queue_count(queue), 2u);

    /* Clearing drops what is queued and says how much, keeping the history either side of it. */
    robo_input_queue_clear(queue);
    ROBO_CHECK_EQ_INT(robo_input_queue_count(queue), 0u);
    ROBO_CHECK_EQ_INT(robo_input_queue_is_empty(queue), 1);
    ROBO_CHECK_EQ_INT(robo_input_queue_stats(queue, &stats), ROBO_OK);
    ROBO_CHECK_EQ_INT(stats.cleared, 2u);
    ROBO_CHECK_EQ_INT(stats.pushed, 2u);
    ROBO_CHECK_EQ_INT(stats.high_water, 2u);
    /* A second clear with nothing in it changes nothing, and does not count a phantom. */
    robo_input_queue_clear(queue);
    ROBO_CHECK_EQ_INT(robo_input_queue_stats(queue, &stats), ROBO_OK);
    ROBO_CHECK_EQ_INT(stats.cleared, 2u);

    /* The queue is usable again after a clear, and its cursors were reset rather than left mid-ring. */
    input_push(queue, &event);
    ROBO_CHECK_EQ_INT(robo_input_queue_peek_at(queue, 0u, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_input_event_equal(&out, &event));

    /* A malformed event is refused with the *event* domain, not the queue's: the caller fixes the event. */
    {
        robo_input_event_t broken = event;

        broken.type = ROBO_INPUT_EVENT_NONE;
        diag = (robo_input_diag_t){ 0 };
        ROBO_CHECK_EQ_INT(robo_input_queue_push(queue, &broken, &diag), ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_EVENT);
        ROBO_CHECK_EQ_INT(robo_input_queue_count(queue), 1u);

        broken = event;
        broken.space = ROBO_INPUT_SPACE_UNKNOWN;
        diag = (robo_input_diag_t){ 0 };
        ROBO_CHECK_EQ_INT(robo_input_queue_push(queue, &broken, &diag), ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_EVENT);

        /* A refused push is not a *drop*: `dropped` counts refusals caused by the bound, which is what a
         * consumer reads to learn it is behind. */
        ROBO_CHECK_EQ_INT(robo_input_queue_stats(queue, &stats), ROBO_OK);
        ROBO_CHECK_EQ_INT(stats.dropped, 0u);
        ROBO_CHECK_EQ_INT(stats.pushed, 3u);
    }

    robo_input_queue_destroy(queue);
}

void robo_test_register_input_queue(void)
{
    robo_test_register("input.queue.lifecycle", test_queue_lifecycle);
    robo_test_register("input.queue.fifo", test_queue_fifo);
    robo_test_register("input.queue.full", test_queue_full);
    robo_test_register("input.queue.clear_and_validate", test_queue_clear_and_validate);
}
