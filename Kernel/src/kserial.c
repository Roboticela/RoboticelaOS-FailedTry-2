#include "kio.h"

#include <stdarg.h>

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
static int gTalk;

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

void KserialInit(void)
{
    uint8_t lsr = inb(0x3F8 + 5);
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

void KserialPutc(char c)
{
    int spins = 32;
    if (gUart < 0) return;
    if (c == '\n') KserialPutc('\r');
    while (spins-- > 0 && !UartReady())
        ;
    if (!UartReady()) {
        gUart = -1;
        return;
    }
    gUart = 1;
    outb(0x3F8, (uint8_t)c);
}

void KserialPuts(const char *s)
{
    if (!s) return;
    while (*s) KserialPutc(*s++);
}

static void KprintChar(char c)
{
    KserialPutc(c);
    KfbLogPutc(c, 0xA1A1A6u);
}

void kwrite(const char *s, unsigned n)
{
    unsigned i;
    if (!s)
        return;
    for (i = 0; i < n; i++)
        KprintChar(s[i]);
}

static void KprintStr(const char *s)
{
    if (!s) return;
    while (*s) KprintChar(*s++);
}

static void FmtNum(unsigned long long v, int base, int width)
{
    char tmp[32];
    const char *d = "0123456789abcdef";
    int n = 0, i;
    if (v == 0) tmp[n++] = '0';
    while (v && n < (int)sizeof(tmp)) {
        tmp[n++] = d[v % (unsigned)base];
        v /= (unsigned)base;
    }
    while (n < width && n < (int)sizeof(tmp)) tmp[n++] = '0';
    for (i = n - 1; i >= 0; i--) KprintChar(tmp[i]);
}

void KlogSetTalk(int on)
{
    gTalk = on ? 1 : 0;
}

int KlogTalk(void)
{
    return gTalk;
}

static void Kvprintf(const char *fmt, va_list ap)
{
    while (*fmt) {
        if (*fmt != '%') {
            KprintChar(*fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            KprintChar('%');
            fmt++;
        } else if (*fmt == 's') {
            KprintStr(va_arg(ap, const char *));
            fmt++;
        } else if (*fmt == 'd') {
            int v = va_arg(ap, int);
            if (v < 0) { KprintChar('-'); v = -v; }
            FmtNum((unsigned)v, 10, 0);
            fmt++;
        } else if (*fmt == 'u') {
            FmtNum(va_arg(ap, unsigned), 10, 0);
            fmt++;
        } else if (*fmt == 'x') {
            FmtNum(va_arg(ap, unsigned), 16, 0);
            fmt++;
        } else if (fmt[0] == 'l' && fmt[1] == 'l' && fmt[2] == 'x') {
            FmtNum(va_arg(ap, unsigned long long), 16, 0);
            fmt += 3;
        } else if (fmt[0] == 'l' && fmt[1] == 'l' && fmt[2] == 'u') {
            FmtNum(va_arg(ap, unsigned long long), 10, 0);
            fmt += 3;
        } else {
            KprintChar('%');
        }
    }
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    if (!fmt)
        return;
    va_start(ap, fmt);
    Kvprintf(fmt, ap);
    va_end(ap);
}

void kinfo(const char *fmt, ...)
{
    va_list ap;
    if (!gTalk || !fmt)
        return;
    va_start(ap, fmt);
    Kvprintf(fmt, ap);
    va_end(ap);
}
