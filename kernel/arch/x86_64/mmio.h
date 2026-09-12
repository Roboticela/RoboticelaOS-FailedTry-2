#ifndef ROBO_ARCH_MMIO_H
#define ROBO_ARCH_MMIO_H

#include "mm/page.h"

void *robo_mmio_map(phys_addr_t pa, uint64_t len);
uint8_t robo_mmio_r8(const volatile void *p);
uint16_t robo_mmio_r16(const volatile void *p);
uint32_t robo_mmio_r32(const volatile void *p);
uint64_t robo_mmio_r64(const volatile void *p);
void robo_mmio_w8(volatile void *p, uint8_t v);
void robo_mmio_w16(volatile void *p, uint16_t v);
void robo_mmio_w32(volatile void *p, uint32_t v);
void robo_mmio_w64(volatile void *p, uint64_t v);

#endif
