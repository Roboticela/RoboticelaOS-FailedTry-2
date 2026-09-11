#ifndef ROBO_MM_ASPACE_H
#define ROBO_MM_ASPACE_H

#include "mm/page.h"

/*
 * Address-space object owned by a process.
 *
 * cr3 is the physical PML4. Kernel threads share one aspace whose
 * tables are the VMM kernel PML4. Cloned aspaces copy that PML4 so
 * kernel identity/heap/stack mappings stay shared (PML4 slots 0 and
 * 256) while ROBO_VMM_PROC_BASE (slot 257) is private.
 */

typedef struct RoboAspaceMap {
    virt_addr_t va;
    phys_addr_t pa;
    struct RoboAspaceMap *next;
} RoboAspaceMap;

typedef struct RoboAddressSpace {
    phys_addr_t cr3;
    int cloned;
    uint64_t table_pages;
    RoboAspaceMap *maps;
} RoboAddressSpace;

#endif
