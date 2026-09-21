#ifndef ROBO_ARCH_GDT_H
#define ROBO_ARCH_GDT_H

#include <stdint.h>

#define RoboGdtNull         0x00u
#define RoboGdtKernelCode  0x08u
#define RoboGdtKernelData  0x10u
#define RoboGdtUserData    0x18u
#define RoboGdtUserCode    0x20u
#define RoboGdtTss          0x28u
#define RoboGdtUserCodeRpl (RoboGdtUserCode | 3u)
#define RoboGdtUserDataRpl (RoboGdtUserData | 3u)

void GdtInit(void *tss, uint32_t tssSize);
uint16_t CpuReadCs(void);
uint16_t CpuReadDs(void);
uint16_t CpuReadSs(void);
uint16_t CpuReadTr(void);

#endif
