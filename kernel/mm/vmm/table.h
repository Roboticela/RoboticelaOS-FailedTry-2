#ifndef ROBO_MM_VMM_TABLE_H
#define ROBO_MM_VMM_TABLE_H

#include "mm/vmm/paging.h"

robo_pte_t *robo_table_ptr(phys_addr_t phys);
phys_addr_t robo_table_alloc(void);
void robo_table_zero(phys_addr_t phys);

robo_pte_t robo_table_get(const robo_pte_t *table, unsigned index);
void robo_table_set(robo_pte_t *table, unsigned index, robo_pte_t entry);

int robo_pte_present(robo_pte_t e);
int robo_pte_huge(robo_pte_t e);

#endif
