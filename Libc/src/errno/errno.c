#include <internal/libc.h>
#include <errno.h>
#include <stddef.h>

#ifdef LIBC_HOST_TEST
static int g_host_errno;

int *__errno_location(void)
{
    return &g_host_errno;
}

void robo_tls_init(void)
{
}

int robo_is_err(robo_long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

int robo_set_err(robo_long r)
{
    if (!robo_is_err(r))
        return 0;
    errno = (int)(-(int64_t)r);
    return 1;
}

#else

struct RoboTcb g_boot_tcb = { &g_boot_tcb, 0 };

int *__errno_location(void)
{
    struct RoboTcb *t = 0;
    __asm__ volatile ("mov %%fs:0, %0" : "=r"(t));
    if (!t)
        return &g_boot_tcb.err;
    return &t->err;
}

int robo_is_err(robo_long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

int robo_set_err(robo_long r)
{
    if (!robo_is_err(r))
        return 0;
    errno = (int)(-(int64_t)r);
    return 1;
}

#endif
