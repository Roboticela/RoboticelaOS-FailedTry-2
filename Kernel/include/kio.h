#ifndef KIO_H
#define KIO_H

#include <stdint.h>
#include <stddef.h>
#include "roboticela/bootinfo.h"

void KserialInit(void);
void KserialPutc(char c);
void KserialPuts(const char *s);
void kwrite(const char *s, unsigned n);
void kprintf(const char *fmt, ...);
void kinfo(const char *fmt, ...);
void KlogSetTalk(int on);
int KlogTalk(void);

void KfbInit(const RoboBootInfo *info);
void KfbClear(uint32_t rgb);
void KfbFill(int x, int y, int w, int h, uint32_t rgb);
void KfbRoundRect(int x, int y, int w, int h, int r, uint32_t rgb);
void KfbTermOpen(int x, int y, int w, int h);
int KfbTermActive(void);
void KfbPuts(const char *s, uint32_t rgb);
void KfbPutu(unsigned long long v, uint32_t rgb);
void KfbPuthex(unsigned long long v, uint32_t rgb);
void KfbNewline(void);
void KfbLogPutc(char c, uint32_t rgb);
void KfbPoll(void);

void KfbSplashSet(unsigned progress);

int KbootDebug(const RoboBootInfo *info);
int KbootCmdHas(const RoboBootInfo *info, const char *key);
int KbootSelftest(const RoboBootInfo *info);
void KbootReport(const RoboBootInfo *info);

#endif
