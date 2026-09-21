#include <robo/thread.h>

int ThreadSelftest(void)
{
    Mutex m;
    return MutexInit(&m) == 0;
}
