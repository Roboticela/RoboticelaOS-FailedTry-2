#ifndef ROBO_MM_VMM_PAGING_H
#define ROBO_MM_VMM_PAGING_H

#include "mm/page.h"

/*
 * x86_64 4-level paging (4 KiB pages).
 *
 *  63        48 47     39 38     30 29     21 20     12 11        0
 * +------------+---------+---------+---------+---------+----------+
 * |  canonical |  PML4   |  PDPT   |   PD    |   PT    |  offset  |
 * +------------+---------+---------+---------+---------+----------+
 *
 * PML4, PDPT, PD, and PT are the same 512-entry / 64-bit-slot table format.
 */

typedef uint64_t robo_pte_t;

#define ROBO_PT_ENTRIES     512u
#define ROBO_PTE_INDEX_MASK 0x1FFu

#define ROBO_PTE_P     (1ull << 0)
#define ROBO_PTE_RW    (1ull << 1)
#define ROBO_PTE_US    (1ull << 2)
#define ROBO_PTE_PWT   (1ull << 3)
#define ROBO_PTE_PCD   (1ull << 4)
#define ROBO_PTE_A     (1ull << 5)
#define ROBO_PTE_D     (1ull << 6)
#define ROBO_PTE_PS    (1ull << 7)
#define ROBO_PTE_G     (1ull << 8)
#define ROBO_PTE_NX    (1ull << 63)
#define ROBO_PTE_ADDR  0x000FFFFFFFFFF000ull

#define ROBO_VM_READ     (1u << 0)
#define ROBO_VM_WRITE    (1u << 1)
#define ROBO_VM_EXECUTE  (1u << 2)
#define ROBO_VM_USER     (1u << 3)
#define ROBO_VM_KERNEL   (1u << 4)
#define ROBO_VM_GLOBAL   (1u << 5)
#define ROBO_VM_NOCACHE  (1u << 6)

#define ROBO_VMM_TEST_BASE   0xFFFF800000000000ull
#define ROBO_VMM_HEAP_BASE   0xFFFF800000100000ull
#define ROBO_VMM_KSTACK_BASE 0xFFFF800020000000ull

uint16_t robo_page_offset(virt_addr_t va);
uint16_t robo_pt_index(virt_addr_t va);
uint16_t robo_pd_index(virt_addr_t va);
uint16_t robo_pdpt_index(virt_addr_t va);
uint16_t robo_pml4_index(virt_addr_t va);

bool robo_virtual_address_is_canonical(virt_addr_t va);
bool robo_virt_aligned(virt_addr_t va);

phys_addr_t robo_pte_phys(robo_pte_t e);
robo_pte_t robo_pte_make(phys_addr_t phys, uint64_t hw_flags);
uint64_t robo_vm_to_pte_flags(uint64_t vm_flags, int nx_enabled);
int robo_vm_flags_valid(uint64_t vm_flags);

#endif
