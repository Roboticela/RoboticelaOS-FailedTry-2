#ifndef ROBO_THREAD_H
#define ROBO_THREAD_H

#include <stdint.h>
#include <sys/types.h>

typedef uint64_t robo_thread_t;
typedef void (*robo_thread_fn)(void *arg);

typedef struct {
    volatile int lock;
} robo_mutex_t;

typedef struct {
    volatile int waiters;
} robo_cond_t;

typedef struct {
    volatile int value;
} robo_sem_t;

int robo_thread_create(robo_thread_t *out, robo_thread_fn fn, void *arg);
void robo_thread_exit(void);
int robo_thread_join(robo_thread_t tid);
int robo_thread_yield(void);
tid_t robo_gettid(void);

int robo_mutex_init(robo_mutex_t *m);
int robo_mutex_lock(robo_mutex_t *m);
int robo_mutex_unlock(robo_mutex_t *m);

int robo_cond_init(robo_cond_t *c);
int robo_cond_wait(robo_cond_t *c, robo_mutex_t *m);
int robo_cond_signal(robo_cond_t *c);

int robo_sem_init(robo_sem_t *s, int value);
int robo_sem_wait(robo_sem_t *s);
int robo_sem_post(robo_sem_t *s);

#endif
