#ifndef ROBO_MM_VMM_H
#define ROBO_MM_VMM_H

#include "mm/vmm/paging.h"
#include "mm/vmm/aspace.h"
#include "roboticela/bootinfo.h"

/*
 * Virtual Memory Manager — x86_64 4-level paging on top of the PMM.
 *
 * Layout (Phase 3):
 *   0x0000000000000000 .. pmm_limit-1
 *       Identity map of managed RAM. Boot-transition mapping so the
 *       kernel, stack, PMM bitmap, and bootinfo stay reachable. This is
 *       not the final higher-half kernel VAS.
 *   framebuffer physical range
 *       Identity-mapped MMIO, kernel RW, NX.
 *   0xFFFF800000000000  ROBO_VMM_TEST_BASE
 *       Reserved test window (not identity mapped).
 *   0xFFFF800000100000  ROBO_VMM_HEAP_BASE
 *       Kernel heap window (Phase 4). Pages are mapped on demand
 *       through kmalloc growth; the range itself is reserved here.
 *   0xFFFF800020000000
 *       Per-thread kernel stacks (Phase 8/9). Guard page + 16 KiB RW NX.
 *
 * Empty intermediate page tables are not freed on unmap.
 * Unmap does not return data pages to the PMM.
 */

bool robo_vmm_init(const RoboBootInfo *info);
int robo_vmm_ready(void);
void robo_vmm_report(void);
#ifndef VMM_HOST_TEST
void robo_vmm_fb_report(void);
#endif
int robo_vmm_selftest(void);
int robo_vmm_nx_enabled(void);

phys_addr_t robo_vmm_get_cr3(void);
phys_addr_t robo_vmm_kernel_cr3(void);
void robo_vmm_load_address_space(phys_addr_t pml4);
void robo_vmm_invalidate_page(virt_addr_t address);

/*
 * Kernel mappings (identity RAM, heap, kernel stacks) live in PML4
 * slots 0 and 256 and are shared: clone copies those entries so a
 * process CR3 switch does not unmap the kernel. Slot 257
 * (ROBO_VMM_PROC_BASE) is private per cloned aspace.
 */
int robo_vmm_aspace_clone(RoboAddressSpace *as);
void robo_vmm_aspace_destroy(RoboAddressSpace *as);
int robo_vmm_aspace_map(RoboAddressSpace *as, virt_addr_t va,
                        phys_addr_t pa, uint64_t flags);
int robo_vmm_aspace_unmap(RoboAddressSpace *as, virt_addr_t va,
                          phys_addr_t *pa);
int robo_vmm_aspace_translate(RoboAddressSpace *as, virt_addr_t va,
                              phys_addr_t *pa);
int robo_vmm_aspace_protect(RoboAddressSpace *as, virt_addr_t va, uint64_t flags);
int robo_vmm_aspace_inspect(RoboAddressSpace *as, virt_addr_t va,
                            phys_addr_t *pa, robo_pte_t *pte);

bool robo_vmm_map(virt_addr_t virtual_address, phys_addr_t physical_address, uint64_t flags);
bool robo_vmm_unmap(virt_addr_t virtual_address, phys_addr_t *physical_address);
bool robo_vmm_translate(virt_addr_t virtual_address, phys_addr_t *physical_address);
bool robo_vmm_protect(virt_addr_t virtual_address, uint64_t flags);
bool robo_vmm_inspect(virt_addr_t virtual_address, phys_addr_t *physical_address, robo_pte_t *pte);

#endif
