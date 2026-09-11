#include "mm/memory_map.h"

robo_memory_type_t robo_memory_type_from_boot(uint32_t boot_type)
{
    switch (boot_type) {
    case ROBO_MEM_AVAILABLE:
        return ROBO_MEMORY_USABLE;
    case ROBO_MEM_RESERVED:
        return ROBO_MEMORY_RESERVED;
    case ROBO_MEM_ACPI:
        return ROBO_MEMORY_ACPI_RECLAIMABLE;
    case ROBO_MEM_NVS:
        return ROBO_MEMORY_ACPI_NVS;
    case ROBO_MEM_MMIO:
        return ROBO_MEMORY_MMIO;
    case ROBO_MEM_LOADER:
        return ROBO_MEMORY_BOOTLOADER;
    default:
        return ROBO_MEMORY_UNKNOWN;
    }
}

const char *robo_memory_type_name(robo_memory_type_t type)
{
    switch (type) {
    case ROBO_MEMORY_USABLE: return "usable";
    case ROBO_MEMORY_RESERVED: return "reserved";
    case ROBO_MEMORY_ACPI_RECLAIMABLE: return "acpi-reclaim";
    case ROBO_MEMORY_ACPI_NVS: return "acpi-nvs";
    case ROBO_MEMORY_BOOTLOADER: return "bootloader";
    case ROBO_MEMORY_KERNEL: return "kernel";
    case ROBO_MEMORY_FRAMEBUFFER: return "framebuffer";
    case ROBO_MEMORY_MMIO: return "mmio";
    case ROBO_MEMORY_RUNTIME: return "runtime";
    default: return "unknown";
    }
}

bool robo_memory_usable_pages(phys_addr_t base, uint64_t length,
                              phys_addr_t *page_lo, phys_addr_t *page_hi)
{
    phys_addr_t end, lo, hi;
    if (!page_lo || !page_hi || length == 0)
        return false;
    if (!robo_phys_end(base, length, &end))
        return false;
    lo = robo_page_align_up(base);
    hi = robo_page_align_down(end);
    if (lo == ROBO_INVALID_PHYS_ADDR || lo >= hi)
        return false;
    *page_lo = lo;
    *page_hi = hi;
    return true;
}

uint64_t robo_bootinfo_framebuffer_size(const RoboBootInfo *info)
{
    uint64_t pitch, height, bytes;
    if (!info || !info->framebuffer_address)
        return 0;
    pitch = info->framebuffer_pitch;
    height = info->framebuffer_height;
    if (!pitch) {
        uint64_t w = info->framebuffer_width;
        uint64_t bpp = info->framebuffer_bpp ? info->framebuffer_bpp : 32;
        if (w > (UINT64_MAX / ((bpp + 7ull) / 8ull)))
            return 0;
        pitch = w * ((bpp + 7ull) / 8ull);
    }
    if (!height || pitch > UINT64_MAX / height)
        return 0;
    bytes = pitch * height;
    return bytes;
}

int robo_memory_map_load(robo_memory_map_t *out, const RoboBootInfo *info)
{
    const RoboMemoryRegion *raw;
    uint64_t i, n;
    if (!out)
        return 0;
    out->count = 0;
    if (!info || info->magic != ROBO_BOOTINFO_MAGIC)
        return 0;
    if (info->version < 1)
        return 0;
    if (!info->memory_map || info->memory_map_count == 0)
        return 0;
    n = info->memory_map_count;
    if (n > ROBO_MMAP_MAX)
        n = ROBO_MMAP_MAX;
    raw = (const RoboMemoryRegion *)(uintptr_t)info->memory_map;
    for (i = 0; i < n; i++) {
        phys_addr_t end;
        robo_memory_region_t *r;
        if (raw[i].length == 0)
            continue;
        if (!robo_phys_end(raw[i].address, raw[i].length, &end))
            continue;
        r = &out->regions[out->count];
        r->base = raw[i].address;
        r->length = raw[i].length;
        r->type = robo_memory_type_from_boot(raw[i].type);
        out->count++;
    }
    return out->count > 0;
}

int robo_memory_map_usable_limit(const robo_memory_map_t *map, phys_addr_t *limit)
{
    size_t i;
    phys_addr_t max_end = 0;
    if (!map || !limit || map->count == 0)
        return 0;
    for (i = 0; i < map->count; i++) {
        phys_addr_t lo, hi;
        if (map->regions[i].type != ROBO_MEMORY_USABLE)
            continue;
        if (!robo_memory_usable_pages(map->regions[i].base, map->regions[i].length, &lo, &hi))
            continue;
        if (hi > max_end)
            max_end = hi;
    }
    if (max_end == 0)
        return 0;
    *limit = max_end;
    return 1;
}
