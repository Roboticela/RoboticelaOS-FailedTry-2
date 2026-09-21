#ifndef ROBO_ARCH_MMIO_H
#define ROBO_ARCH_MMIO_H

#include "mm/page.h"

void *MmioMap(PhysAddr pa, uint64_t len);
uint8_t MmioR8(const volatile void *p);
uint16_t MmioR16(const volatile void *p);
uint32_t MmioR32(const volatile void *p);
uint64_t MmioR64(const volatile void *p);
void MmioW8(volatile void *p, uint8_t v);
void MmioW16(volatile void *p, uint16_t v);
void MmioW32(volatile void *p, uint32_t v);
void MmioW64(volatile void *p, uint64_t v);

#endif
