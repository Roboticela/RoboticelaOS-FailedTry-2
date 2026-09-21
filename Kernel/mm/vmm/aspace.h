#ifndef ROBO_MM_ASPACE_H
#define ROBO_MM_ASPACE_H

#include "mm/page.h"

/*
 * Address-space object owned by a process.
 *
 * cr3 is the physical PML4. Kernel threads share one aspace whose
 * tables are the VMM kernel PML4. Cloned aspaces copy that PML4 so
 * kernel identity/heap/stack mappings stay shared (PML4 slots 0 and
 * 256) while RoboVmmProcBase (slot 257) is private.
 */

typedef struct RoboAspaceMap {
    VirtAddr va;
    PhysAddr pa;
    struct RoboAspaceMap *next;
} RoboAspaceMap;

typedef struct RoboAddressSpace {
    PhysAddr cr3;
    int cloned;
    uint64_t tablePages;
    RoboAspaceMap *maps;
} RoboAddressSpace;

#endif
