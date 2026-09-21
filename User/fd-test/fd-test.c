#include <robosys/syscall.h>

static int IsErr(Long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static int errcode(Long r)
{
    return (int)(-(int64_t)r);
}

int main(void)
{
    static const char hello[] = "Hello from user space.\n";
    static const char err[] = "error\n";
    static const char path[] = "/test.txt";
    char buf[32];
    Long r;
    int fd;
    unsigned i;

    r = Write(1, hello, 23);
    if (IsErr(r) || r != 23)
        Exit(1);

    r = Write(2, err, 6);
    if (IsErr(r) || r != 6)
        Exit(2);

    r = Write(1, hello, 0);
    if (IsErr(r) || r != 0)
        Exit(3);

    r = Write(99, hello, 5);
    if (!IsErr(r) || errcode(r) != RoboEbadf)
        Exit(4);

    r = Write((int)(-1), hello, 5);
    if (!IsErr(r) || errcode(r) != RoboEbadf)
        Exit(5);

    r = Read(1, buf, 4);
    if (!IsErr(r) || errcode(r) != RoboEbadf)
        Exit(6);

    r = Write(1, (const void *)0, 8);
    if (!IsErr(r) || errcode(r) != RoboEfault)
        Exit(7);

    fd = (int)Open(path, (int)RoboORdonly, 0);
    if (IsErr((Long)fd) || fd < 3)
        Exit(8);

    for (i = 0; i < 32; i++)
        buf[i] = 0;
    r = Read(fd, buf, 13);
    if (IsErr(r) || r != 13)
        Exit(9);
    r = Write(1, buf, 13);
    if (IsErr(r) || r != 13)
        Exit(10);

    r = Close(fd);
    if (IsErr(r))
        Exit(11);
    r = Close(fd);
    if (!IsErr(r) || errcode(r) != RoboEbadf)
        Exit(12);

    r = Close(999);
    if (!IsErr(r) || errcode(r) != RoboEbadf)
        Exit(13);

    r = Read(0, buf, 4);
    if (!IsErr(r) || errcode(r) != RoboEnotsup)
        Exit(14);

    r = Read(3, (void *)(uintptr_t)0x401000ull, 4);
    if (!IsErr(r) || (errcode(r) != RoboEbadf && errcode(r) != RoboEfault))
        Exit(15);

    return 0;
}
