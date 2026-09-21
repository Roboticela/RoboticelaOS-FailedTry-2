#ifndef ROBO_ARCH_IDT_H
#define ROBO_ARCH_IDT_H

#include <stdint.h>

#define RoboIdtEntries 256u
#define RoboIdtExceptions 32u

void IdtInit(void);
void IdtSet(unsigned vec, void (*handler)(void), uint8_t ist);
int IdtGatePresent(unsigned vec);
uint16_t IdtLimit(void);
uint64_t IdtBase(void);

#endif
