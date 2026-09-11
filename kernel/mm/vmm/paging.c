#include "mm/vmm/paging.h"

uint16_t robo_page_offset(virt_addr_t va)
{
    return (uint16_t)(va & ROBO_PAGE_MASK);
}

uint16_t robo_pt_index(virt_addr_t va)
{
    return (uint16_t)((va >> 12) & ROBO_PTE_INDEX_MASK);
}

uint16_t robo_pd_index(virt_addr_t va)
{
    return (uint16_t)((va >> 21) & ROBO_PTE_INDEX_MASK);
}

uint16_t robo_pdpt_index(virt_addr_t va)
{
    return (uint16_t)((va >> 30) & ROBO_PTE_INDEX_MASK);
}

uint16_t robo_pml4_index(virt_addr_t va)
{
    return (uint16_t)((va >> 39) & ROBO_PTE_INDEX_MASK);
}

bool robo_virtual_address_is_canonical(virt_addr_t va)
{
    uint64_t hi = va >> 47;
    return hi == 0 || hi == 0x1FFFFull;
}

bool robo_virt_aligned(virt_addr_t va)
{
    return (va & ROBO_PAGE_MASK) == 0;
}

phys_addr_t robo_pte_phys(robo_pte_t e)
{
    return (phys_addr_t)(e & ROBO_PTE_ADDR);
}

robo_pte_t robo_pte_make(phys_addr_t phys, uint64_t hw_flags)
{
    return ((robo_pte_t)phys & ROBO_PTE_ADDR) | (hw_flags & ~ROBO_PTE_ADDR);
}

int robo_vm_flags_valid(uint64_t vm_flags)
{
    if (!(vm_flags & (ROBO_VM_READ | ROBO_VM_WRITE | ROBO_VM_EXECUTE)))
        return 0;
    if ((vm_flags & ROBO_VM_USER) && (vm_flags & ROBO_VM_KERNEL))
        return 0;
    return 1;
}

uint64_t robo_vm_to_pte_flags(uint64_t vm_flags, int nx_enabled)
{
    uint64_t hw = ROBO_PTE_P;
    if (vm_flags & ROBO_VM_WRITE)
        hw |= ROBO_PTE_RW;
    if (vm_flags & ROBO_VM_USER)
        hw |= ROBO_PTE_US;
    if (vm_flags & ROBO_VM_GLOBAL)
        hw |= ROBO_PTE_G;
    if (vm_flags & ROBO_VM_NOCACHE)
        hw |= ROBO_PTE_PCD;
    if (nx_enabled && !(vm_flags & ROBO_VM_EXECUTE))
        hw |= ROBO_PTE_NX;
    return hw;
}
