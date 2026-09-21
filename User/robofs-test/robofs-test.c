#include <robosys/syscall.h>

static int IsErr(Long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static int eq(const char *a, const char *b, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

int main(void)
{
    static const char msg[] = "Hello from RoboFS.\n";
    char buf[64];
    RoboStat st;
    Long r;
    int fd;
    unsigned i;

    fd = (int)Open("/hello.txt",
                        (int)(RoboOCreat | RoboORdwr | RoboOTrunc), 0);
    if (IsErr((Long)fd) || fd < 3)
        Exit(1);

    r = Write(fd, msg, 19);
    if (IsErr(r) || r != 19)
        Exit(2);

    r = Seek(fd, 0, RoboSeekSet);
    if (IsErr(r) || r != 0)
        Exit(3);

    for (i = 0; i < 64; i++)
        buf[i] = 0;
    r = Read(fd, buf, 19);
    if (IsErr(r) || r != 19 || !eq(buf, msg, 19))
        Exit(4);

    r = Write(1, buf, 19);
    if (IsErr(r) || r != 19)
        Exit(5);

    r = Close(fd);
    if (IsErr(r))
        Exit(6);

    r = Rename("/hello.txt", "/renamed.txt");
    if (IsErr(r))
        Exit(7);

    r = Stat("/renamed.txt", &st);
    if (IsErr(r) || st.size != 19)
        Exit(8);

    r = Unlink("/renamed.txt");
    if (IsErr(r))
        Exit(9);

    r = RoboMkdir("/test", 0);
    if (IsErr(r))
        Exit(10);

    fd = (int)Open("/test/a.txt",
                        (int)(RoboOCreat | RoboORdwr), 0);
    if (IsErr((Long)fd))
        Exit(11);
    Close(fd);

    fd = (int)Open("/test/b.txt",
                        (int)(RoboOCreat | RoboORdwr), 0);
    if (IsErr((Long)fd))
        Exit(12);
    Close(fd);

    r = Rename("/test/a.txt", "/test/c.txt");
    if (IsErr(r))
        Exit(13);

    r = Stat("/test/c.txt", &st);
    if (IsErr(r))
        Exit(14);

    r = Unlink("/test/b.txt");
    if (IsErr(r))
        Exit(15);

    r = Unlink("/test/c.txt");
    if (IsErr(r))
        Exit(16);

    r = Rmdir("/test");
    if (IsErr(r))
        Exit(17);

    r = Stat("/test", &st);
    if (!IsErr(r))
        Exit(18);

    return 0;
}
