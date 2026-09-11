#ifndef KIO_H
#define KIO_H

#include <stdint.h>
#include <stddef.h>
#include "roboticela/bootinfo.h"

void kserial_init(void);
void kserial_putc(char c);
void kserial_puts(const char *s);
void kwrite(const char *s, unsigned n);
void kprintf(const char *fmt, ...);

void kfb_init(const RoboBootInfo *info);
void kfb_clear(uint32_t rgb);
void kfb_fill(int x, int y, int w, int h, uint32_t rgb);
void kfb_round_rect(int x, int y, int w, int h, int r, uint32_t rgb);
void kfb_term_open(int x, int y, int w, int h);
int kfb_term_active(void);
void kfb_puts(const char *s, uint32_t rgb);
void kfb_putu(unsigned long long v, uint32_t rgb);
void kfb_puthex(unsigned long long v, uint32_t rgb);
void kfb_newline(void);
void kfb_log_putc(char c, uint32_t rgb);
void kfb_poll(void);

void kfb_splash_set(unsigned progress);

int kboot_debug(const RoboBootInfo *info);
void kboot_report(const RoboBootInfo *info);

#endif
