#include <time.h>
#include <errno.h>
#include <internal/libc.h>

#ifndef LIBC_HOST_TEST
#include <robosys/syscall_numbers.h>

static uint64_t monotonic_ns(void)
{
    robo_long r = robo_syscall((robo_long)ROBO_SYS_TIME, 0, 0, 0, 0, 0, 0);
    if (robo_set_err(r))
        return 0;
    return (uint64_t)r;
}

time_t time(time_t *out)
{
    time_t s = (time_t)(monotonic_ns() / 1000000000ull);
    if (out)
        *out = s;
    return s;
}

clock_t clock(void)
{
    return (clock_t)(monotonic_ns() / 1000ull);
}

int clock_gettime(int clock_id, struct timespec *ts)
{
    uint64_t ns;
    if (!ts) {
        errno = EINVAL;
        return -1;
    }
    (void)clock_id;
    ns = monotonic_ns();
    ts->tv_sec = (time_t)(ns / 1000000000ull);
    ts->tv_nsec = (long)(ns % 1000000000ull);
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    uint64_t ms;
    robo_long r;
    if (!req || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }
    ms = (uint64_t)req->tv_sec * 1000ull + ((uint64_t)req->tv_nsec + 999999ull) / 1000000ull;
    if (!ms)
        ms = 1;
    r = robo_syscall((robo_long)ROBO_SYS_SLEEP, (robo_long)ms, 0, 0, 0, 0, 0);
    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    if (robo_set_err(r))
        return -1;
    return 0;
}

#else

time_t time(time_t *out)
{
    if (out)
        *out = 0;
    return 0;
}

clock_t clock(void)
{
    return 0;
}

int clock_gettime(int clock_id, struct timespec *ts)
{
    (void)clock_id;
    if (!ts)
        return -1;
    ts->tv_sec = 0;
    ts->tv_nsec = 0;
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    (void)req;
    (void)rem;
    return 0;
}

#endif
