#ifndef ROBO_MM_MEMORY_MAP_H
#define ROBO_MM_MEMORY_MAP_H

#include "mm/page.h"
#include "roboticela/bootinfo.h"

#define ROBO_MMAP_MAX 512

typedef enum {
    ROBO_MEMORY_USABLE = 0,
    ROBO_MEMORY_RESERVED,
    ROBO_MEMORY_ACPI_RECLAIMABLE,
    ROBO_MEMORY_ACPI_NVS,
    ROBO_MEMORY_BOOTLOADER,
    ROBO_MEMORY_KERNEL,
    ROBO_MEMORY_FRAMEBUFFER,
    ROBO_MEMORY_MMIO,
    ROBO_MEMORY_RUNTIME,
    ROBO_MEMORY_UNKNOWN
} robo_memory_type_t;

typedef struct {
    phys_addr_t base;
    uint64_t length;
    robo_memory_type_t type;
} robo_memory_region_t;

typedef struct {
    robo_memory_region_t regions[ROBO_MMAP_MAX];
    size_t count;
} robo_memory_map_t;

robo_memory_type_t robo_memory_type_from_boot(uint32_t boot_type);
const char *robo_memory_type_name(robo_memory_type_t type);

int robo_memory_map_load(robo_memory_map_t *out, const RoboBootInfo *info);
int robo_memory_map_usable_limit(const robo_memory_map_t *map, phys_addr_t *limit);
bool robo_memory_usable_pages(phys_addr_t base, uint64_t length,
                              phys_addr_t *page_lo, phys_addr_t *page_hi);

uint64_t robo_bootinfo_framebuffer_size(const RoboBootInfo *info);

#endif
