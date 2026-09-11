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

static int g_uart; /* 0 unknown, 1 present, -1 absent */

static int uart_ready(void)
{
    uint8_t lsr;
    if (g_uart < 0) return 0;
    lsr = inb(0x3F8 + 5);
    if (lsr == 0xFF) {
        g_uart = -1;
        return 0;
    }
    return (lsr & 0x20) != 0;
}

void kserial_init(void)
{
    uint8_t lsr = inb(0x3F8 + 5);
    if (lsr == 0xFF) {
        g_uart = -1;
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

void kserial_putc(char c)
{
    int spins = 32;
    if (g_uart < 0) return;
    if (c == '\n') kserial_putc('\r');
    while (spins-- > 0 && !uart_ready())
        ;
    if (!uart_ready()) {
        g_uart = -1;
        return;
    }
    g_uart = 1;
    outb(0x3F8, (uint8_t)c);
}

void kserial_puts(const char *s)
{
    if (!s) return;
    while (*s) kserial_putc(*s++);
}

static void kprint_char(char c)
{
    kserial_putc(c);
    kfb_log_putc(c, 0xA1A1A6u);
}

static void kprint_str(const char *s)
{
    if (!s) return;
    while (*s) kprint_char(*s++);
}

static void fmt_num(unsigned long long v, int base, int width)
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
    for (i = n - 1; i >= 0; i--) kprint_char(tmp[i]);
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    if (!fmt) return;
    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') {
            kprint_char(*fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            kprint_char('%');
            fmt++;
        } else if (*fmt == 's') {
            kprint_str(va_arg(ap, const char *));
            fmt++;
        } else if (*fmt == 'd') {
            int v = va_arg(ap, int);
            if (v < 0) { kprint_char('-'); v = -v; }
            fmt_num((unsigned)v, 10, 0);
            fmt++;
        } else if (*fmt == 'u') {
            fmt_num(va_arg(ap, unsigned), 10, 0);
            fmt++;
        } else if (*fmt == 'x') {
            fmt_num(va_arg(ap, unsigned), 16, 0);
            fmt++;
        } else if (fmt[0] == 'l' && fmt[1] == 'l' && fmt[2] == 'x') {
            fmt_num(va_arg(ap, unsigned long long), 16, 0);
            fmt += 3;
        } else if (fmt[0] == 'l' && fmt[1] == 'l' && fmt[2] == 'u') {
            fmt_num(va_arg(ap, unsigned long long), 10, 0);
            fmt += 3;
        } else {
            kprint_char('%');
        }
    }
    va_end(ap);
}
