#include <unistd.h>

int robo_process_selftest(void)
{
    return getpid() != 0;
}
