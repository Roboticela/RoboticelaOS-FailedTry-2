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

static int gFails;
static int gPass;

#define CHECK(cond, msg) do { \
    if (cond) { gPass++; } \
    else { gFails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } \
} while (0)

#define HOST_PHYS 0x00100000ull
#define HOST_LEN  (8ull * 1024ull * 1024ull)

static void *arena;

static int TestPageHelpers(void)
{
    PhysAddr end;
    CHECK(RoboPageSize == 4096ull, "page size");
    CHECK(PageAligned(0), "0 aligned");
    CHECK(PageAligned(0x401000), "kernel aligned");
    CHECK(!PageAligned(1), "1 unaligned");
    CHECK(PageAlignDown(0x401123) == 0x401000, "align down");
    CHECK(PageAlignUp(0x401000) == 0x401000, "align up exact");
    CHECK(PageAlignUp(0x401001) == 0x402000, "align up");
    CHECK(PhysToPage(0x2000) == 2, "phys to page");
    CHECK(PageToPhys(2) == 0x2000, "page to phys");
    CHECK(BitmapBytes(1) == 1, "bitmap 1 page");
    CHECK(BitmapBytes(8) == 1, "bitmap 8 pages");
    CHECK(BitmapBytes(9) == 2, "bitmap 9 pages");
    CHECK(PhysEnd(0x1000, 0x2000, &end) && end == 0x3000, "phys end");
    CHECK(!PhysEnd(RoboInvalidPhysAddr - 8, 16, &end), "overflow end");
    CHECK(PhysRangesOverlap(0x1000, 0x1000, 0x1800, 0x1000), "overlap");
    CHECK(!PhysRangesOverlap(0x1000, 0x1000, 0x2000, 0x1000), "adjacent");
    return gFails == 0;
}

static int TestMemoryMap(void)
{
    RoboBootInfo info;
    RoboMemoryRegion regs[4];
    MemoryMap map;
    PhysAddr lo, hi, limit;

    memset(&info, 0, sizeof(info));
    memset(regs, 0, sizeof(regs));
    CHECK(!MemoryMapLoad(&map, NULL), "null info");

    info.magic = RoboBootinfoMagic;
    info.version = 2;
    info.memoryMap = (uint64_t)(uintptr_t)regs;
    info.memoryMapCount = 3;
    regs[0].address = 0x123;
    regs[0].length = 0x3000;
    regs[0].type = RoboMemAvailable;
    regs[1].address = 0x100000;
    regs[1].length = 0;
    regs[1].type = RoboMemAvailable;
    regs[2].address = 0x200000;
    regs[2].length = 0x4000;
    regs[2].type = RoboMemReserved;

    CHECK(MemoryMapLoad(&map, &info), "load map");
    CHECK(map.count == 2, "skip zero length");
    CHECK(map.regions[0].type == RoboMemoryUsable, "usable type");
    CHECK(map.regions[1].type == RoboMemoryReserved, "reserved type");
    CHECK(MemoryUsablePages(0x123, 0x3000, &lo, &hi), "usable pages");
    CHECK(lo == 0x1000 && hi == 0x3000, "partial pages excluded");
    CHECK(MemoryMapUsableLimit(&map, &limit) && limit == 0x3000, "usable limit ignores reserved");
    CHECK(MemoryTypeFromBoot(RoboMemAcpi) == RoboMemoryAcpiReclaimable, "acpi type");
    CHECK(MemoryTypeFromBoot(99) == RoboMemoryUnknown, "unknown type");
    return 1;
}

static int BuildFakeBootinfo(RoboBootInfo **out)
{
    RoboBootInfo *info;
    RoboMemoryRegion *regs;
    uint64_t kernelLen = 64ull * 1024ull;
    uint64_t bootinfoLen = 8192ull;
    uint64_t fbLen = 64ull * 1024ull;

    memset(arena, 0, (size_t)HOST_LEN);
    info = (RoboBootInfo *)arena;
    regs = (RoboMemoryRegion *)(info + 1);

    info->magic = RoboBootinfoMagic;
    info->version = RoboBootinfoVersion;
    info->memoryMap = (uint64_t)(uintptr_t)regs;
    info->memoryMapCount = 4;
    info->kernelBase = HOST_PHYS + 0x10000;
    info->kernelSize = kernelLen;
    info->bootinfoAddress = HOST_PHYS;
    info->bootinfoSize = bootinfoLen;
    info->framebufferAddress = HOST_PHYS + HOST_LEN - fbLen;
    info->framebufferWidth = 64;
    info->framebufferHeight = 64;
    info->framebufferPitch = 256;
    info->framebufferBpp = 32;

    regs[0].address = HOST_PHYS;
    regs[0].length = HOST_LEN;
    regs[0].type = RoboMemAvailable;

    regs[1].address = HOST_PHYS + 0x80000;
    regs[1].length = 0x4000;
    regs[1].type = RoboMemReserved;

    regs[2].address = HOST_PHYS + 0xA0000;
    regs[2].length = 0x1000;
    regs[2].type = RoboMemAcpi;

    regs[3].address = HOST_PHYS + 0xB0000;
    regs[3].length = 0x1000;
    regs[3].type = RoboMemMmio;

    *out = info;
    return 1;
}

static int TestPmmLive(void)
{
    RoboBootInfo *info;
    PhysAddr p, hole, kernelPage;
    size_t i;

    if (!BuildFakeBootinfo(&info))
        return 0;
    PmmHostBind(arena, HOST_PHYS, HOST_LEN);
    CHECK(PmmInit(info), "pmm init");
    CHECK(PmmReady(), "pmm ready");
    CHECK(PageCount() > 0, "page count");
    CHECK(FreePageCount() > 200, "free pages");
    CHECK(UsedPageCount() + FreePageCount() == PageCount(), "stats partition");

    kernelPage = PageAlignDown((PhysAddr)info->kernelBase);
    CHECK(PageIsReserved(kernelPage), "kernel reserved");
    CHECK(!PageIsFree(kernelPage), "kernel not free");
    CHECK(PageIsReserved(PmmBitmapAddress()), "bitmap reserved");

    hole = PageAlignDown((PhysAddr)(HOST_PHYS + 0x80000));
    CHECK(PageIsReserved(hole), "overlap reserved");
    CHECK(PageIsReserved(PageAlignDown((PhysAddr)(HOST_PHYS + 0xA0000))), "acpi reserved");
    CHECK(PageIsReserved(PageAlignDown((PhysAddr)(HOST_PHYS + 0xB0000))), "mmio reserved");

    p = PageAlloc();
    CHECK(p != RoboInvalidPhysAddr, "alloc");
    CHECK(PageAligned(p), "alloc aligned");
    CHECK(p != kernelPage, "not kernel");
    CHECK(p != hole, "not reserved hole");
    CHECK(p >= HOST_PHYS && p < HOST_PHYS + HOST_LEN, "alloc inside arena");
    for (i = 0; i < PmmBitmapBytes(); i += (size_t)RoboPageSize)
        CHECK(p != PmmBitmapAddress() + i, "not bitmap");
    PageFree(p);

    CHECK(PmmSelftest() == 1, "selftest");
    return 1;
}

int main(void)
{
#ifdef _WIN32
    arena = AlignedMalloc((size_t)HOST_LEN, 4096);
#else
    if (PosixMemalign(&arena, 4096, (size_t)HOST_LEN) != 0)
        arena = NULL;
#endif
    if (!arena) {
        fprintf(stderr, "FAIL arena alloc\n");
        return 1;
    }

    TestPageHelpers();
    TestMemoryMap();
    TestPmmLive();

#ifdef _WIN32
    AlignedFree(arena);
#else
    free(arena);
#endif

    printf("PMM host tests  %d passed  %d failed\n", gPass, gFails);
    return gFails ? 1 : 0;
}
