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
 *   0xFFFF800000000000  RoboVmmTestBase
 *       Reserved test window (not identity mapped).
 *   0xFFFF800000100000  RoboVmmHeapBase
 *       Kernel heap window (Phase 4). Pages are mapped on demand
 *       through kmalloc growth; the range itself is reserved here.
 *   0xFFFF800020000000
 *       Per-thread kernel stacks (Phase 8/9). Guard page + 16 KiB RW NX.
 *
 * Empty intermediate page tables are not freed on unmap.
 * Unmap does not return data pages to the PMM.
 */

bool VmmInit(const RoboBootInfo *info);
int VmmReady(void);
void VmmReport(void);
#ifndef VMM_HOST_TEST
void VmmFbReport(void);
#endif
int VmmSelftest(void);
int VmmNxEnabled(void);

PhysAddr VmmGetCr3(void);
PhysAddr VmmKernelCr3(void);
void VmmLoadAddressSpace(PhysAddr pml4);
void VmmInvalidatePage(VirtAddr address);

/*
 * Kernel mappings (identity RAM, heap, kernel stacks) live in PML4
 * slots 0 and 256 and are shared: clone copies those entries so a
 * process CR3 switch does not unmap the kernel. Slot 257
 * (RoboVmmProcBase) is private per cloned aspace.
 */
int VmmAspaceClone(RoboAddressSpace *as);
void VmmAspaceDestroy(RoboAddressSpace *as);
int VmmAspaceMap(RoboAddressSpace *as, VirtAddr va,
                        PhysAddr pa, uint64_t flags);
int VmmAspaceUnmap(RoboAddressSpace *as, VirtAddr va,
                          PhysAddr *pa);
int VmmAspaceTranslate(RoboAddressSpace *as, VirtAddr va,
                              PhysAddr *pa);
int VmmAspaceProtect(RoboAddressSpace *as, VirtAddr va, uint64_t flags);
int VmmAspaceInspect(RoboAddressSpace *as, VirtAddr va,
                            PhysAddr *pa, Pte *pte);

bool VmmMap(VirtAddr virtualAddress, PhysAddr physicalAddress, uint64_t flags);
bool VmmUnmap(VirtAddr virtualAddress, PhysAddr *physicalAddress);
bool VmmTranslate(VirtAddr virtualAddress, PhysAddr *physicalAddress);
bool VmmProtect(VirtAddr virtualAddress, uint64_t flags);
bool VmmInspect(VirtAddr virtualAddress, PhysAddr *physicalAddress, Pte *pte);

#endif
