#include <robosys/syscall.h>

static int is_err(robo_long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static int errcode(robo_long r)
{
    return (int)(-(int64_t)r);
}

int main(void)
{
    static const char hello[] = "Hello from user space.\n";
    static const char err[] = "error\n";
    static const char path[] = "/test.txt";
    char buf[32];
    robo_long r;
    int fd;
    unsigned i;

    r = robo_write(1, hello, 23);
    if (is_err(r) || r != 23)
        robo_exit(1);

    r = robo_write(2, err, 6);
    if (is_err(r) || r != 6)
        robo_exit(2);

    r = robo_write(1, hello, 0);
    if (is_err(r) || r != 0)
        robo_exit(3);

    r = robo_write(99, hello, 5);
    if (!is_err(r) || errcode(r) != ROBO_EBADF)
        robo_exit(4);

    r = robo_write((int)(-1), hello, 5);
    if (!is_err(r) || errcode(r) != ROBO_EBADF)
        robo_exit(5);

    r = robo_read(1, buf, 4);
    if (!is_err(r) || errcode(r) != ROBO_EBADF)
        robo_exit(6);

    r = robo_write(1, (const void *)0, 8);
    if (!is_err(r) || errcode(r) != ROBO_EFAULT)
        robo_exit(7);

    fd = (int)robo_open(path, (int)ROBO_O_RDONLY, 0);
    if (is_err((robo_long)fd) || fd < 3)
        robo_exit(8);

    for (i = 0; i < 32; i++)
        buf[i] = 0;
    r = robo_read(fd, buf, 13);
    if (is_err(r) || r != 13)
        robo_exit(9);
    r = robo_write(1, buf, 13);
    if (is_err(r) || r != 13)
        robo_exit(10);

    r = robo_close(fd);
    if (is_err(r))
        robo_exit(11);
    r = robo_close(fd);
    if (!is_err(r) || errcode(r) != ROBO_EBADF)
        robo_exit(12);

    r = robo_close(999);
    if (!is_err(r) || errcode(r) != ROBO_EBADF)
        robo_exit(13);

    r = robo_read(0, buf, 4);
    if (!is_err(r) || errcode(r) != ROBO_ENOTSUP)
        robo_exit(14);

    r = robo_read(3, (void *)(uintptr_t)0x401000ull, 4);
    if (!is_err(r) || (errcode(r) != ROBO_EBADF && errcode(r) != ROBO_EFAULT))
        robo_exit(15);

    return 0;
}
