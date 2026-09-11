#ifndef ROBO_SCHED_INTERNAL_H
#define ROBO_SCHED_INTERNAL_H

#include "scheduler/scheduler.h"
#include "scheduler/queue.h"
#include "thread/thread.h"

extern RoboThread *g_current;
extern RoboThread *g_idle;
extern RoboThread *g_bootstrap;
extern RoboThread *g_all;
extern robo_runqueue_t g_ready;
extern robo_sched_state_t g_sched_state;
extern unsigned g_preempt_count;

uint64_t robo_sched_lock(void);
void robo_sched_unlock(uint64_t flags);
void robo_sched_panic_corrupt(const char *detail, RoboThread *cur, RoboThread *next);

uint64_t robo_sched_alloc_id(void);
void robo_sched_all_add(RoboThread *thread);
void robo_sched_note_create(void);
void robo_sched_note_term(void);
void robo_sched_reap_later(RoboThread *thread);
uint64_t robo_sched_quantum_ns(void);

void robo_idle_entry(void *arg);

#endif
