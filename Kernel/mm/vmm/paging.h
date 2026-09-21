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

typedef uint64_t Pte;

#define RoboPtEntries     512u
#define RoboPteIndexMask 0x1FFu

#define RoboPteP     (1ull << 0)
#define RoboPteRw    (1ull << 1)
#define RoboPteUs    (1ull << 2)
#define RoboPtePwt   (1ull << 3)
#define RoboPtePcd   (1ull << 4)
#define RoboPteA     (1ull << 5)
#define RoboPteD     (1ull << 6)
#define RoboPtePs    (1ull << 7)
#define RoboPteG     (1ull << 8)
#define RoboPteNx    (1ull << 63)
#define RoboPteAddr  0x000FFFFFFFFFF000ull

#define RoboVmRead     (1u << 0)
#define RoboVmWrite    (1u << 1)
#define RoboVmExecute  (1u << 2)
#define RoboVmUser     (1u << 3)
#define RoboVmKernel   (1u << 4)
#define RoboVmGlobal   (1u << 5)
#define RoboVmNocache  (1u << 6)

#define RoboVmmTestBase   0xFFFF800000000000ull
#define RoboVmmHeapBase   0xFFFF800000100000ull
#define RoboVmmKstackBase 0xFFFF800020000000ull
/* Unused PML4 slot 257 — per-process private mappings. Kernel PML4
 * entries 0 and 256 stay shared so identity maps, heap, and stacks
 * remain visible after a CR3 switch. */
#define RoboVmmProcBase   0xFFFF808000000000ull
/* Private PML4 slot 257 is the user window (512 GiB). Low identity and
 * kernel heap/stacks stay shared and are not valid user VAs. */
#define RoboUserMin        RoboVmmProcBase
#define RoboUserMax        (RoboVmmProcBase + 0x8000000000ull)
#define RoboUserTextBase  (RoboVmmProcBase + 0x400000ull)
#define RoboUserStackTop  (RoboVmmProcBase + 0x80000000ull)
#define RoboUserStackSize (64u * 1024u)
#define RoboUserMmapBase  (RoboVmmProcBase + 0x01000000ull)

uint16_t PageOffset(VirtAddr va);
uint16_t PtIndex(VirtAddr va);
uint16_t PdIndex(VirtAddr va);
uint16_t PdptIndex(VirtAddr va);
uint16_t Pml4Index(VirtAddr va);

bool VirtualAddressIsCanonical(VirtAddr va);
bool VirtAligned(VirtAddr va);

PhysAddr PtePhys(Pte e);
Pte PteMake(PhysAddr phys, uint64_t hwFlags);
uint64_t VmToPteFlags(uint64_t vmFlags, int nxEnabled);
int VmFlagsValid(uint64_t vmFlags);

#endif
