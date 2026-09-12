#ifndef ROBO_ARCH_PAGING_CPU_H
#define ROBO_ARCH_PAGING_CPU_H

#include "mm/page.h"

phys_addr_t robo_cpu_read_cr3(void);
void robo_cpu_write_cr3(phys_addr_t pml4);
void robo_cpu_invlpg(virt_addr_t va);

int robo_cpu_nx_supported(void);
int robo_cpu_nx_enabled(void);
void robo_cpu_enable_nx(void);
void robo_cpu_enable_wp(void);
void robo_cpu_write_fs_base(uint64_t value);

#endif
