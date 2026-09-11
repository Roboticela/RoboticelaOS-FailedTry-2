#include "scheduler/queue.h"

void robo_rq_init(robo_runqueue_t *q)
{
    if (!q)
        return;
    q->head = 0;
    q->count = 0;
}

void robo_rq_enqueue(robo_runqueue_t *q, RoboThread *thread)
{
    if (!q || !thread)
        return;
    if (thread->state != ROBO_THREAD_READY)
        return;
    thread->next = 0;
    thread->prev = 0;
    if (!q->head) {
        thread->next = thread;
        thread->prev = thread;
        q->head = thread;
    } else {
        RoboThread *h = q->head;
        thread->next = h;
        thread->prev = h->prev;
        h->prev->next = thread;
        h->prev = thread;
    }
    q->count++;
}

RoboThread *robo_rq_dequeue(robo_runqueue_t *q)
{
    RoboThread *t;
    if (!q || !q->head)
        return 0;
    t = q->head;
    if (t->next == t) {
        q->head = 0;
    } else {
        t->prev->next = t->next;
        t->next->prev = t->prev;
        q->head = t->next;
    }
    t->next = 0;
    t->prev = 0;
    q->count--;
    return t;
}

void robo_rq_remove(robo_runqueue_t *q, RoboThread *thread)
{
    if (!q || !thread || !q->head || !thread->next || !thread->prev)
        return;
    if (thread->next == thread) {
        q->head = 0;
    } else {
        thread->prev->next = thread->next;
        thread->next->prev = thread->prev;
        if (q->head == thread)
            q->head = thread->next;
    }
    thread->next = 0;
    thread->prev = 0;
    q->count--;
}

unsigned robo_rq_count(const robo_runqueue_t *q)
{
    return q ? q->count : 0;
}

int robo_rq_contains(const robo_runqueue_t *q, const RoboThread *thread)
{
    const RoboThread *p;
    unsigned n;
    if (!q || !thread || !q->head)
        return 0;
    p = q->head;
    n = 0;
    do {
        if (p == thread)
            return 1;
        p = p->next;
        n++;
    } while (p && p != q->head && n <= q->count);
    return 0;
}
