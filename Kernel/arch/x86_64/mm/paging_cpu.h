#ifndef ROBO_ARCH_PAGING_CPU_H
#define ROBO_ARCH_PAGING_CPU_H

#include "mm/page.h"

PhysAddr CpuReadCr3(void);
void CpuWriteCr3(PhysAddr pml4);
void CpuInvlpg(VirtAddr va);

int CpuNxSupported(void);
int CpuNxEnabled(void);
void CpuEnableNx(void);
void CpuEnableWp(void);
void CpuWriteFsBase(uint64_t value);

#endif
