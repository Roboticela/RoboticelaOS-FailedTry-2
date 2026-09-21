#include "scheduler/queue.h"

void RqInit(Runqueue *q)
{
    if (!q)
        return;
    q->head = 0;
    q->count = 0;
}

void RqEnqueue(Runqueue *q, RoboThread *thread)
{
    if (!q || !thread)
        return;
    if (thread->state != RoboThreadReady)
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

RoboThread *RqDequeue(Runqueue *q)
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

void RqRemove(Runqueue *q, RoboThread *thread)
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

unsigned RqCount(const Runqueue *q)
{
    return q ? q->count : 0;
}

int RqContains(const Runqueue *q, const RoboThread *thread)
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
