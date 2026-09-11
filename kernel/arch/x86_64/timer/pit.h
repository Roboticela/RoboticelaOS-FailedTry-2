#ifndef ROBO_ARCH_PIT_H
#define ROBO_ARCH_PIT_H

#include <stdint.h>

#define ROBO_PIT_HZ 1193182u
#define ROBO_PIC_IRQ_PIT 0
#define ROBO_IRQ_BASE 32u

void robo_pic_init(void);
void robo_pic_eoi(unsigned irq);
void robo_pic_mask(unsigned irq);
void robo_pic_unmask(unsigned irq);

void robo_pit_program(unsigned hz);
uint16_t robo_pit_latch0(void);
uint32_t robo_pit_divisor(void);
uint64_t robo_pit_calibrate_tsc(uint32_t pit_ticks);

#endif
