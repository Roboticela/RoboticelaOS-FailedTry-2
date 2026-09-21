#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <fcntl.h>
#include <internal/libc.h>

#ifndef LIBC_HOST_TEST
#include <robosys/syscall_numbers.h>
#endif

struct __robo_file {
    int fd;
    int error;
    int eof;
};

static FILE g_stdin = { 0, 0, 0 };
static FILE g_stdout = { 1, 0, 0 };
static FILE g_stderr = { 2, 0, 0 };

FILE *stdin = &g_stdin;
FILE *stdout = &g_stdout;
FILE *stderr = &g_stderr;

void robo_stdio_init(void)
{
    stdin = &g_stdin;
    stdout = &g_stdout;
    stderr = &g_stderr;
}

void robo_stdio_fini(void)
{
}

static void out_char(char **dst, size_t *left, size_t *count, char c)
{
    if (*dst && *left > 1) {
        **dst = c;
        (*dst)++;
        (*left)--;
    }
    (*count)++;
}

static void out_str(char **dst, size_t *left, size_t *count, const char *s)
{
    if (!s)
        s = "(null)";
    while (*s)
        out_char(dst, left, count, *s++);
}

static void out_uint(char **dst, size_t *left, size_t *count, uint64_t v,
                     int base, int upper, int width, int prec)
{
    char buf[32];
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    int i;
    int pad;

    if (v == 0 && prec == 0) {
        n = 0;
    } else {
        do {
            buf[n++] = dig[v % (unsigned)base];
            v /= (unsigned)base;
        } while (v);
    }
    while (n < prec && n < 32)
        buf[n++] = '0';
    pad = width - n;
    while (pad-- > 0)
        out_char(dst, left, count, ' ');
    for (i = n - 1; i >= 0; i--)
        out_char(dst, left, count, buf[i]);
}

static int fmt(char *dst, size_t cap, const char *fmt, va_list ap)
{
    size_t left = cap;
    size_t count = 0;
    const char *p;

    for (p = fmt; p && *p; p++) {
        int width = 0, prec = -1, longf = 0;
        if (*p != '%') {
            out_char(&dst, &left, &count, *p);
            continue;
        }
        p++;
        if (*p == '%') {
            out_char(&dst, &left, &count, '%');
            continue;
        }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }
        if (*p == '.') {
            prec = 0;
            p++;
            while (*p >= '0' && *p <= '9') {
                prec = prec * 10 + (*p - '0');
                p++;
            }
        }
        while (*p == 'l') {
            longf++;
            p++;
        }
        switch (*p) {
        case 'd':
        case 'i': {
            int64_t v = longf ? va_arg(ap, long long) : va_arg(ap, int);
            if (v < 0) {
                out_char(&dst, &left, &count, '-');
                out_uint(&dst, &left, &count, (uint64_t)(-v), 10, 0, width, prec);
            } else
                out_uint(&dst, &left, &count, (uint64_t)v, 10, 0, width, prec);
            break;
        }
        case 'u': {
            uint64_t v = longf ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned);
            out_uint(&dst, &left, &count, v, 10, 0, width, prec);
            break;
        }
        case 'x':
        case 'X': {
            uint64_t v = longf ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned);
            out_uint(&dst, &left, &count, v, 16, *p == 'X', width, prec);
            break;
        }
        case 'o': {
            uint64_t v = longf ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned);
            out_uint(&dst, &left, &count, v, 8, 0, width, prec);
            break;
        }
        case 'c':
            out_char(&dst, &left, &count, (char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (prec >= 0) {
                int i;
                if (!s)
                    s = "(null)";
                for (i = 0; i < prec && s[i]; i++)
                    out_char(&dst, &left, &count, s[i]);
            } else
                out_str(&dst, &left, &count, s);
            break;
        }
        case 'p': {
            uintptr_t v = (uintptr_t)va_arg(ap, void *);
            out_str(&dst, &left, &count, "0x");
            out_uint(&dst, &left, &count, (uint64_t)v, 16, 0, 0, 0);
            break;
        }
        default:
            out_char(&dst, &left, &count, '%');
            if (*p)
                out_char(&dst, &left, &count, *p);
            break;
        }
    }
    if (dst && cap)
        *dst = 0;
    else if (dst)
        (void)dst;
    return (int)count;
}

int vsnprintf(char *dst, size_t n, const char *format, va_list ap)
{
    return fmt(dst, n, format, ap);
}

int snprintf(char *dst, size_t n, const char *format, ...)
{
    va_list ap;
    int r;
    va_start(ap, format);
    r = vsnprintf(dst, n, format, ap);
    va_end(ap);
    return r;
}

int sprintf(char *dst, const char *format, ...)
{
    va_list ap;
    int r;
    va_start(ap, format);
    r = vsnprintf(dst, (size_t)-1, format, ap);
    va_end(ap);
    return r;
}

int vfprintf(FILE *fp, const char *format, va_list ap)
{
    char buf[512];
    int n;
    va_list copy;
    va_copy(copy, ap);
    n = vsnprintf(buf, sizeof(buf), format, copy);
    va_end(copy);
    if (n < 0)
        return n;
    if ((size_t)n >= sizeof(buf)) {
        /* truncated local buffer; still write what fits */
        n = (int)sizeof(buf) - 1;
    }
    if (!fp)
        fp = stdout;
#ifdef LIBC_HOST_TEST
    {
        extern int robo_host_write(const void *buf, int n);
        (void)fp;
        robo_host_write(buf, n);
        return n;
    }
#else
    {
        robo_long w = robo_syscall((robo_long)ROBO_SYS_WRITE, (robo_long)fp->fd,
                                   (robo_long)(uintptr_t)buf, (robo_long)n, 0, 0, 0);
        if (robo_set_err(w))
            return -1;
        return (int)w;
    }
#endif
}

int fprintf(FILE *fp, const char *format, ...)
{
    va_list ap;
    int r;
    va_start(ap, format);
    r = vfprintf(fp, format, ap);
    va_end(ap);
    return r;
}

int vprintf(const char *format, va_list ap)
{
    return vfprintf(stdout, format, ap);
}

int printf(const char *format, ...)
{
    va_list ap;
    int r;
    va_start(ap, format);
    r = vprintf(format, ap);
    va_end(ap);
    return r;
}

int fputc(int c, FILE *fp)
{
    char ch = (char)c;
#ifdef LIBC_HOST_TEST
    (void)fp;
    return (unsigned char)ch;
#else
    robo_long w;
    if (!fp)
        fp = stdout;
    w = robo_syscall((robo_long)ROBO_SYS_WRITE, (robo_long)fp->fd,
                     (robo_long)(uintptr_t)&ch, 1, 0, 0, 0);
    if (robo_set_err(w))
        return EOF;
    return (unsigned char)ch;
#endif
}

int putchar(int c)
{
    return fputc(c, stdout);
}

int fputs(const char *s, FILE *fp)
{
    size_t n;
    if (!s)
        return EOF;
    n = strlen(s);
#ifdef LIBC_HOST_TEST
    (void)fp;
    return (int)n;
#else
    {
        robo_long w;
        if (!fp)
            fp = stdout;
        w = robo_syscall((robo_long)ROBO_SYS_WRITE, (robo_long)fp->fd,
                         (robo_long)(uintptr_t)s, (robo_long)n, 0, 0, 0);
        if (robo_set_err(w))
            return EOF;
        return (int)w;
    }
#endif
}

int puts(const char *s)
{
    if (fputs(s, stdout) < 0)
        return EOF;
    return fputc('\n', stdout) == EOF ? EOF : 0;
}

FILE *fopen(const char *path, const char *mode)
{
    int flags = O_RDONLY;
    int fd;
    FILE *fp;
    if (!path || !mode)
        return 0;
    if (mode[0] == 'w')
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (mode[0] == 'a' || (mode[0] == 'r' && mode[1] == '+'))
        flags = O_RDWR | O_CREAT;
#ifdef LIBC_HOST_TEST
    (void)flags;
    return 0;
#else
    fd = (int)robo_syscall((robo_long)ROBO_SYS_OPEN, (robo_long)(uintptr_t)path,
                           (robo_long)flags, 0, 0, 0, 0);
    if (robo_set_err((robo_long)fd))
        return 0;
    fp = (FILE *)malloc(sizeof(*fp));
    if (!fp) {
        robo_syscall((robo_long)ROBO_SYS_CLOSE, (robo_long)fd, 0, 0, 0, 0, 0);
        return 0;
    }
    fp->fd = fd;
    fp->error = 0;
    fp->eof = 0;
    return fp;
#endif
}

int fclose(FILE *fp)
{
    int r;
    if (!fp || fp == stdin || fp == stdout || fp == stderr)
        return 0;
#ifdef LIBC_HOST_TEST
    (void)r;
    return 0;
#else
    r = (int)robo_syscall((robo_long)ROBO_SYS_CLOSE, (robo_long)fp->fd, 0, 0, 0, 0, 0);
    free(fp);
    if (robo_set_err((robo_long)r))
        return EOF;
    return 0;
#endif
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp)
{
    size_t n;
    if (!ptr || !fp || (size && nmemb > (size_t)-1 / size))
        return 0;
    n = size * nmemb;
#ifdef LIBC_HOST_TEST
    return nmemb;
#else
    {
        robo_long w = robo_syscall((robo_long)ROBO_SYS_WRITE, (robo_long)fp->fd,
                                   (robo_long)(uintptr_t)ptr, (robo_long)n, 0, 0, 0);
        if (robo_set_err(w) || (size_t)w != n)
            return 0;
        return nmemb;
    }
#endif
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp)
{
    size_t n;
    if (!ptr || !fp || (size && nmemb > (size_t)-1 / size))
        return 0;
    n = size * nmemb;
#ifdef LIBC_HOST_TEST
    (void)n;
    return 0;
#else
    {
        robo_long r = robo_syscall((robo_long)ROBO_SYS_READ, (robo_long)fp->fd,
                                   (robo_long)(uintptr_t)ptr, (robo_long)n, 0, 0, 0);
        if (robo_set_err(r))
            return 0;
        if ((size_t)r < n)
            fp->eof = 1;
        return size ? (size_t)r / size : 0;
    }
#endif
}

int fflush(FILE *fp)
{
    (void)fp;
    return 0;
}
