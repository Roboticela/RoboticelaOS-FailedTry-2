#ifndef ROBO_SCHED_INTERNAL_H
#define ROBO_SCHED_INTERNAL_H

#include "scheduler/scheduler.h"
#include "scheduler/queue.h"
#include "thread/thread.h"

extern RoboThread *gCurrent;
extern RoboThread *gIdle;
extern RoboThread *gBootstrap;
extern RoboThread *gAll;
extern Runqueue gReady;
extern SchedState gSchedState;
extern unsigned gPreemptCount;

uint64_t SchedLock(void);
void SchedUnlock(uint64_t flags);
void SchedPanicCorrupt(const char *detail, RoboThread *cur, RoboThread *next);

uint64_t SchedAllocId(void);
void SchedAllAdd(RoboThread *thread);
void SchedNoteCreate(void);
void SchedNoteTerm(void);
void SchedReapLater(RoboThread *thread);
uint64_t SchedQuantumNs(void);

void IdleEntry(void *arg);

#endif
