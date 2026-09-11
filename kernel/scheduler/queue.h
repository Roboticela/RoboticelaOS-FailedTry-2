#ifndef ROBO_SCHEDULER_QUEUE_H
#define ROBO_SCHEDULER_QUEUE_H

#include "scheduler/task.h"

/*
 * FIFO circular doubly-linked ready queue.
 * All runnable non-idle tasks share this one class (no priorities).
 */
typedef struct robo_runqueue {
    robo_task_t *head;
    unsigned count;
} robo_runqueue_t;

void robo_rq_init(robo_runqueue_t *q);
void robo_rq_enqueue(robo_runqueue_t *q, robo_task_t *task);
robo_task_t *robo_rq_dequeue(robo_runqueue_t *q);
void robo_rq_remove(robo_runqueue_t *q, robo_task_t *task);
unsigned robo_rq_count(const robo_runqueue_t *q);
int robo_rq_contains(const robo_runqueue_t *q, const robo_task_t *task);

#endif
