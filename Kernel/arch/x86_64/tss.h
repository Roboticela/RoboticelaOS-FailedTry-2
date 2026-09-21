#ifndef ROBO_ARCH_TSS_H
#define ROBO_ARCH_TSS_H

#include <stdint.h>

#define RoboTssIstDf 1u

void TssInit(void);
void *TssPtr(void);
uint32_t TssSize(void);
uint64_t TssRsp0(void);
void TssSetRsp0(uint64_t rsp0);
uint64_t TssIst1(void);

#endif
