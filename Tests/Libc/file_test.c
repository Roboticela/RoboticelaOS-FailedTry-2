#include <fcntl.h>
#include <unistd.h>

int FileSelftest(void)
{
    return open("/no-such", O_RDONLY) < 0;
}
