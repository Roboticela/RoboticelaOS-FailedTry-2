#include <robosys/syscall.h>

static int is_err(robo_long r)
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
    robo_long r;
    int fd;
    unsigned i;

    fd = (int)robo_open("/hello.txt",
                        (int)(ROBO_O_CREAT | ROBO_O_RDWR | ROBO_O_TRUNC), 0);
    if (is_err((robo_long)fd) || fd < 3)
        robo_exit(1);

    r = robo_write(fd, msg, 19);
    if (is_err(r) || r != 19)
        robo_exit(2);

    r = robo_seek(fd, 0, ROBO_SEEK_SET);
    if (is_err(r) || r != 0)
        robo_exit(3);

    for (i = 0; i < 64; i++)
        buf[i] = 0;
    r = robo_read(fd, buf, 19);
    if (is_err(r) || r != 19 || !eq(buf, msg, 19))
        robo_exit(4);

    r = robo_write(1, buf, 19);
    if (is_err(r) || r != 19)
        robo_exit(5);

    r = robo_close(fd);
    if (is_err(r))
        robo_exit(6);

    r = robo_rename("/hello.txt", "/renamed.txt");
    if (is_err(r))
        robo_exit(7);

    r = robo_stat("/renamed.txt", &st);
    if (is_err(r) || st.size != 19)
        robo_exit(8);

    r = robo_unlink("/renamed.txt");
    if (is_err(r))
        robo_exit(9);

    r = robo_mkdir("/test", 0);
    if (is_err(r))
        robo_exit(10);

    fd = (int)robo_open("/test/a.txt",
                        (int)(ROBO_O_CREAT | ROBO_O_RDWR), 0);
    if (is_err((robo_long)fd))
        robo_exit(11);
    robo_close(fd);

    fd = (int)robo_open("/test/b.txt",
                        (int)(ROBO_O_CREAT | ROBO_O_RDWR), 0);
    if (is_err((robo_long)fd))
        robo_exit(12);
    robo_close(fd);

    r = robo_rename("/test/a.txt", "/test/c.txt");
    if (is_err(r))
        robo_exit(13);

    r = robo_stat("/test/c.txt", &st);
    if (is_err(r))
        robo_exit(14);

    r = robo_unlink("/test/b.txt");
    if (is_err(r))
        robo_exit(15);

    r = robo_unlink("/test/c.txt");
    if (is_err(r))
        robo_exit(16);

    r = robo_rmdir("/test");
    if (is_err(r))
        robo_exit(17);

    r = robo_stat("/test", &st);
    if (!is_err(r))
        robo_exit(18);

    return 0;
}
