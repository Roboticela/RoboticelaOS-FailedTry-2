#ifndef ROBO_ARCH_TSS_H
#define ROBO_ARCH_TSS_H

#include <stdint.h>

#define ROBO_TSS_IST_DF 1u

void robo_tss_init(void);
void *robo_tss_ptr(void);
uint32_t robo_tss_size(void);
uint64_t robo_tss_rsp0(void);
uint64_t robo_tss_ist1(void);

#endif
