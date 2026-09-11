#include "scheduler/task.h"
#include "thread/thread.h"

robo_task_t *robo_task_create(const char *name, robo_task_entry_t entry, void *arg)
{
    return robo_thread_create(name, entry, arg);
}

void robo_task_exit(void)
{
    robo_thread_exit();
}

void robo_task_yield(void)
{
    robo_thread_yield();
}

void robo_task_block(void)
{
    robo_thread_block();
}

int robo_task_unblock(robo_task_t *task)
{
    return robo_thread_wake(task);
}
