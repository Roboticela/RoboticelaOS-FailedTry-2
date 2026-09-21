#ifndef ROBO_SCHEDULER_QUEUE_H
#define ROBO_SCHEDULER_QUEUE_H

#include "thread/thread.h"

/*
 * FIFO circular doubly-linked ready queue of RoboThread.
 * Only READY threads may be enqueued.
 */
typedef struct Runqueue {
    RoboThread *head;
    unsigned count;
} Runqueue;

void RqInit(Runqueue *q);
void RqEnqueue(Runqueue *q, RoboThread *thread);
RoboThread *RqDequeue(Runqueue *q);
void RqRemove(Runqueue *q, RoboThread *thread);
unsigned RqCount(const Runqueue *q);
int RqContains(const Runqueue *q, const RoboThread *thread);

#endif
