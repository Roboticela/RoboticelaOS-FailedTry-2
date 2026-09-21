#ifndef ROBO_MM_MEMORY_MAP_H
#define ROBO_MM_MEMORY_MAP_H

#include "mm/page.h"
#include "roboticela/bootinfo.h"

#define RoboMmapMax 512

typedef enum {
    RoboMemoryUsable = 0,
    RoboMemoryReserved,
    RoboMemoryAcpiReclaimable,
    RoboMemoryAcpiNvs,
    RoboMemoryBootloader,
    RoboMemoryKernel,
    RoboMemoryFramebuffer,
    RoboMemoryMmio,
    RoboMemoryRuntime,
    RoboMemoryUnknown
} MemoryType;

typedef struct {
    PhysAddr base;
    uint64_t length;
    MemoryType type;
} MemoryRegion;

typedef struct {
    MemoryRegion regions[RoboMmapMax];
    size_t count;
} MemoryMap;

MemoryType MemoryTypeFromBoot(uint32_t bootType);
const char *MemoryTypeName(MemoryType type);

int MemoryMapLoad(MemoryMap *out, const RoboBootInfo *info);
int MemoryMapUsableLimit(const MemoryMap *map, PhysAddr *limit);
bool MemoryUsablePages(PhysAddr base, uint64_t length,
                              PhysAddr *pageLo, PhysAddr *pageHi);

uint64_t BootinfoFramebufferSize(const RoboBootInfo *info);

#endif
