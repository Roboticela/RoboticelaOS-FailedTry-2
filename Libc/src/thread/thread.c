#include <robo/thread.h>
#include <robo/memory.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <internal/libc.h>

#ifndef LIBC_HOST_TEST
#include <robosys/syscall_numbers.h>

#define ROBO_UTHREAD_STACK (64u * 1024u)

struct spawn {
    robo_thread_fn fn;
    void *arg;
    struct RoboTcb *tcb;
};

__attribute__((used, noinline))
static void thread_tramp(void *p)
{
    struct spawn s = *(struct spawn *)p;
    s.tcb->self = s.tcb;
    s.tcb->err = 0;
    robo_syscall((robo_long)RoboSysThread, (robo_long)RoboThreadOpSetFs,
                 (robo_long)(uintptr_t)s.tcb, 0, 0, 0, 0);
    s.fn(s.arg);
    robo_thread_exit();
}

int robo_thread_create(robo_thread_t *out, robo_thread_fn fn, void *arg)
{
    void *stack;
    struct RoboTcb *tcb;
    struct spawn *s;
    uintptr_t rsp;
    robo_long tid;

    if (!out || !fn) {
        errno = EINVAL;
        return -1;
    }
    stack = robo_mmap(0, ROBO_UTHREAD_STACK, ROBO_PROT_READ | ROBO_PROT_WRITE,
                      ROBO_MAP_PRIVATE | ROBO_MAP_ANON, -1, 0);
    if (!stack || stack == (void *)-1)
        return -1;
    tcb = (struct RoboTcb *)robo_mmap(0, 4096, ROBO_PROT_READ | ROBO_PROT_WRITE,
                                      ROBO_MAP_PRIVATE | ROBO_MAP_ANON, -1, 0);
    if (!tcb || tcb == (void *)-1) {
        robo_munmap(stack, ROBO_UTHREAD_STACK);
        return -1;
    }
    rsp = (uintptr_t)stack + ROBO_UTHREAD_STACK;
    rsp &= ~(uintptr_t)15;
    rsp -= sizeof(struct spawn);
    s = (struct spawn *)rsp;
    s->fn = fn;
    s->arg = arg;
    s->tcb = tcb;
    tcb->self = tcb;
    tcb->err = 0;
    tid = robo_syscall((robo_long)RoboSysThreadCreate,
                       (robo_long)(uintptr_t)thread_tramp,
                       (robo_long)rsp, (robo_long)(uintptr_t)s, 0, 0, 0);
    if (robo_set_err(tid)) {
        robo_munmap(tcb, 4096);
        robo_munmap(stack, ROBO_UTHREAD_STACK);
        return -1;
    }
    *out = (robo_thread_t)tid;
    return 0;
}

void robo_thread_exit(void)
{
    robo_syscall((robo_long)RoboSysThread, (robo_long)RoboThreadOpExit,
                 0, 0, 0, 0, 0);
    for (;;)
        ;
}

int robo_thread_join(robo_thread_t tid)
{
    robo_long r = robo_syscall((robo_long)RoboSysThread,
                               (robo_long)RoboThreadOpJoin,
                               (robo_long)tid, 0, 0, 0, 0);
    if (robo_set_err(r))
        return -1;
    return (int)r;
}

int robo_thread_yield(void)
{
    robo_long r = robo_syscall((robo_long)RoboSysThread,
                               (robo_long)RoboThreadOpYield, 0, 0, 0, 0, 0);
    if (robo_set_err(r))
        return -1;
    return 0;
}

tid_t robo_gettid(void)
{
    return (tid_t)robo_syscall((robo_long)RoboSysThread,
                               (robo_long)RoboThreadOpGettid, 0, 0, 0, 0, 0);
}

int robo_mutex_init(robo_mutex_t *m)
{
    if (!m)
        return -1;
    m->lock = 0;
    return 0;
}

int robo_mutex_lock(robo_mutex_t *m)
{
    if (!m)
        return -1;
    while (__sync_lock_test_and_set(&m->lock, 1))
        robo_thread_yield();
    return 0;
}

int robo_mutex_unlock(robo_mutex_t *m)
{
    if (!m)
        return -1;
    __sync_lock_release(&m->lock);
    return 0;
}

int robo_cond_init(robo_cond_t *c)
{
    if (!c)
        return -1;
    c->waiters = 0;
    return 0;
}

int robo_cond_wait(robo_cond_t *c, robo_mutex_t *m)
{
    if (!c || !m)
        return -1;
    c->waiters++;
    robo_mutex_unlock(m);
    while (c->waiters)
        robo_thread_yield();
    robo_mutex_lock(m);
    return 0;
}

int robo_cond_signal(robo_cond_t *c)
{
    if (!c)
        return -1;
    if (c->waiters)
        c->waiters--;
    return 0;
}

int robo_sem_init(robo_sem_t *s, int value)
{
    if (!s)
        return -1;
    s->value = value;
    return 0;
}

int robo_sem_wait(robo_sem_t *s)
{
    if (!s)
        return -1;
    for (;;) {
        int v = s->value;
        if (v > 0 && __sync_bool_compare_and_swap(&s->value, v, v - 1))
            return 0;
        robo_thread_yield();
    }
}

int robo_sem_post(robo_sem_t *s)
{
    if (!s)
        return -1;
    __sync_fetch_and_add(&s->value, 1);
    return 0;
}

#endif
