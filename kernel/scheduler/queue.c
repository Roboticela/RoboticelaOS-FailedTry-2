#include "scheduler/queue.h"

void robo_rq_init(robo_runqueue_t *q)
{
    if (!q)
        return;
    q->head = 0;
    q->count = 0;
}

void robo_rq_enqueue(robo_runqueue_t *q, robo_task_t *task)
{
    if (!q || !task)
        return;
    task->next = 0;
    task->prev = 0;
    if (!q->head) {
        task->next = task;
        task->prev = task;
        q->head = task;
    } else {
        robo_task_t *h = q->head;
        task->next = h;
        task->prev = h->prev;
        h->prev->next = task;
        h->prev = task;
    }
    q->count++;
}

robo_task_t *robo_rq_dequeue(robo_runqueue_t *q)
{
    robo_task_t *t;
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

void robo_rq_remove(robo_runqueue_t *q, robo_task_t *task)
{
    if (!q || !task || !q->head || !task->next || !task->prev)
        return;
    if (task->next == task) {
        q->head = 0;
    } else {
        task->prev->next = task->next;
        task->next->prev = task->prev;
        if (q->head == task)
            q->head = task->next;
    }
    task->next = 0;
    task->prev = 0;
    q->count--;
}

unsigned robo_rq_count(const robo_runqueue_t *q)
{
    return q ? q->count : 0;
}

int robo_rq_contains(const robo_runqueue_t *q, const robo_task_t *task)
{
    const robo_task_t *p;
    unsigned n;
    if (!q || !task || !q->head)
        return 0;
    p = q->head;
    n = 0;
    do {
        if (p == task)
            return 1;
        p = p->next;
        n++;
    } while (p && p != q->head && n <= q->count);
    return 0;
}
