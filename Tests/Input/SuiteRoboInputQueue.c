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

static void TestQueueLifecycle(void)
{
    InputQueueConfig config;
    InputQueue *queue = NULL;
    InputQueue *custom = NULL;
    InputQueueStats stats;
    size_t baseline;

    InputQueueConfigInit(&config);
    RoboCheckEqInt(config.capacity, RoboInputQueueCapacityDefault);
    InputQueueConfigInit(NULL);

    /* A NULL configuration means the defaults, and a zero capacity does too. */
    RoboCheckEqInt(InputQueueCreate(NULL, &queue, NULL), RoboOk);
    RoboCheck(InputQueueIsLive(queue));
    RoboCheckEqInt(InputQueueCapacity(queue), RoboInputQueueCapacityDefault);
    RoboCheckEqInt(InputQueueCount(queue), 0u);
    RoboCheckEqInt(InputQueueIsEmpty(queue), 1);
    RoboCheckEqInt(InputQueueIsFull(queue), 0);
    RoboCheckEqInt(GetInputQueueStats(queue, &stats), RoboOk);
    RoboCheckEqInt(stats.capacity, RoboInputQueueCapacityDefault);
    RoboCheckEqInt(stats.count, 0u);
    RoboCheckEqInt(stats.pushed, 0u);
    RoboCheckEqInt(stats.popped, 0u);
    RoboCheckEqInt(stats.dropped, 0u);
    RoboCheckEqInt(stats.cleared, 0u);
    RoboCheckEqInt(stats.highWater, 0u);
    InputQueueDestroy(queue);

    /* A named capacity is honored; a capacity above the maximum is *refused*, not clamped, because a caller
     * that asked for more and silently got the maximum would have a bounded queue it believed was
     * unbounded. */
    config.capacity = 4u;
    RoboCheckEqInt(InputQueueCreate(&config, &custom, NULL), RoboOk);
    RoboCheckEqInt(InputQueueCapacity(custom), 4u);
    InputQueueDestroy(custom);

    config.capacity = RoboInputQueueCapacityMax + 1u;
    custom = NULL;
    {
        InputDiag diag;

        diag = (InputDiag){ 0 };
        RoboCheckEqInt(InputQueueCreate(&config, &custom, &diag), RoboInvalidArgument);
        RoboCheckEqInt(diag.domain, RoboInputDomainQueue);
        RoboCheck(custom == NULL);
    }
    config.capacity = RoboInputQueueCapacityMax;
    RoboCheckEqInt(InputQueueCreate(&config, &custom, NULL), RoboOk);
    RoboCheckEqInt(InputQueueCapacity(custom), RoboInputQueueCapacityMax);
    InputQueueDestroy(custom);

    /* NULL arguments, a NULL queue everywhere, and destruction of nothing. */
    RoboCheckEqInt(InputQueueCreate(&config, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputQueueIsLive(NULL), 0);
    RoboCheckEqInt(InputQueueCapacity(NULL), 0u);
    RoboCheckEqInt(InputQueueCount(NULL), 0u);
    RoboCheckEqInt(InputQueueIsEmpty(NULL), 1);
    RoboCheckEqInt(InputQueueIsFull(NULL), 0);
    RoboCheckEqInt(GetInputQueueStats(NULL, &stats), RoboInvalidArgument);
    RoboCheckEqInt(GetInputQueueStats(NULL, NULL), RoboInvalidArgument);
    InputQueueClear(NULL);
    InputQueueDestroy(NULL);

    /* Create/destroy round trips take and return exactly the same blocks. */
    baseline = InputLiveBlocks();
    {
        int round;

        for (round = 0; round < 4; round++) {
            InputQueue *cycle = GetRoboInputQueue(8u);

            RoboCheck(InputQueueIsLive(cycle));
            InputQueueDestroy(cycle);
            cycle = GetRoboInputQueue(32u);
            RoboCheckEqInt(InputQueueCapacity(cycle), 32u);
            InputQueueDestroy(cycle);
        }
    }
    RoboCheckEqInt((int)InputLiveBlocks(), (int)baseline);
}

/* ------------------------------------------------------------------ */
/* FIFO order                                                          */
/* ------------------------------------------------------------------ */

static void TestQueueFifo(void)
{
    InputQueue *queue = GetRoboInputQueue(8u);
    InputQueueStats stats;
    InputEvent first = InputPointerMove(1, 2, 0u, RoboInputSpaceScreen, 3u);
    InputEvent second = InputButtonEvent(3, 4, RoboMouseButtonLeft, 1, 3u);
    InputEvent third = InputKeyEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 1,
                                               RoboInputModifiersNone);
    InputEvent out;
    InputDiag diag;

    /* An empty queue answers "nothing" in both directions, and leaves the caller's record untouched so a
     * stale value cannot be mistaken for a dequeued one. */
    out = first;
    RoboCheckEqInt(InputQueuePeek(queue, &out, &diag), RoboNotFound);
    RoboCheckEqInt(diag.domain, RoboInputDomainQueue);
    RoboCheckEqInt(out.type, first.type);
    RoboCheckEqInt(InputQueuePop(queue, &out, &diag), RoboNotFound);
    RoboCheckEqInt(out.type, first.type);
    RoboCheckEqInt(InputQueuePeekAt(queue, 0u, &out, &diag), RoboNotFound);

    RoboInputPush(queue, &first);
    RoboInputPush(queue, &second);
    RoboInputPush(queue, &third);
    RoboCheckEqInt(InputQueueCount(queue), 3u);
    RoboCheckEqInt(InputQueueIsEmpty(queue), 0);
    RoboCheckEqInt(GetInputQueueStats(queue, &stats), RoboOk);
    RoboCheckEqInt(stats.pushed, 3u);
    RoboCheckEqInt(stats.highWater, 3u);

    /* Peek is a look, not a take: three different event types read back in the order they went in. */
    RoboCheckEqInt(InputQueuePeek(queue, &out, NULL), RoboOk);
    RoboCheck(InputEventEqual(&out, &first));
    RoboCheckEqInt(InputQueueCount(queue), 3u);
    RoboCheckEqInt(InputQueuePeekAt(queue, 0u, &out, NULL), RoboOk);
    RoboCheck(InputEventEqual(&out, &first));
    RoboCheckEqInt(InputQueuePeekAt(queue, 1u, &out, NULL), RoboOk);
    RoboCheck(InputEventEqual(&out, &second));
    RoboCheckEqInt(InputQueuePeekAt(queue, 2u, &out, NULL), RoboOk);
    RoboCheck(InputEventEqual(&out, &third));
    RoboCheckEqInt(InputQueuePeekAt(queue, 3u, &out, &diag), RoboNotFound);
    RoboCheckEqInt(diag.domain, RoboInputDomainQueue);

    RoboCheckEqInt(InputQueuePop(queue, &out, NULL), RoboOk);
    RoboCheck(InputEventEqual(&out, &first));
    RoboCheckEqInt(InputQueuePop(queue, &out, NULL), RoboOk);
    RoboCheck(InputEventEqual(&out, &second));
    RoboCheckEqInt(InputQueuePop(queue, &out, NULL), RoboOk);
    RoboCheck(InputEventEqual(&out, &third));
    RoboCheckEqInt(InputQueueCount(queue), 0u);
    RoboCheckEqInt(InputQueueIsEmpty(queue), 1);

    RoboCheckEqInt(GetInputQueueStats(queue, &stats), RoboOk);
    RoboCheckEqInt(stats.popped, 3u);
    RoboCheckEqInt(stats.highWater, 3u);

    /* A ring's cursors wrap: pop, push, pop, push leaves the *arrival order* intact across the wrap. */
    {
        int index;

        for (index = 0; index < 6; index++) {
            InputEvent event = InputPointerMove(index, index, 0u, RoboInputSpaceScreen, 3u);

            RoboInputPush(queue, &event);
            RoboCheckEqInt(InputQueuePop(queue, &out, NULL), RoboOk);
            RoboCheckEqInt(out.payload.pointer.position.x, index);
        }
        RoboCheckEqInt(InputQueueCount(queue), 0u);
    }

    /* NULL arguments refuse, and a NULL output is refused rather than accepted-and-ignored. */
    RoboCheckEqInt(InputQueuePush(NULL, &first, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputQueuePush(queue, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputQueuePop(NULL, &out, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputQueuePop(queue, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputQueuePeek(queue, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputQueuePeek(NULL, &out, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputQueuePeekAt(queue, 0u, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputQueuePeekAt(NULL, 0u, &out, NULL), RoboInvalidArgument);

    InputQueueDestroy(queue);
}

/* ------------------------------------------------------------------ */
/* The bound                                                           */
/* ------------------------------------------------------------------ */

static void TestQueueFull(void)
{
    InputQueue *queue = GetRoboInputQueue(2u);
    InputQueueStats stats;
    InputEvent first = InputPointerMove(1, 1, 0u, RoboInputSpaceScreen, 3u);
    InputEvent second = InputPointerMove(2, 2, 0u, RoboInputSpaceScreen, 3u);
    InputEvent third = InputPointerMove(3, 3, 0u, RoboInputSpaceScreen, 3u);
    InputEvent out;
    InputDiag diag;

    RoboInputPush(queue, &first);
    RoboInputPush(queue, &second);
    RoboCheckEqInt(InputQueueIsFull(queue), 1);
    RoboCheckEqInt(InputQueueCount(queue), 2u);

    /* The third push is refused, counted, and **changes nothing**: the two events that are in are the two
     * that went in, in their order. A queue that evicted would have lost `first` here. */
    diag = (InputDiag){ 0 };
    RoboCheckEqInt(InputQueuePush(queue, &third, &diag), RoboBusy);
    RoboCheckEqInt(diag.domain, RoboInputDomainQueue);
    RoboCheckEqInt(InputQueueCount(queue), 2u);
    RoboCheckEqInt(GetInputQueueStats(queue, &stats), RoboOk);
    RoboCheckEqInt(stats.pushed, 2u);
    RoboCheckEqInt(stats.dropped, 1u);
    RoboCheckEqInt(stats.highWater, 2u);
    RoboCheckEqInt(InputQueuePeekAt(queue, 0u, &out, NULL), RoboOk);
    RoboCheck(InputEventEqual(&out, &first));

    /* Draining one makes room for exactly one more, and the refused event was not queued behind it. */
    RoboCheckEqInt(InputQueuePop(queue, &out, NULL), RoboOk);
    RoboCheck(InputEventEqual(&out, &first));
    RoboCheckEqInt(InputQueueIsFull(queue), 0);
    RoboInputPush(queue, &third);
    RoboCheckEqInt(InputQueueCount(queue), 2u);
    RoboCheckEqInt(InputQueuePeekAt(queue, 0u, &out, NULL), RoboOk);
    RoboCheck(InputEventEqual(&out, &second));
    RoboCheckEqInt(InputQueuePeekAt(queue, 1u, &out, NULL), RoboOk);
    RoboCheck(InputEventEqual(&out, &third));

    /* A queue of one event: the tightest bound, and the same behaviour. */
    InputQueueDestroy(queue);
    queue = GetRoboInputQueue(1u);
    RoboInputPush(queue, &first);
    RoboCheckEqInt(InputQueuePush(queue, &second, NULL), RoboBusy);
    RoboCheckEqInt(InputQueuePop(queue, &out, NULL), RoboOk);
    RoboCheck(InputEventEqual(&out, &first));
    RoboInputPush(queue, &second);
    RoboCheckEqInt(InputQueuePop(queue, &out, NULL), RoboOk);
    RoboCheck(InputEventEqual(&out, &second));

    InputQueueDestroy(queue);
}

/* ------------------------------------------------------------------ */
/* Clearing, and refusing a malformed event                            */
/* ------------------------------------------------------------------ */

static void TestQueueClearAndValidate(void)
{
    InputQueue *queue = GetRoboInputQueue(4u);
    InputQueueStats stats;
    InputEvent event = InputPointerMove(9, 9, 0u, RoboInputSpaceScreen, 3u);
    InputEvent out;
    InputDiag diag;

    RoboInputPush(queue, &event);
    RoboInputPush(queue, &event);
    RoboCheckEqInt(InputQueueCount(queue), 2u);

    /* Clearing drops what is queued and says how much, keeping the history either side of it. */
    InputQueueClear(queue);
    RoboCheckEqInt(InputQueueCount(queue), 0u);
    RoboCheckEqInt(InputQueueIsEmpty(queue), 1);
    RoboCheckEqInt(GetInputQueueStats(queue, &stats), RoboOk);
    RoboCheckEqInt(stats.cleared, 2u);
    RoboCheckEqInt(stats.pushed, 2u);
    RoboCheckEqInt(stats.highWater, 2u);
    /* A second clear with nothing in it changes nothing, and does not count a phantom. */
    InputQueueClear(queue);
    RoboCheckEqInt(GetInputQueueStats(queue, &stats), RoboOk);
    RoboCheckEqInt(stats.cleared, 2u);

    /* The queue is usable again after a clear, and its cursors were reset rather than left mid-ring. */
    RoboInputPush(queue, &event);
    RoboCheckEqInt(InputQueuePeekAt(queue, 0u, &out, NULL), RoboOk);
    RoboCheck(InputEventEqual(&out, &event));

    /* A malformed event is refused with the *event* domain, not the queue's: the caller fixes the event. */
    {
        InputEvent broken = event;

        broken.type = RoboInputEventNone;
        diag = (InputDiag){ 0 };
        RoboCheckEqInt(InputQueuePush(queue, &broken, &diag), RoboInvalidArgument);
        RoboCheckEqInt(diag.domain, RoboInputDomainEvent);
        RoboCheckEqInt(InputQueueCount(queue), 1u);

        broken = event;
        broken.space = RoboInputSpaceUnknown;
        diag = (InputDiag){ 0 };
        RoboCheckEqInt(InputQueuePush(queue, &broken, &diag), RoboInvalidArgument);
        RoboCheckEqInt(diag.domain, RoboInputDomainEvent);

        /* A refused push is not a *drop*: `dropped` counts refusals caused by the bound, which is what a
         * consumer reads to learn it is behind. */
        RoboCheckEqInt(GetInputQueueStats(queue, &stats), RoboOk);
        RoboCheckEqInt(stats.dropped, 0u);
        RoboCheckEqInt(stats.pushed, 3u);
    }

    InputQueueDestroy(queue);
}

void TestRegisterInputQueue(void)
{
    TestRegister("input.queue.lifecycle", TestQueueLifecycle);
    TestRegister("input.queue.fifo", TestQueueFifo);
    TestRegister("input.queue.full", TestQueueFull);
    TestRegister("input.queue.clear_and_validate", TestQueueClearAndValidate);
}
