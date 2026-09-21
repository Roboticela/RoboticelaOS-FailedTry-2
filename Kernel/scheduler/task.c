#include "scheduler/task.h"
#include "thread/thread.h"

Task *TaskCreate(const char *name, TaskEntry entry, void *arg)
{
    return ThreadCreate(name, entry, arg);
}

void TaskExit(void)
{
    ThreadExit();
}

void TaskYield(void)
{
    ThreadYield();
}

void TaskBlock(void)
{
    ThreadBlock();
}

int TaskUnblock(Task *task)
{
    return ThreadWake(task);
}
