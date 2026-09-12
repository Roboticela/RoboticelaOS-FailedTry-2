#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <internal/libc.h>

#ifndef LIBC_HOST_TEST
#include <robosys/syscall_numbers.h>
#endif

int abs(int n)
{
    return n < 0 ? -n : n;
}

long labs(long n)
{
    return n < 0 ? -n : n;
}

int atoi(const char *s)
{
    return (int)strtol(s, 0, 10);
}

static int digit(int c, int base)
{
    int v;
    if (c >= '0' && c <= '9')
        v = c - '0';
    else if (c >= 'a' && c <= 'z')
        v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'Z')
        v = c - 'A' + 10;
    else
        return -1;
    if (v >= base)
        return -1;
    return v;
}

static unsigned long parse_ul(const char *s, char **end, int base, int *neg)
{
    const char *p = s;
    unsigned long acc = 0;
    int d, seen = 0;

    *neg = 0;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    if (*p == '+' || *p == '-') {
        *neg = (*p == '-');
        p++;
    }
    if (base == 0) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            base = 16;
            p += 2;
        } else if (p[0] == '0')
            base = 8;
        else
            base = 10;
    } else if (base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
        p += 2;
    if (base < 2 || base > 36) {
        errno = EINVAL;
        if (end)
            *end = (char *)s;
        return 0;
    }
    for (;;) {
        d = digit((unsigned char)*p, base);
        if (d < 0)
            break;
        if (acc > (ULONG_MAX - (unsigned long)d) / (unsigned long)base)
            errno = EOVERFLOW;
        acc = acc * (unsigned long)base + (unsigned long)d;
        p++;
        seen = 1;
    }
    if (!seen) {
        if (end)
            *end = (char *)s;
        return 0;
    }
    if (end)
        *end = (char *)p;
    return acc;
}

long strtol(const char *s, char **end, int base)
{
    int neg = 0;
    unsigned long v;
    if (!s)
        return 0;
    v = parse_ul(s, end, base, &neg);
    if (neg) {
        if (v > (unsigned long)LONG_MAX + 1ul)
            return LONG_MIN;
        return -(long)v;
    }
    if (v > (unsigned long)LONG_MAX)
        return LONG_MAX;
    return (long)v;
}

unsigned long strtoul(const char *s, char **end, int base)
{
    int neg = 0;
    unsigned long v;
    if (!s)
        return 0;
    v = parse_ul(s, end, base, &neg);
    return neg ? (unsigned long)-(long)v : v;
}

static void swap_bytes(unsigned char *a, unsigned char *b, size_t n)
{
    while (n--) {
        unsigned char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

void qsort(void *base, size_t count, size_t size,
           int (*cmp)(const void *, const void *))
{
    size_t i, j;
    unsigned char *p = (unsigned char *)base;
    if (!base || !cmp || size == 0 || count < 2)
        return;
    for (i = 0; i < count; i++) {
        for (j = 0; j + 1 < count - i; j++) {
            unsigned char *a = p + j * size;
            unsigned char *b = p + (j + 1) * size;
            if (cmp(a, b) > 0)
                swap_bytes(a, b, size);
        }
    }
}

void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*cmp)(const void *, const void *))
{
    const unsigned char *p = (const unsigned char *)base;
    size_t lo = 0, hi = count;
    if (!key || !base || !cmp || size == 0)
        return 0;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = cmp(key, p + mid * size);
        if (c == 0)
            return (void *)(p + mid * size);
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return 0;
}

#ifdef LIBC_HOST_TEST
void abort(void)
{
    exit(1);
}

void exit(int status)
{
    _Exit(status);
}

void _Exit(int status)
{
    (void)status;
    for (;;)
        ;
}

#else

void abort(void)
{
    _Exit(134);
}

void exit(int status)
{
    robo_stdio_fini();
    _Exit(status);
}

void _Exit(int status)
{
    robo_syscall((robo_long)ROBO_SYS_EXIT, (robo_long)status, 0, 0, 0, 0, 0);
    for (;;)
        ;
}
#endif
