#include <fcntl.h>
#include <unistd.h>

int robo_file_selftest(void)
{
    return open("/no-such", O_RDONLY) < 0;
}
