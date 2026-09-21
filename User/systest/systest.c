#include <robosys/syscall.h>

extern long RegPreserveTest(void);
extern void worker(unsigned char *page);

static int IsErr(Long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static int errcode(Long r)
{
    return (int)(-(int64_t)r);
}

static void die(int code)
{
    Exit(code);
}

int main(void)
{
    Long r;
    uint64_t t0, t1, t2, t3;
    void *map;
    unsigned char *p;
    int fd;
    unsigned char mag[4];
    unsigned i;
    RoboAbiInfo info;
    static const char helloPath[] = "/bin/hello";
    static const char missing[] = "/bin/missing-xyz";
    static const char pathA[] = "/bin/a";
    static const char pathB[] = "/bin/b";

    r = AbiInfo(&info);
    if (IsErr(r) || info.major != 1 || info.minor != 0)
        die(10);
    if ((r & 0xFFFFFFFFull) != 1)
        die(11);

    t0 = Time();
    t1 = Time();
    t2 = Time();
    if (t1 < t0 || t2 < t1)
        die(12);

    r = Write(99, &info, 4);
    if (!IsErr(r) || errcode(r) != RoboEbadf)
        die(13);

    r = Write(1, (const void *)0, 8);
    if (!IsErr(r) || errcode(r) != RoboEfault)
        die(14);

    r = Write(1, (const void *)(uintptr_t)0x401000ull, 8);
    if (!IsErr(r) || errcode(r) != RoboEfault)
        die(15);

    r = Syscall((Long)0xFFFFFFFFull, 0, 0, 0, 0, 0, 0);
    if (!IsErr(r) || errcode(r) != RoboEnosys)
        die(16);

    r = Syscall(13, 0, 0, 0, 0, 0, 0);
    if (!IsErr(r) || errcode(r) != RoboEnosys)
        die(17);

    t0 = Time();
    r = RoboSleep(50);
    t1 = Time();
    if (IsErr(r) || t1 < t0)
        die(18);

    map = (void *)(uintptr_t)Mmap(0, 4096,
                                       (int)(RoboProtRead | RoboProtWrite),
                                       (int)(RoboMapPrivate | RoboMapAnon),
                                       -1, 0);
    if (IsErr((Long)(uintptr_t)map))
        die(19);
    p = (unsigned char *)map;
    p[0] = 0x5A;
    if (p[0] != 0x5A)
        die(20);
    r = Munmap(map, 4096);
    if (IsErr(r))
        die(21);

    r = Mmap(0, 0, (int)RoboProtRead,
                  (int)(RoboMapPrivate | RoboMapAnon), -1, 0);
    if (!IsErr(r) || errcode(r) != RoboEinval)
        die(22);

    r = Mmap((void *)(uintptr_t)0x401000ull, 4096,
                  (int)RoboProtRead,
                  (int)(RoboMapPrivate | RoboMapAnon | RoboMapFixed),
                  -1, 0);
    if (!IsErr(r) || (errcode(r) != RoboEfault && errcode(r) != RoboEinval))
        die(23);

    r = Mmap((void *)(uintptr_t)0x8000000000000000ull, 4096,
                  (int)RoboProtRead,
                  (int)(RoboMapPrivate | RoboMapAnon | RoboMapFixed),
                  -1, 0);
    if (!IsErr(r))
        die(24);

    fd = (int)Open(helloPath, (int)RoboORdonly, 0);
    if (IsErr((Long)fd) || fd < 3)
        die(25);
    r = Read(fd, mag, 4);
    if (IsErr(r) || r != 4 || mag[0] != 0x7F || mag[1] != 'E' ||
        mag[2] != 'L' || mag[3] != 'F')
        die(26);
    r = Close(fd);
    if (IsErr(r))
        die(27);

    r = Open(missing, (int)RoboORdonly, 0);
    if (!IsErr(r) || errcode(r) != RoboEnoent)
        die(28);

    r = Read(1, mag, 4);
    if (!IsErr(r) || errcode(r) != RoboEbadf)
        die(29);

    r = Close(99);
    if (!IsErr(r) || errcode(r) != RoboEbadf)
        die(30);

    r = Ipc(1, 0, 0, 0, 0);
    if (!IsErr(r) || errcode(r) != RoboEbadf)
        die(31);

    map = (void *)(uintptr_t)Mmap(0, 4096,
                                       (int)(RoboProtRead | RoboProtWrite),
                                       (int)(RoboMapPrivate | RoboMapAnon),
                                       -1, 0);
    if (IsErr((Long)(uintptr_t)map))
        die(32);
    p = (unsigned char *)map;
    *p = 0;
    {
        void *stk;
        unsigned char *top;
        stk = (void *)(uintptr_t)Mmap(0, 16384,
                                           (int)(RoboProtRead | RoboProtWrite),
                                           (int)(RoboMapPrivate | RoboMapAnon),
                                           -1, 0);
        if (IsErr((Long)(uintptr_t)stk))
            die(33);
        top = (unsigned char *)stk + 16384;
        r = ThreadCreate((void *)(uintptr_t)worker, top, p);
        if (IsErr(r))
            die(34);
        RoboSleep(40);
        if (*p != 0xABu)
            die(35);
    }

    r = ProcessCreate(pathA);
    if (IsErr(r) || r < 2)
        die(36);
    r = ProcessCreate(pathB);
    if (IsErr(r) || r < 2)
        die(37);
    RoboSleep(80);

    t3 = Time();
    if (t3 < t2)
        die(38);

    if (RegPreserveTest() != 0)
        die(39);

    for (i = 0; i < 8; i++) {
        r = Syscall((Long)(0x1000u + i * 17u), 1, 2, 3, 4, 5, 6);
        if (!IsErr(r) || errcode(r) != RoboEnosys)
            die(40);
    }

    r = Mmap(0, 4096,
                  (int)(RoboProtWrite | RoboProtExec),
                  (int)(RoboMapPrivate | RoboMapAnon), -1, 0);
    if (!IsErr(r) || errcode(r) != RoboEinval)
        die(41);

    return 0;
}
