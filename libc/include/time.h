#ifndef ROBO_LIBC_TIME_H
#define ROBO_LIBC_TIME_H

#include <stdint.h>
#include <stddef.h>

typedef int64_t time_t;
typedef int64_t clock_t;

#define CLOCKS_PER_SEC 1000000
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

struct timeval {
    time_t tv_sec;
    long tv_usec;
};

time_t time(time_t *out);
clock_t clock(void);
int clock_gettime(int clock_id, struct timespec *ts);
int nanosleep(const struct timespec *req, struct timespec *rem);

#endif
