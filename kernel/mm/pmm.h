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

bool robo_pmm_init(const RoboBootInfo *info);
int robo_pmm_ready(void);
void robo_pmm_report(void);
#ifndef PMM_HOST_TEST
void robo_pmm_fb_report(void);
#else
void robo_pmm_host_bind(void *virt, phys_addr_t phys, uint64_t len);
#endif
int robo_pmm_selftest(void);

phys_addr_t robo_page_alloc(void);
void robo_page_free(phys_addr_t page);

size_t robo_page_count(void);
size_t robo_free_page_count(void);
size_t robo_used_page_count(void);

bool robo_page_is_free(phys_addr_t page);
bool robo_page_is_reserved(phys_addr_t page);

phys_addr_t robo_pmm_bitmap_address(void);
size_t robo_pmm_bitmap_bytes(void);
phys_addr_t robo_pmm_limit(void);

#endif
