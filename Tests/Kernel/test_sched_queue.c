#include "scheduler/queue.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int gPass;
static int gFails;

#define CHECK(cond, msg) do { \
    if (cond) { gPass++; } \
    else { gFails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } \
} while (0)

static void ZeroThread(RoboThread *t, uint64_t id)
{
    memset(t, 0, sizeof(*t));
    t->id = id;
    t->state = RoboThreadReady;
}

int main(void)
{
    Runqueue q;
    RoboThread a, b, c, d;
    RoboThread *t;

    RqInit(&q);
    CHECK(RqCount(&q) == 0, "empty count");
    CHECK(RqDequeue(&q) == 0, "empty dequeue");

    ZeroThread(&a, 1);
    ZeroThread(&b, 2);
    ZeroThread(&c, 3);
    ZeroThread(&d, 4);

    RqEnqueue(&q, &a);
    RqEnqueue(&q, &b);
    RqEnqueue(&q, &c);
    CHECK(RqCount(&q) == 3, "count 3");
    CHECK(RqContains(&q, &a), "contains a");
    CHECK(RqContains(&q, &c), "contains c");
    CHECK(!RqContains(&q, &d), "not contains d");

    t = RqDequeue(&q);
    CHECK(t == &a, "fifo a");
    t = RqDequeue(&q);
    CHECK(t == &b, "fifo b");
    t = RqDequeue(&q);
    CHECK(t == &c, "fifo c");
    CHECK(RqCount(&q) == 0, "drained");

    RqEnqueue(&q, &a);
    RqEnqueue(&q, &b);
    RqEnqueue(&q, &c);
    t = RqDequeue(&q);
    CHECK(t == &a, "rr take a");
    RqEnqueue(&q, &a);
    t = RqDequeue(&q);
    CHECK(t == &b, "rr take b");
    RqEnqueue(&q, &b);
    t = RqDequeue(&q);
    CHECK(t == &c, "rr take c");
    RqEnqueue(&q, &c);
    t = RqDequeue(&q);
    CHECK(t == &a, "rr wrap a");
    RqEnqueue(&q, &a);

    RqRemove(&q, &c);
    CHECK(!RqContains(&q, &c), "removed c");
    CHECK(RqCount(&q) == 2, "count after remove");
    t = RqDequeue(&q);
    CHECK(t == &b, "head is b after remove wrap");
    t = RqDequeue(&q);
    CHECK(t == &a, "then a");
    CHECK(RqDequeue(&q) == 0, "empty after");

    printf("Scheduler queue host tests  %d passed  %d failed\n", gPass, gFails);
    return gFails ? 1 : 0;
}
