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

static int g_fail;
static int g_counter;
static robo_mutex_t g_mu;
static int g_err_a;
static int g_err_b;

static void fail(const char *n)
{
    printf("[FAIL] %s\n", n);
    g_fail = 1;
}

static void ok(const char *n)
{
    printf("[ OK ] %s\n", n);
}

static int test_types(void)
{
    if (sizeof(uint8_t) != 1 || sizeof(uint64_t) != 8)
        return 0;
    if (sizeof(size_t) != 8 || sizeof(uintptr_t) != 8)
        return 0;
    if (NULL != 0)
        return 0;
    return 1;
}

static int test_string(void)
{
    char a[16];
    strcpy(a, "abc");
    return strcmp(a, "abc") == 0 && strlen(a) == 3 && strstr(a, "bc") != 0;
}

static int test_memory(void)
{
    char *message = malloc(64);
    if (!message)
        return 0;
    strcpy(message, "Hello from Roboticela libc!\n");
    printf("%s", message);
    free(message);
    return 1;
}

static int test_stdio(void)
{
    char buf[8];
    int n = snprintf(buf, sizeof(buf), "Hello World");
    return n == 11 && buf[7] == 0;
}

static int test_errno(void)
{
    int fd = open("/no-such-libc-file", O_RDONLY);
    if (fd >= 0) {
        close(fd);
        return 0;
    }
    return errno == ENOENT;
}

static int test_time(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    if (nanosleep(&(struct timespec){ 0, 1000000L }, 0) != 0)
        return 0;
    return time(0) >= 0;
}

static int test_files(void)
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
    if (stat("/libc-renamed.txt", &st) != 0 || st.st_size != 10)
        return 0;
    if (unlink("/libc-renamed.txt") != 0)
        return 0;
    mkdir("/libc-dir", 0);
    rmdir("/libc-dir");
    return 1;
}

static int test_process(void)
{
    pid_t pid, self, w, pp;
    int st = -1;
    char *argv[] = { "argv-test", "arg1", "arg2", 0 };

    self = getpid();
    pp = getppid();
    if (self == 0) {
        printf("process: getpid 0\n");
        return 0;
    }
    pid = robo_spawn("/bin/hello", 0);
    if (pid == (pid_t)-1) {
        printf("process: spawn hello errno=%d\n", errno);
        return 0;
    }
    w = robo_wait(pid, &st);
    if (w != pid) {
        printf("process: wait hello w=%d st=%d errno=%d\n", (int)w, st, errno);
        return 0;
    }
    st = -1;
    pid = robo_spawn("/bin/argv-test", argv);
    if (pid == (pid_t)-1) {
        printf("process: spawn argv errno=%d\n", errno);
        return 0;
    }
    w = robo_wait(pid, &st);
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
        robo_mutex_lock(&g_mu);
        g_counter++;
        robo_mutex_unlock(&g_mu);
    }
    if (n == 11)
        g_err_a = errno;
    else if (n == 22)
        g_err_b = errno;
}

static int test_threads(void)
{
    robo_thread_t a, b, c;
    g_counter = 0;
    g_err_a = 0;
    g_err_b = 0;
    robo_mutex_init(&g_mu);
    if (robo_thread_create(&a, worker, (void *)(intptr_t)11) != 0) {
        printf("threads: create a errno=%d\n", errno);
        return 0;
    }
    if (robo_thread_create(&b, worker, (void *)(intptr_t)22) != 0) {
        printf("threads: create b errno=%d\n", errno);
        return 0;
    }
    if (robo_thread_create(&c, worker, (void *)(intptr_t)33) != 0) {
        printf("threads: create c errno=%d\n", errno);
        return 0;
    }
    if (robo_thread_join(a) < 0 || robo_thread_join(b) < 0 ||
        robo_thread_join(c) < 0) {
        printf("threads: join errno=%d\n", errno);
        return 0;
    }
    if (g_counter != 3000) {
        printf("threads: counter=%d\n", g_counter);
        return 0;
    }
    if (g_err_a != 11 || g_err_b != 22) {
        printf("threads: errno a=%d b=%d\n", g_err_a, g_err_b);
        return 0;
    }
    return 1;
}

static int test_sockets(void)
{
    int s;
    robo_sockaddr_t sa;
    char buf[64];
    const char *hello = "Hello from Roboticela OS\n";
    ssize_t n;

    s = robo_socket(ROBO_AF_INET, ROBO_SOCK_STREAM, 0);
    if (s < 0)
        return 0;
    sa.family = ROBO_AF_INET;
    sa.port = 18019;
    sa.addr = 0x0A000204u;
    sa.zero = 0;
    if (robo_connect(s, &sa) != 0) {
        close(s);
        return 0;
    }
    if (robo_send(s, hello, 25) < 0) {
        close(s);
        return 0;
    }
    n = robo_recv(s, buf, 63);
    close(s);
    if (n < 15)
        return 0;
    return memcmp(buf, "Hello from host", 15) == 0;
}

int main(void)
{
    printf("Roboticela libc test\n");
    printf("--------------------\n\n");

    if (test_types())
        ok("stdint");
    else
        fail("stdint");
    if (test_types())
        ok("stddef");
    else
        fail("stddef");
    if (test_string())
        ok("string");
    else
        fail("string");
    if (test_memory())
        ok("memory");
    else
        fail("memory");
    if (test_stdio())
        ok("stdio");
    else
        fail("stdio");
    if (test_errno())
        ok("errno");
    else
        fail("errno");
    if (test_time())
        ok("time");
    else
        fail("time");
    if (test_files())
        ok("files");
    else
        fail("files");
    if (test_process())
        ok("process");
    else
        fail("process");
    if (test_threads())
        ok("threads");
    else
        fail("threads");
    if (test_sockets())
        ok("sockets");
    else
        fail("sockets");

    if (!g_fail) {
        printf("\nAll tests passed.\n");
        printf("[PASS] LIBC\n");
        return 0;
    }
    printf("[FAIL] LIBC\n");
    return 1;
}
