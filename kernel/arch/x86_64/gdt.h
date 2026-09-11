#ifndef ROBO_ARCH_GDT_H
#define ROBO_ARCH_GDT_H

#include <stdint.h>

#define ROBO_GDT_NULL         0x00u
#define ROBO_GDT_KERNEL_CODE  0x08u
#define ROBO_GDT_KERNEL_DATA  0x10u
#define ROBO_GDT_USER_DATA    0x18u
#define ROBO_GDT_USER_CODE    0x20u
#define ROBO_GDT_TSS          0x28u
#define ROBO_GDT_USER_CODE_RPL (ROBO_GDT_USER_CODE | 3u)
#define ROBO_GDT_USER_DATA_RPL (ROBO_GDT_USER_DATA | 3u)

void robo_gdt_init(void *tss, uint32_t tss_size);
uint16_t robo_cpu_read_cs(void);
uint16_t robo_cpu_read_ds(void);
uint16_t robo_cpu_read_ss(void);
uint16_t robo_cpu_read_tr(void);

#endif
