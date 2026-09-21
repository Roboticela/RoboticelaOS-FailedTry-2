#ifndef ROBO_ARCH_PIT_H
#define ROBO_ARCH_PIT_H

#include <stdint.h>

#define RoboPitHz 1193182u
#define RoboPicIrqPit 0
#define RoboIrqBase 32u

void PicInit(void);
void PicEoi(unsigned irq);
void PicMask(unsigned irq);
void PicUnmask(unsigned irq);

void PitProgram(unsigned hz);
uint16_t PitLatch0(void);
uint32_t PitDivisor(void);
uint64_t PitCalibrateTsc(uint32_t pitTicks);

#endif
