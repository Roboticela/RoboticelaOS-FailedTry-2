#include <unistd.h>

int ProcessSelftest(void)
{
    return getpid() != 0;
}
