#include <robosys/syscall.h>

extern long robo_reg_preserve_test(void);
extern void worker(unsigned char *page);

static int is_err(robo_long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static int errcode(robo_long r)
{
    return (int)(-(int64_t)r);
}

static void die(int code)
{
    robo_exit(code);
}

int main(void)
{
    robo_long r;
    uint64_t t0, t1, t2, t3;
    void *map;
    unsigned char *p;
    int fd;
    unsigned char mag[4];
    unsigned i;
    RoboAbiInfo info;
    static const char hello_path[] = "/bin/hello";
    static const char missing[] = "/bin/missing-xyz";
    static const char path_a[] = "/bin/a";
    static const char path_b[] = "/bin/b";

    r = robo_abi_info(&info);
    if (is_err(r) || info.major != 1 || info.minor != 0)
        die(10);
    if ((r & 0xFFFFFFFFull) != 1)
        die(11);

    t0 = robo_time();
    t1 = robo_time();
    t2 = robo_time();
    if (t1 < t0 || t2 < t1)
        die(12);

    r = robo_write(99, &info, 4);
    if (!is_err(r) || errcode(r) != ROBO_EBADF)
        die(13);

    r = robo_write(1, (const void *)0, 8);
    if (!is_err(r) || errcode(r) != ROBO_EFAULT)
        die(14);

    r = robo_write(1, (const void *)(uintptr_t)0x401000ull, 8);
    if (!is_err(r) || errcode(r) != ROBO_EFAULT)
        die(15);

    r = robo_syscall((robo_long)0xFFFFFFFFull, 0, 0, 0, 0, 0, 0);
    if (!is_err(r) || errcode(r) != ROBO_ENOSYS)
        die(16);

    r = robo_syscall(13, 0, 0, 0, 0, 0, 0);
    if (!is_err(r) || errcode(r) != ROBO_ENOSYS)
        die(17);

    t0 = robo_time();
    r = robo_sleep(50);
    t1 = robo_time();
    if (is_err(r) || t1 < t0)
        die(18);

    map = (void *)(uintptr_t)robo_mmap(0, 4096,
                                       (int)(ROBO_PROT_READ | ROBO_PROT_WRITE),
                                       (int)(ROBO_MAP_PRIVATE | ROBO_MAP_ANON),
                                       -1, 0);
    if (is_err((robo_long)(uintptr_t)map))
        die(19);
    p = (unsigned char *)map;
    p[0] = 0x5A;
    if (p[0] != 0x5A)
        die(20);
    r = robo_munmap(map, 4096);
    if (is_err(r))
        die(21);

    r = robo_mmap(0, 0, (int)ROBO_PROT_READ,
                  (int)(ROBO_MAP_PRIVATE | ROBO_MAP_ANON), -1, 0);
    if (!is_err(r) || errcode(r) != ROBO_EINVAL)
        die(22);

    r = robo_mmap((void *)(uintptr_t)0x401000ull, 4096,
                  (int)ROBO_PROT_READ,
                  (int)(ROBO_MAP_PRIVATE | ROBO_MAP_ANON | ROBO_MAP_FIXED),
                  -1, 0);
    if (!is_err(r) || (errcode(r) != ROBO_EFAULT && errcode(r) != ROBO_EINVAL))
        die(23);

    r = robo_mmap((void *)(uintptr_t)0x8000000000000000ull, 4096,
                  (int)ROBO_PROT_READ,
                  (int)(ROBO_MAP_PRIVATE | ROBO_MAP_ANON | ROBO_MAP_FIXED),
                  -1, 0);
    if (!is_err(r))
        die(24);

    fd = (int)robo_open(hello_path, (int)ROBO_O_RDONLY, 0);
    if (is_err((robo_long)fd) || fd < 3)
        die(25);
    r = robo_read(fd, mag, 4);
    if (is_err(r) || r != 4 || mag[0] != 0x7F || mag[1] != 'E' ||
        mag[2] != 'L' || mag[3] != 'F')
        die(26);
    r = robo_close(fd);
    if (is_err(r))
        die(27);

    r = robo_open(missing, (int)ROBO_O_RDONLY, 0);
    if (!is_err(r) || errcode(r) != ROBO_ENOENT)
        die(28);

    r = robo_read(1, mag, 4);
    if (!is_err(r) || errcode(r) != ROBO_EBADF)
        die(29);

    r = robo_close(99);
    if (!is_err(r) || errcode(r) != ROBO_EBADF)
        die(30);

    r = robo_ipc(1, 0, 0, 0, 0);
    if (!is_err(r) || errcode(r) != ROBO_ENOSYS)
        die(31);

    map = (void *)(uintptr_t)robo_mmap(0, 4096,
                                       (int)(ROBO_PROT_READ | ROBO_PROT_WRITE),
                                       (int)(ROBO_MAP_PRIVATE | ROBO_MAP_ANON),
                                       -1, 0);
    if (is_err((robo_long)(uintptr_t)map))
        die(32);
    p = (unsigned char *)map;
    *p = 0;
    {
        void *stk;
        unsigned char *top;
        stk = (void *)(uintptr_t)robo_mmap(0, 16384,
                                           (int)(ROBO_PROT_READ | ROBO_PROT_WRITE),
                                           (int)(ROBO_MAP_PRIVATE | ROBO_MAP_ANON),
                                           -1, 0);
        if (is_err((robo_long)(uintptr_t)stk))
            die(33);
        top = (unsigned char *)stk + 16384;
        r = robo_thread_create((void *)(uintptr_t)worker, top, p);
        if (is_err(r))
            die(34);
        robo_sleep(40);
        if (*p != 0xABu)
            die(35);
    }

    r = robo_process_create(path_a);
    if (is_err(r) || r < 2)
        die(36);
    r = robo_process_create(path_b);
    if (is_err(r) || r < 2)
        die(37);
    robo_sleep(80);

    t3 = robo_time();
    if (t3 < t2)
        die(38);

    if (robo_reg_preserve_test() != 0)
        die(39);

    for (i = 0; i < 8; i++) {
        r = robo_syscall((robo_long)(0x1000u + i * 17u), 1, 2, 3, 4, 5, 6);
        if (!is_err(r) || errcode(r) != ROBO_ENOSYS)
            die(40);
    }

    r = robo_mmap(0, 4096,
                  (int)(ROBO_PROT_WRITE | ROBO_PROT_EXEC),
                  (int)(ROBO_MAP_PRIVATE | ROBO_MAP_ANON), -1, 0);
    if (!is_err(r) || errcode(r) != ROBO_EINVAL)
        die(41);

    return 0;
}
