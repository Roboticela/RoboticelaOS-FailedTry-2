#ifndef ROBO_SCHED_INTERNAL_H
#define ROBO_SCHED_INTERNAL_H

#include "scheduler/scheduler.h"
#include "scheduler/queue.h"

extern robo_task_t *g_current;
extern robo_task_t *g_idle;
extern robo_task_t *g_bootstrap;
extern robo_task_t *g_all;
extern robo_runqueue_t g_ready;
extern robo_sched_state_t g_sched_state;
extern unsigned g_preempt_count;

uint64_t robo_sched_lock(void);
void robo_sched_unlock(uint64_t flags);
void robo_sched_panic_corrupt(const char *detail, robo_task_t *cur, robo_task_t *next);

uint64_t robo_sched_alloc_id(void);
void robo_sched_all_add(robo_task_t *task);
void robo_sched_note_create(void);
void robo_sched_note_term(void);
void robo_sched_reap_later(robo_task_t *task);
uint64_t robo_sched_quantum_ns(void);

void robo_idle_entry(void *arg);
void robo_task_bootstrap(void);

int robo_task_stack_alloc(robo_task_t *task);
void robo_task_stack_setup_iframe(robo_task_t *task);
void robo_task_stack_free(robo_task_t *task);
void robo_task_zero(robo_task_t *task);
void robo_task_set_name(robo_task_t *task, const char *name);

#endif
