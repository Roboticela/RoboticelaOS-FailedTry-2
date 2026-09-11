#ifndef ROBO_ARCH_CPU_H
#define ROBO_ARCH_CPU_H

#include "arch/x86_64/gdt.h"
#include "arch/x86_64/idt.h"

int robo_cpu_init(void);
int robo_cpu_ready(void);
void robo_cpu_report(void);
void robo_cpu_fb_report(void);
int robo_cpu_selftest(void);

#endif
