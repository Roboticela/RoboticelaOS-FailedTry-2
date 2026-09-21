#include "mm/memory_map.h"

MemoryType MemoryTypeFromBoot(uint32_t bootType)
{
    switch (bootType) {
    case RoboMemAvailable:
        return RoboMemoryUsable;
    case RoboMemReserved:
        return RoboMemoryReserved;
    case RoboMemAcpi:
        return RoboMemoryAcpiReclaimable;
    case RoboMemNvs:
        return RoboMemoryAcpiNvs;
    case RoboMemMmio:
        return RoboMemoryMmio;
    case RoboMemLoader:
        return RoboMemoryBootloader;
    default:
        return RoboMemoryUnknown;
    }
}

const char *MemoryTypeName(MemoryType type)
{
    switch (type) {
    case RoboMemoryUsable: return "usable";
    case RoboMemoryReserved: return "reserved";
    case RoboMemoryAcpiReclaimable: return "acpi-reclaim";
    case RoboMemoryAcpiNvs: return "acpi-nvs";
    case RoboMemoryBootloader: return "bootloader";
    case RoboMemoryKernel: return "kernel";
    case RoboMemoryFramebuffer: return "framebuffer";
    case RoboMemoryMmio: return "mmio";
    case RoboMemoryRuntime: return "runtime";
    default: return "unknown";
    }
}

bool MemoryUsablePages(PhysAddr base, uint64_t length,
                              PhysAddr *pageLo, PhysAddr *pageHi)
{
    PhysAddr end, lo, hi;
    if (!pageLo || !pageHi || length == 0)
        return false;
    if (!PhysEnd(base, length, &end))
        return false;
    lo = PageAlignUp(base);
    hi = PageAlignDown(end);
    if (lo == RoboInvalidPhysAddr || lo >= hi)
        return false;
    *pageLo = lo;
    *pageHi = hi;
    return true;
}

uint64_t BootinfoFramebufferSize(const RoboBootInfo *info)
{
    uint64_t pitch, height, bytes;
    if (!info || !info->framebufferAddress)
        return 0;
    pitch = info->framebufferPitch;
    height = info->framebufferHeight;
    if (!pitch) {
        uint64_t w = info->framebufferWidth;
        uint64_t bpp = info->framebufferBpp ? info->framebufferBpp : 32;
        if (w > (UINT64_MAX / ((bpp + 7ull) / 8ull)))
            return 0;
        pitch = w * ((bpp + 7ull) / 8ull);
    }
    if (!height || pitch > UINT64_MAX / height)
        return 0;
    bytes = pitch * height;
    return bytes;
}

int MemoryMapLoad(MemoryMap *out, const RoboBootInfo *info)
{
    const RoboMemoryRegion *raw;
    uint64_t i, n;
    if (!out)
        return 0;
    out->count = 0;
    if (!info || info->magic != RoboBootinfoMagic)
        return 0;
    if (info->version < 1)
        return 0;
    if (!info->memoryMap || info->memoryMapCount == 0)
        return 0;
    n = info->memoryMapCount;
    if (n > RoboMmapMax)
        n = RoboMmapMax;
    raw = (const RoboMemoryRegion *)(uintptr_t)info->memoryMap;
    for (i = 0; i < n; i++) {
        PhysAddr end;
        MemoryRegion *r;
        if (raw[i].length == 0)
            continue;
        if (!PhysEnd(raw[i].address, raw[i].length, &end))
            continue;
        r = &out->regions[out->count];
        r->base = raw[i].address;
        r->length = raw[i].length;
        r->type = MemoryTypeFromBoot(raw[i].type);
        out->count++;
    }
    return out->count > 0;
}

int MemoryMapUsableLimit(const MemoryMap *map, PhysAddr *limit)
{
    size_t i;
    PhysAddr maxEnd = 0;
    if (!map || !limit || map->count == 0)
        return 0;
    for (i = 0; i < map->count; i++) {
        PhysAddr lo, hi;
        if (map->regions[i].type != RoboMemoryUsable)
            continue;
        if (!MemoryUsablePages(map->regions[i].base, map->regions[i].length, &lo, &hi))
            continue;
        if (hi > maxEnd)
            maxEnd = hi;
    }
    if (maxEnd == 0)
        return 0;
    *limit = maxEnd;
    return 1;
}
