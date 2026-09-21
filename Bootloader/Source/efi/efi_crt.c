#include "efi.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

EFI_SYSTEM_TABLE *gST;
EFI_BOOT_SERVICES *gBS;
EFI_RUNTIME_SERVICES *gRT;
EFI_HANDLE gImage;

int fltused = 1;

void __chkstk(void)
{
}

struct EfiFile { int dummy; };
static struct EfiFile gErr, gOut;
FILE *stderr = &gErr;
FILE *stdout = &gOut;

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static int gUart; /* 0 unknown, 1 present, -1 absent */

static int UartReady(void)
{
    uint8_t lsr;
    if (gUart < 0) return 0;
    lsr = inb(0x3F8 + 5);
    if (lsr == 0xFF) {
        gUart = -1;
        return 0;
    }
    return (lsr & 0x20) != 0;
}

static void UartInit(void)
{
    uint8_t lsr;
    lsr = inb(0x3F8 + 5);
    if (lsr == 0xFF) {
        gUart = -1;
        return;
    }
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}

static void UartPutc(char c)
{
    int spins = 32;
    if (gUart < 0) return;
    while (spins-- > 0 && !UartReady())
        ;
    if (!UartReady()) {
        gUart = -1;
        return;
    }
    gUart = 1;
    outb(0x3F8, (uint8_t)c);
}

void EfiLog(const char *msg)
{
    int i;
    if (!msg) return;
    for (i = 0; msg[i]; i++) {
        char c = msg[i];
        if (c == '\n') {
            UartPutc('\r');
            UartPutc('\n');
        } else {
            UartPutc(c);
        }
    }
    UartPutc('\r');
    UartPutc('\n');
}

void EfiInitLib(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    gImage = image;
    gST = st;
    gBS = st ? st->BootServices : 0;
    gRT = st ? st->RuntimeServices : 0;
    UartInit();
    if (gBS && gBS->SetWatchdogTimer)
        gBS->SetWatchdogTimer(0, 0, 0, 0);
}

void *malloc(size_t n)
{
    void *p = 0;
    if (!n) n = 1;
    if (!gBS || gBS->AllocatePool(EfiLoaderData, n, &p) != EFI_SUCCESS)
        return 0;
    return p;
}

void *calloc(size_t n, size_t m)
{
    size_t bytes;
    void *p;
    if (m && n > (size_t)-1 / m) return 0;
    bytes = n * m;
    p = malloc(bytes);
    if (p) memset(p, 0, bytes);
    return p;
}

void free(void *p)
{
    if (p && gBS) gBS->FreePool(p);
}

int atoi(const char *s)
{
    int sign = 1, n = 0;
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return sign * n;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) return dst;
    if (d < s) return memcpy(dst, src, n);
    d += n;
    s += n;
    while (n--) *--d = *--s;
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    while (n--) {
        if (*p != *q) return (int)*p - (int)*q;
        p++;
        q++;
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    if (!a) a = "";
    if (!b) b = "";
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    if (!a) a = "";
    if (!b) b = "";
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (n == 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *strchr(const char *s, int c)
{
    if (!s) return 0;
    while (*s) {
        if ((unsigned char)*s == (unsigned char)c) return (char *)s;
        s++;
    }
    if (c == 0) return (char *)s;
    return 0;
}

static void FmtNum(char **out, size_t *left, unsigned long long v, int base, int width)
{
    char tmp[32];
    const char *d = "0123456789abcdef";
    int n = 0, i;
    if (v == 0) tmp[n++] = '0';
    while (v && n < 32) {
        tmp[n++] = d[v % (unsigned)base];
        v /= (unsigned)base;
    }
    while (n < width && n < 32) tmp[n++] = '0';
    for (i = n - 1; i >= 0; i--) {
        if (*left > 1) {
            *(*out)++ = tmp[i];
            (*left)--;
        }
    }
}

int vsnprintf(char *s, size_t n, const char *fmt, va_list ap)
{
    char *out = s;
    size_t left = n;
    if (!s || n == 0) return 0;
    if (!fmt) { s[0] = 0; return 0; }
    while (*fmt && left > 1) {
        if (*fmt != '%') {
            *out++ = *fmt++;
            left--;
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            *out++ = '%';
            left--;
            fmt++;
            continue;
        }
        {
            int width = 0;
            int zero = 0;
            while (*fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '#' || *fmt == '0') {
                if (*fmt == '0') zero = 1;
                fmt++;
            }
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
            if (*fmt == '.') {
                int prec = 0;
                fmt++;
                while (*fmt >= '0' && *fmt <= '9') {
                    prec = prec * 10 + (*fmt - '0');
                    fmt++;
                }
                if (prec > width) width = prec;
                zero = 1;
            }
            if (!zero) width = 0;
            if (*fmt == 's') {
                const char *p = va_arg(ap, const char *);
                if (!p) p = "(null)";
                while (*p && left > 1) { *out++ = *p++; left--; }
                fmt++;
            } else if (*fmt == 'c') {
                char c = (char)va_arg(ap, int);
                *out++ = c;
                left--;
                fmt++;
            } else if (*fmt == 'd') {
                int v = va_arg(ap, int);
                if (v < 0 && left > 1) { *out++ = '-'; left--; v = -v; }
                FmtNum(&out, &left, (unsigned long long)(unsigned)v, 10, width);
                fmt++;
            } else if (*fmt == 'u') {
                FmtNum(&out, &left, va_arg(ap, unsigned), 10, width);
                fmt++;
            } else if (*fmt == 'x') {
                FmtNum(&out, &left, va_arg(ap, unsigned), 16, width);
                fmt++;
            } else if (fmt[0] == 'z' && fmt[1] == 'u') {
                FmtNum(&out, &left, (unsigned long long)va_arg(ap, size_t), 10, width);
                fmt += 2;
            } else if (fmt[0] == 'l' && fmt[1] == 'l' && fmt[2] == 'x') {
                FmtNum(&out, &left, va_arg(ap, unsigned long long), 16, width);
                fmt += 3;
            } else if (fmt[0] == 'l' && fmt[1] == 'l' && fmt[2] == 'u') {
                FmtNum(&out, &left, va_arg(ap, unsigned long long), 10, width);
                fmt += 3;
            } else {
                *out++ = '%';
                left--;
            }
        }
    }
    *out = 0;
    return (int)(out - s);
}

int snprintf(char *s, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    (void)f;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    EfiLog(buf);
    return 0;
}

float fabsf(float x) { return x < 0.f ? -x : x; }
float fminf(float a, float b) { return a < b ? a : b; }
float fmaxf(float a, float b) { return a > b ? a : b; }

float floorf(float x)
{
    int i;
    if (x >= 0.f) {
        i = (int)x;
        return (float)i;
    }
    i = (int)x;
    return (x == (float)i) ? (float)i : (float)(i - 1);
}

float ceilf(float x)
{
    int i;
    if (x <= 0.f) {
        i = (int)x;
        return (float)i;
    }
    i = (int)x;
    return (x == (float)i) ? (float)i : (float)(i + 1);
}

float sqrtf(float x)
{
    float g;
    int i;
    if (x <= 0.f) return 0.f;
    g = x < 1.f ? 1.f : x;
    for (i = 0; i < 12; i++)
        g = 0.5f * (g + x / g);
    return g;
}

static float LogfApprox(float x)
{
    /* ln(x) for x>0, simple range reduction */
    int exp = 0;
    float z, z2, r;
    if (x <= 0.f) return -1e9f;
    while (x >= 2.f) { x *= 0.5f; exp++; }
    while (x < 1.f) { x *= 2.f; exp--; }
    z = x - 1.f;
    z2 = z * z;
    r = z - z2 / 2.f + z2 * z / 3.f - z2 * z2 / 4.f;
    return r + 0.693147f * (float)exp;
}

static float ExpfApprox(float x)
{
    float s = 1.f, t = 1.f;
    int i;
    if (x > 20.f) return 1e9f;
    if (x < -20.f) return 0.f;
    for (i = 1; i < 10; i++) {
        t *= x / (float)i;
        s += t;
    }
    return s;
}

float powf(float x, float y)
{
    if (x <= 0.f) return 0.f;
    return ExpfApprox(y * LogfApprox(x));
}
