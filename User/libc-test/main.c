#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <robo/process.h>
#include <robo/thread.h>
#include <robo/socket.h>
#include <robo/file.h>
#include <robo/memory.h>

static int gFail;
static int gCounter;
static Mutex gMu;
static int gErrA;
static int gErrB;

static void fail(const char *n)
{
    printf("[FAIL] %s\n", n);
    gFail = 1;
}

static void ok(const char *n)
{
    printf("[ OK ] %s\n", n);
}

static int TestTypes(void)
{
    if (sizeof(uint8_t) != 1 || sizeof(uint64_t) != 8)
        return 0;
    if (sizeof(size_t) != 8 || sizeof(uintptr_t) != 8)
        return 0;
    if (NULL != 0)
        return 0;
    return 1;
}

static int TestString(void)
{
    char a[16];
    strcpy(a, "abc");
    return strcmp(a, "abc") == 0 && strlen(a) == 3 && strstr(a, "bc") != 0;
}

static int TestMemory(void)
{
    char *message = malloc(64);
    if (!message)
        return 0;
    strcpy(message, "Hello from Roboticela libc!\n");
    printf("%s", message);
    free(message);
    return 1;
}

static int TestStdio(void)
{
    char buf[8];
    int n = snprintf(buf, sizeof(buf), "Hello World");
    return n == 11 && buf[7] == 0;
}

static int TestErrno(void)
{
    int fd = open("/no-such-libc-file", O_RDONLY);
    if (fd >= 0) {
        close(fd);
        return 0;
    }
    return errno == ENOENT;
}

static int TestTime(void)
{
    struct timespec ts;
    if (ClockGettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    if (nanosleep(&(struct timespec){ 0, 1000000L }, 0) != 0)
        return 0;
    return time(0) >= 0;
}

static int TestFiles(void)
{
    int fd;
    char buf[32];
    struct stat st;
    const char *msg = "libc-file\n";

    fd = open("/libc-hello.txt", O_CREAT | O_RDWR | O_TRUNC, 0);
    if (fd < 0)
        return 0;
    if (write(fd, msg, 10) != 10) {
        close(fd);
        return 0;
    }
    if (lseek(fd, 0, SEEK_SET) != 0) {
        close(fd);
        return 0;
    }
    if (read(fd, buf, 10) != 10 || memcmp(buf, msg, 10) != 0) {
        close(fd);
        return 0;
    }
    close(fd);
    if (rename("/libc-hello.txt", "/libc-renamed.txt") != 0)
        return 0;
    if (stat("/libc-renamed.txt", &st) != 0 || st.stSize != 10)
        return 0;
    if (unlink("/libc-renamed.txt") != 0)
        return 0;
    mkdir("/libc-dir", 0);
    rmdir("/libc-dir");
    return 1;
}

static int TestProcess(void)
{
    Pid pid, self, w, pp;
    int st = -1;
    char *argv[] = { "argv-test", "arg1", "arg2", 0 };

    self = getpid();
    pp = getppid();
    if (self == 0) {
        printf("process: getpid 0\n");
        return 0;
    }
    pid = Spawn("/bin/hello", 0);
    if (pid == (Pid)-1) {
        printf("process: spawn hello errno=%d\n", errno);
        return 0;
    }
    w = Wait(pid, &st);
    if (w != pid) {
        printf("process: wait hello w=%d st=%d errno=%d\n", (int)w, st, errno);
        return 0;
    }
    st = -1;
    pid = Spawn("/bin/argv-test", argv);
    if (pid == (Pid)-1) {
        printf("process: spawn argv errno=%d\n", errno);
        return 0;
    }
    w = Wait(pid, &st);
    if (w != pid || st != 0) {
        printf("process: wait argv w=%d st=%d errno=%d ppid=%d\n",
               (int)w, st, errno, (int)pp);
        return 0;
    }
    if (pp == 0) {
        printf("process: getppid 0\n");
        return 0;
    }
    return 1;
}

static void worker(void *arg)
{
    int n = (int)(intptr_t)arg;
    int i;
    errno = n;
    for (i = 0; i < 1000; i++) {
        MutexLock(&gMu);
        gCounter++;
        MutexUnlock(&gMu);
    }
    if (n == 11)
        gErrA = errno;
    else if (n == 22)
        gErrB = errno;
}

static int TestThreads(void)
{
    Thread a, b, c;
    gCounter = 0;
    gErrA = 0;
    gErrB = 0;
    MutexInit(&gMu);
    if (ThreadCreate(&a, worker, (void *)(intptr_t)11) != 0) {
        printf("threads: create a errno=%d\n", errno);
        return 0;
    }
    if (ThreadCreate(&b, worker, (void *)(intptr_t)22) != 0) {
        printf("threads: create b errno=%d\n", errno);
        return 0;
    }
    if (ThreadCreate(&c, worker, (void *)(intptr_t)33) != 0) {
        printf("threads: create c errno=%d\n", errno);
        return 0;
    }
    if (ThreadJoin(a) < 0 || ThreadJoin(b) < 0 ||
        ThreadJoin(c) < 0) {
        printf("threads: join errno=%d\n", errno);
        return 0;
    }
    if (gCounter != 3000) {
        printf("threads: counter=%d\n", gCounter);
        return 0;
    }
    if (gErrA != 11 || gErrB != 22) {
        printf("threads: errno a=%d b=%d\n", gErrA, gErrB);
        return 0;
    }
    return 1;
}

static int TestSockets(void)
{
    int s;
    Sockaddr sa;
    char buf[64];
    const char *hello = "Hello from Roboticela OS\n";
    ssize_t n;

    s = Socket(RoboAfInet, RoboSockStream, 0);
    if (s < 0)
        return 0;
    sa.family = RoboAfInet;
    sa.port = 18019;
    sa.addr = 0x0A000204u;
    sa.zero = 0;
    if (Connect(s, &sa) != 0) {
        close(s);
        return 0;
    }
    if (Send(s, hello, 25) < 0) {
        close(s);
        return 0;
    }
    n = Recv(s, buf, 63);
    close(s);
    if (n < 15)
        return 0;
    return memcmp(buf, "Hello from host", 15) == 0;
}

int main(void)
{
    printf("Roboticela libc test\n");
    printf("--------------------\n\n");

    if (TestTypes())
        ok("stdint");
    else
        fail("stdint");
    if (TestTypes())
        ok("stddef");
    else
        fail("stddef");
    if (TestString())
        ok("string");
    else
        fail("string");
    if (TestMemory())
        ok("memory");
    else
        fail("memory");
    if (TestStdio())
        ok("stdio");
    else
        fail("stdio");
    if (TestErrno())
        ok("errno");
    else
        fail("errno");
    if (TestTime())
        ok("time");
    else
        fail("time");
    if (TestFiles())
        ok("files");
    else
        fail("files");
    if (TestProcess())
        ok("process");
    else
        fail("process");
    if (TestThreads())
        ok("threads");
    else
        fail("threads");
    if (TestSockets())
        ok("sockets");
    else
        fail("sockets");

    if (!gFail) {
        printf("\nAll tests passed.\n");
        printf("[PASS] LIBC\n");
        return 0;
    }
    printf("[FAIL] LIBC\n");
    return 1;
}
