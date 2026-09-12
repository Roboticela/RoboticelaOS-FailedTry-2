#include <robo/thread.h>

int robo_thread_selftest(void)
{
    robo_mutex_t m;
    return robo_mutex_init(&m) == 0;
}
