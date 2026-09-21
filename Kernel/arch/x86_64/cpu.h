#ifndef ROBO_ARCH_CPU_H
#define ROBO_ARCH_CPU_H

#include "arch/x86_64/gdt.h"
#include "arch/x86_64/idt.h"

int CpuInit(void);
int CpuReady(void);
void CpuReport(void);
void CpuFbReport(void);
int CpuSelftest(void);

#endif
