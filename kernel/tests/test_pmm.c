#include "mm/page.h"
#include "mm/memory_map.h"
#include "mm/pmm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <malloc.h>
#endif

static int g_fails;
static int g_pass;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } \
} while (0)

#define HOST_PHYS 0x00100000ull
#define HOST_LEN  (8ull * 1024ull * 1024ull)

static void *arena;

static int test_page_helpers(void)
{
    phys_addr_t end;
    CHECK(ROBO_PAGE_SIZE == 4096ull, "page size");
    CHECK(robo_page_aligned(0), "0 aligned");
    CHECK(robo_page_aligned(0x401000), "kernel aligned");
    CHECK(!robo_page_aligned(1), "1 unaligned");
    CHECK(robo_page_align_down(0x401123) == 0x401000, "align down");
    CHECK(robo_page_align_up(0x401000) == 0x401000, "align up exact");
    CHECK(robo_page_align_up(0x401001) == 0x402000, "align up");
    CHECK(robo_phys_to_page(0x2000) == 2, "phys to page");
    CHECK(robo_page_to_phys(2) == 0x2000, "page to phys");
    CHECK(robo_bitmap_bytes(1) == 1, "bitmap 1 page");
    CHECK(robo_bitmap_bytes(8) == 1, "bitmap 8 pages");
    CHECK(robo_bitmap_bytes(9) == 2, "bitmap 9 pages");
    CHECK(robo_phys_end(0x1000, 0x2000, &end) && end == 0x3000, "phys end");
    CHECK(!robo_phys_end(ROBO_INVALID_PHYS_ADDR - 8, 16, &end), "overflow end");
    CHECK(robo_phys_ranges_overlap(0x1000, 0x1000, 0x1800, 0x1000), "overlap");
    CHECK(!robo_phys_ranges_overlap(0x1000, 0x1000, 0x2000, 0x1000), "adjacent");
    return g_fails == 0;
}

static int test_memory_map(void)
{
    RoboBootInfo info;
    RoboMemoryRegion regs[4];
    robo_memory_map_t map;
    phys_addr_t lo, hi, limit;

    memset(&info, 0, sizeof(info));
    memset(regs, 0, sizeof(regs));
    CHECK(!robo_memory_map_load(&map, NULL), "null info");

    info.magic = ROBO_BOOTINFO_MAGIC;
    info.version = 2;
    info.memory_map = (uint64_t)(uintptr_t)regs;
    info.memory_map_count = 3;
    regs[0].address = 0x123;
    regs[0].length = 0x3000;
    regs[0].type = ROBO_MEM_AVAILABLE;
    regs[1].address = 0x100000;
    regs[1].length = 0;
    regs[1].type = ROBO_MEM_AVAILABLE;
    regs[2].address = 0x200000;
    regs[2].length = 0x4000;
    regs[2].type = ROBO_MEM_RESERVED;

    CHECK(robo_memory_map_load(&map, &info), "load map");
    CHECK(map.count == 2, "skip zero length");
    CHECK(map.regions[0].type == ROBO_MEMORY_USABLE, "usable type");
    CHECK(map.regions[1].type == ROBO_MEMORY_RESERVED, "reserved type");
    CHECK(robo_memory_usable_pages(0x123, 0x3000, &lo, &hi), "usable pages");
    CHECK(lo == 0x1000 && hi == 0x3000, "partial pages excluded");
    CHECK(robo_memory_map_usable_limit(&map, &limit) && limit == 0x3000, "usable limit ignores reserved");
    CHECK(robo_memory_type_from_boot(ROBO_MEM_ACPI) == ROBO_MEMORY_ACPI_RECLAIMABLE, "acpi type");
    CHECK(robo_memory_type_from_boot(99) == ROBO_MEMORY_UNKNOWN, "unknown type");
    return 1;
}

static int build_fake_bootinfo(RoboBootInfo **out)
{
    RoboBootInfo *info;
    RoboMemoryRegion *regs;
    uint64_t kernel_len = 64ull * 1024ull;
    uint64_t bootinfo_len = 8192ull;
    uint64_t fb_len = 64ull * 1024ull;

    memset(arena, 0, (size_t)HOST_LEN);
    info = (RoboBootInfo *)arena;
    regs = (RoboMemoryRegion *)(info + 1);

    info->magic = ROBO_BOOTINFO_MAGIC;
    info->version = ROBO_BOOTINFO_VERSION;
    info->memory_map = (uint64_t)(uintptr_t)regs;
    info->memory_map_count = 4;
    info->kernel_base = HOST_PHYS + 0x10000;
    info->kernel_size = kernel_len;
    info->bootinfo_address = HOST_PHYS;
    info->bootinfo_size = bootinfo_len;
    info->framebuffer_address = HOST_PHYS + HOST_LEN - fb_len;
    info->framebuffer_width = 64;
    info->framebuffer_height = 64;
    info->framebuffer_pitch = 256;
    info->framebuffer_bpp = 32;

    regs[0].address = HOST_PHYS;
    regs[0].length = HOST_LEN;
    regs[0].type = ROBO_MEM_AVAILABLE;

    regs[1].address = HOST_PHYS + 0x80000;
    regs[1].length = 0x4000;
    regs[1].type = ROBO_MEM_RESERVED;

    regs[2].address = HOST_PHYS + 0xA0000;
    regs[2].length = 0x1000;
    regs[2].type = ROBO_MEM_ACPI;

    regs[3].address = HOST_PHYS + 0xB0000;
    regs[3].length = 0x1000;
    regs[3].type = ROBO_MEM_MMIO;

    *out = info;
    return 1;
}

static int test_pmm_live(void)
{
    RoboBootInfo *info;
    phys_addr_t p, hole, kernel_page;
    size_t i;

    if (!build_fake_bootinfo(&info))
        return 0;
    robo_pmm_host_bind(arena, HOST_PHYS, HOST_LEN);
    CHECK(robo_pmm_init(info), "pmm init");
    CHECK(robo_pmm_ready(), "pmm ready");
    CHECK(robo_page_count() > 0, "page count");
    CHECK(robo_free_page_count() > 200, "free pages");
    CHECK(robo_used_page_count() + robo_free_page_count() == robo_page_count(), "stats partition");

    kernel_page = robo_page_align_down((phys_addr_t)info->kernel_base);
    CHECK(robo_page_is_reserved(kernel_page), "kernel reserved");
    CHECK(!robo_page_is_free(kernel_page), "kernel not free");
    CHECK(robo_page_is_reserved(robo_pmm_bitmap_address()), "bitmap reserved");

    hole = robo_page_align_down((phys_addr_t)(HOST_PHYS + 0x80000));
    CHECK(robo_page_is_reserved(hole), "overlap reserved");
    CHECK(robo_page_is_reserved(robo_page_align_down((phys_addr_t)(HOST_PHYS + 0xA0000))), "acpi reserved");
    CHECK(robo_page_is_reserved(robo_page_align_down((phys_addr_t)(HOST_PHYS + 0xB0000))), "mmio reserved");

    p = robo_page_alloc();
    CHECK(p != ROBO_INVALID_PHYS_ADDR, "alloc");
    CHECK(robo_page_aligned(p), "alloc aligned");
    CHECK(p != kernel_page, "not kernel");
    CHECK(p != hole, "not reserved hole");
    CHECK(p >= HOST_PHYS && p < HOST_PHYS + HOST_LEN, "alloc inside arena");
    for (i = 0; i < robo_pmm_bitmap_bytes(); i += (size_t)ROBO_PAGE_SIZE)
        CHECK(p != robo_pmm_bitmap_address() + i, "not bitmap");
    robo_page_free(p);

    CHECK(robo_pmm_selftest() == 1, "selftest");
    return 1;
}

int main(void)
{
#ifdef _WIN32
    arena = _aligned_malloc((size_t)HOST_LEN, 4096);
#else
    if (posix_memalign(&arena, 4096, (size_t)HOST_LEN) != 0)
        arena = NULL;
#endif
    if (!arena) {
        fprintf(stderr, "FAIL arena alloc\n");
        return 1;
    }

    test_page_helpers();
    test_memory_map();
    test_pmm_live();

#ifdef _WIN32
    _aligned_free(arena);
#else
    free(arena);
#endif

    printf("PMM host tests  %d passed  %d failed\n", g_pass, g_fails);
    return g_fails ? 1 : 0;
}
