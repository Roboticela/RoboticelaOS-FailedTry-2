#include "mm/vmm/table.h"
#include "mm/pmm.h"

robo_pte_t *robo_table_ptr(phys_addr_t phys)
{
    if (phys == ROBO_INVALID_PHYS_ADDR || !robo_page_aligned(phys))
        return 0;
    /* Page-table pages live in identity-mapped RAM for this phase. */
    return (robo_pte_t *)(uintptr_t)phys;
}

void robo_table_zero(phys_addr_t phys)
{
    robo_pte_t *t = robo_table_ptr(phys);
    unsigned i;
    if (!t)
        return;
    for (i = 0; i < ROBO_PT_ENTRIES; i++)
        t[i] = 0;
}

phys_addr_t robo_table_alloc(void)
{
    phys_addr_t p = robo_page_alloc();
    if (p == ROBO_INVALID_PHYS_ADDR)
        return p;
    robo_table_zero(p);
    return p;
}

robo_pte_t robo_table_get(const robo_pte_t *table, unsigned index)
{
    if (!table || index >= ROBO_PT_ENTRIES)
        return 0;
    return table[index];
}

void robo_table_set(robo_pte_t *table, unsigned index, robo_pte_t entry)
{
    if (!table || index >= ROBO_PT_ENTRIES)
        return;
    table[index] = entry;
}

int robo_pte_present(robo_pte_t e)
{
    return (e & ROBO_PTE_P) != 0;
}

int robo_pte_huge(robo_pte_t e)
{
    return (e & ROBO_PTE_PS) != 0;
}
