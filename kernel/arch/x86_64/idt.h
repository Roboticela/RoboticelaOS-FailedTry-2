#ifndef ROBO_ARCH_IDT_H
#define ROBO_ARCH_IDT_H

#include <stdint.h>

#define ROBO_IDT_ENTRIES 256u
#define ROBO_IDT_EXCEPTIONS 32u

void robo_idt_init(void);
void robo_idt_set(unsigned vec, void (*handler)(void), uint8_t ist);
int robo_idt_gate_present(unsigned vec);
uint16_t robo_idt_limit(void);
uint64_t robo_idt_base(void);

#endif
