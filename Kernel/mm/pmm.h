#ifndef ROBO_MM_PMM_H
#define ROBO_MM_PMM_H

#include "mm/memory_map.h"

/*
 * Physical Memory Manager — bitmap of 4 KiB pages.
 *
 * 0 = free, 1 = occupied. Only complete pages inside boot-classified
 * usable (conventional) memory are ever marked free. ACPI reclaim,
 * NVS, loader, runtime, MMIO, framebuffer, kernel, bootinfo, and the
 * PMM bitmaps themselves stay permanently reserved.
 *
 * Bitmap placement: occupancy and sticky-reserved maps sit together in
 * the lowest usable span that does not overlap the kernel image,
 * RoboBootInfo block, initrd, or framebuffer. Access is through the
 * identity map left by RoboBoot after ExitBootServices.
 *
 * Allocation returns physical addresses, never virtual pointers.
 */

bool PmmInit(const RoboBootInfo *info);
int PmmReady(void);
void PmmReport(void);
#ifndef PMM_HOST_TEST
void PmmFbReport(void);
#else
void PmmHostBind(void *virt, PhysAddr phys, uint64_t len);
#endif
int PmmSelftest(void);

PhysAddr PageAlloc(void);
void PageFree(PhysAddr page);

size_t PageCount(void);
size_t FreePageCount(void);
size_t UsedPageCount(void);

bool PageIsFree(PhysAddr page);
bool PageIsReserved(PhysAddr page);

PhysAddr PmmBitmapAddress(void);
size_t PmmBitmapBytes(void);
PhysAddr PmmLimit(void);

#endif
