#ifndef ROBO_SCHEDULER_TASK_H
#define ROBO_SCHEDULER_TASK_H

#include "thread/thread.h"

/*
 * Compatibility alias: the schedulable object is RoboThread.
 * Phase 8 "task" names map onto the same structure.
 */
typedef RoboThread Task;
typedef ThreadState TaskState;
typedef ThreadEntry TaskEntry;

#define RoboTidBootstrap RoboThreadIdBootstrap
#define RoboTidIdle      RoboThreadIdIdle
#define RoboTaskNameMax RoboThreadNameMax
#define RoboKstackSize   RoboThreadKstackSize
#define RoboKstackPages  RoboThreadKstackPages
#define RoboKstackGuardPages RoboThreadKstackGuardPages

#define RoboTaskNew        RoboThreadNew
#define RoboTaskReady      RoboThreadReady
#define RoboTaskRunning    RoboThreadRunning
#define RoboTaskBlocked    RoboThreadBlocked
#define RoboTaskSleeping   RoboThreadSleeping
#define RoboTaskTerminated RoboThreadTerminated

Task *TaskCreate(const char *name, TaskEntry entry, void *arg);
void TaskExit(void);
void TaskYield(void);
void TaskBlock(void);
int TaskUnblock(Task *task);

static inline const char *TaskStateName(TaskState state)
{
    return ThreadStateName(state);
}

#endif
