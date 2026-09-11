#ifndef ROBO_SCHEDULER_TASK_H
#define ROBO_SCHEDULER_TASK_H

#include "thread/thread.h"

/*
 * Compatibility alias: the schedulable object is RoboThread.
 * Phase 8 "task" names map onto the same structure.
 */
typedef RoboThread robo_task_t;
typedef robo_thread_state_t robo_task_state_t;
typedef robo_thread_entry_t robo_task_entry_t;

#define ROBO_TID_BOOTSTRAP ROBO_THREAD_ID_BOOTSTRAP
#define ROBO_TID_IDLE      ROBO_THREAD_ID_IDLE
#define ROBO_TASK_NAME_MAX ROBO_THREAD_NAME_MAX
#define ROBO_KSTACK_SIZE   ROBO_THREAD_KSTACK_SIZE
#define ROBO_KSTACK_PAGES  ROBO_THREAD_KSTACK_PAGES
#define ROBO_KSTACK_GUARD_PAGES ROBO_THREAD_KSTACK_GUARD_PAGES

#define ROBO_TASK_NEW        ROBO_THREAD_NEW
#define ROBO_TASK_READY      ROBO_THREAD_READY
#define ROBO_TASK_RUNNING    ROBO_THREAD_RUNNING
#define ROBO_TASK_BLOCKED    ROBO_THREAD_BLOCKED
#define ROBO_TASK_SLEEPING   ROBO_THREAD_SLEEPING
#define ROBO_TASK_TERMINATED ROBO_THREAD_TERMINATED

robo_task_t *robo_task_create(const char *name, robo_task_entry_t entry, void *arg);
void robo_task_exit(void);
void robo_task_yield(void);
void robo_task_block(void);
int robo_task_unblock(robo_task_t *task);

static inline const char *robo_task_state_name(robo_task_state_t state)
{
    return robo_thread_state_name(state);
}

#endif
