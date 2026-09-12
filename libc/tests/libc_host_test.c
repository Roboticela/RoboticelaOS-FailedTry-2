#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>

#ifdef _WIN32
void *__stdcall GetStdHandle(unsigned long n);
int __stdcall WriteFile(void *h, const void *buf, unsigned long n, unsigned long *w, void *ov);
void __stdcall ExitProcess(unsigned int c);

int robo_host_write(const void *buf, int n)
{
    unsigned long w = 0;
    WriteFile(GetStdHandle(4294967285u), buf, (unsigned long)n, &w, 0);
    return (int)w;
}
#else
int write(int fd, const void *buf, unsigned n);
int robo_host_write(const void *buf, int n)
{
    return (int)write(1, buf, (unsigned)n);
}
#endif

static int g_fail;

static void fail(const char *n)
{
    printf("[FAIL] %s\n", n);
    g_fail = 1;
}

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a;
    int y = *(const int *)b;
    return x - y;
}

static int test_string(void)
{
    char a[32];
    char b[32];
    char ov[16];

    memset(a, 0, sizeof(a));
    memcpy(a, "hello", 6);
    if (strcmp(a, "hello") != 0 || strlen(a) != 5)
        return 0;
    if (strncmp(a, "hellp", 4) != 0)
        return 0;
    strcpy(b, a);
    if (strcmp(a, b) != 0)
        return 0;
    strncpy(b, "xy", 8);
    if (b[2] != 0 || b[7] != 0)
        return 0;
    if (!strchr(a, 'e') || !strrchr(a, 'l') || !strstr(a, "ll"))
        return 0;
    strcat(b, "z");
    memset(ov, 'A', 12);
    ov[12] = 0;
    memmove(ov + 2, ov, 6);
    if (ov[2] != 'A')
        return 0;
    memcpy(ov, "AAAABBBBCCCC", 12);
    memmove(ov + 4, ov, 8);
    if (memcmp(ov, "AAAAAAAABBBB", 12) != 0)
        return 0;
    memcpy(ov, "AAAABBBBCCCC", 12);
    memmove(ov, ov + 4, 8);
    if (memcmp(ov, "BBBBCCCC", 8) != 0)
        return 0;
    if (strlen("") != 0 || memcmp("a", "a", 0) != 0)
        return 0;
    return 1;
}

static int test_stdio(void)
{
    char buf[8];
    int n = snprintf(buf, sizeof(buf), "Hello World");
    if (n != 11)
        return 0;
    if (buf[7] != 0 || strncmp(buf, "Hello W", 7) != 0)
        return 0;
    n = snprintf(buf, 0, "abc");
    if (n != 3)
        return 0;
    n = snprintf(buf, sizeof(buf), "%d %u %x %s %c %%", -7, 7u, 0x2a, "hi", 'Z');
    if (n < 0 || buf[7] != 0)
        return 0;
    n = snprintf(buf, sizeof(buf), "%p", (void *)0);
    if (n < 2 || buf[0] != '0')
        return 0;
    return 1;
}

static int test_mem(void)
{
    char *p;
    char *q;
    int i;
    void *z;

    z = malloc(0);
    if (!z)
        return 0;
    free(z);
    free(0);
    p = (char *)malloc(32);
    if (!p)
        return 0;
    if (((uintptr_t)p & 15u) != 0)
        return 0;
    strcpy(p, "hello");
    q = (char *)realloc(p, 128);
    if (!q || strcmp(q, "hello") != 0)
        return 0;
    p = (char *)realloc(0, 16);
    if (!p)
        return 0;
    free(p);
    p = (char *)calloc(4, 8);
    if (!p)
        return 0;
    for (i = 0; i < 32; i++) {
        if (p[i] != 0) {
            free(p);
            free(q);
            return 0;
        }
    }
    free(p);
    q = (char *)realloc(q, 0);
    if (q)
        return 0;
    if (calloc((size_t)-1, (size_t)-1))
        return 0;
    return 1;
}

static int test_stdlib(void)
{
    int v[5] = { 5, 1, 4, 2, 3 };
    int key = 4;
    int *f;
    qsort(v, 5, sizeof(int), cmp_int);
    if (v[0] != 1 || v[4] != 5)
        return 0;
    f = (int *)bsearch(&key, v, 5, sizeof(int), cmp_int);
    if (!f || *f != 4)
        return 0;
    if (atoi("  -42") != -42)
        return 0;
    if (strtol("0x10", 0, 0) != 16)
        return 0;
    if (abs(-3) != 3 || labs(-4L) != 4L)
        return 0;
    return 1;
}

int main(void)
{
    if (sizeof(uint64_t) != 8 || sizeof(size_t) != 8)
        fail("stdint");
    else
        printf("[ OK ] stdint\n");
    if (NULL != 0)
        fail("stddef");
    else
        printf("[ OK ] stddef\n");
    if (!test_string())
        fail("string");
    else
        printf("[ OK ] string\n");
    if (!test_mem())
        fail("memory");
    else
        printf("[ OK ] memory\n");
    if (!test_stdio())
        fail("stdio");
    else
        printf("[ OK ] stdio\n");
    if (!test_stdlib())
        fail("stdlib");
    else
        printf("[ OK ] stdlib\n");
    errno = EINVAL;
    if (errno != EINVAL)
        fail("errno");
    else
        printf("[ OK ] errno\n");
#ifdef _WIN32
    ExitProcess(g_fail ? 1u : 0u);
#endif
    return g_fail ? 1 : 0;
}
