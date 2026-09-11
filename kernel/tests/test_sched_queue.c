#include "scheduler/queue.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int g_pass;
static int g_fails;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } \
} while (0)

static void zero_task(robo_task_t *t, uint64_t id)
{
    memset(t, 0, sizeof(*t));
    t->id = id;
    t->state = ROBO_TASK_READY;
}

int main(void)
{
    robo_runqueue_t q;
    robo_task_t a, b, c, d;
    robo_task_t *t;

    robo_rq_init(&q);
    CHECK(robo_rq_count(&q) == 0, "empty count");
    CHECK(robo_rq_dequeue(&q) == 0, "empty dequeue");

    zero_task(&a, 1);
    zero_task(&b, 2);
    zero_task(&c, 3);
    zero_task(&d, 4);

    robo_rq_enqueue(&q, &a);
    robo_rq_enqueue(&q, &b);
    robo_rq_enqueue(&q, &c);
    CHECK(robo_rq_count(&q) == 3, "count 3");
    CHECK(robo_rq_contains(&q, &a), "contains a");
    CHECK(robo_rq_contains(&q, &c), "contains c");
    CHECK(!robo_rq_contains(&q, &d), "not contains d");

    t = robo_rq_dequeue(&q);
    CHECK(t == &a, "fifo a");
    t = robo_rq_dequeue(&q);
    CHECK(t == &b, "fifo b");
    t = robo_rq_dequeue(&q);
    CHECK(t == &c, "fifo c");
    CHECK(robo_rq_count(&q) == 0, "drained");

    robo_rq_enqueue(&q, &a);
    robo_rq_enqueue(&q, &b);
    robo_rq_enqueue(&q, &c);
    t = robo_rq_dequeue(&q);
    CHECK(t == &a, "rr take a");
    robo_rq_enqueue(&q, &a);
    t = robo_rq_dequeue(&q);
    CHECK(t == &b, "rr take b");
    robo_rq_enqueue(&q, &b);
    t = robo_rq_dequeue(&q);
    CHECK(t == &c, "rr take c");
    robo_rq_enqueue(&q, &c);
    t = robo_rq_dequeue(&q);
    CHECK(t == &a, "rr wrap a");
    robo_rq_enqueue(&q, &a);

    robo_rq_remove(&q, &c);
    CHECK(!robo_rq_contains(&q, &c), "removed c");
    CHECK(robo_rq_count(&q) == 2, "count after remove");
    t = robo_rq_dequeue(&q);
    CHECK(t == &b, "head is b after remove wrap");
    t = robo_rq_dequeue(&q);
    CHECK(t == &a, "then a");
    CHECK(robo_rq_dequeue(&q) == 0, "empty after");

    printf("Scheduler queue host tests  %d passed  %d failed\n", g_pass, g_fails);
    return g_fails ? 1 : 0;
}
