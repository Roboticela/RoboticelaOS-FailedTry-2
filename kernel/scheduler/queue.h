#ifndef ROBO_SCHEDULER_QUEUE_H
#define ROBO_SCHEDULER_QUEUE_H

#include "thread/thread.h"

/*
 * FIFO circular doubly-linked ready queue of RoboThread.
 * Only READY threads may be enqueued.
 */
typedef struct robo_runqueue {
    RoboThread *head;
    unsigned count;
} robo_runqueue_t;

void robo_rq_init(robo_runqueue_t *q);
void robo_rq_enqueue(robo_runqueue_t *q, RoboThread *thread);
RoboThread *robo_rq_dequeue(robo_runqueue_t *q);
void robo_rq_remove(robo_runqueue_t *q, RoboThread *thread);
unsigned robo_rq_count(const robo_runqueue_t *q);
int robo_rq_contains(const robo_runqueue_t *q, const RoboThread *thread);

#endif
