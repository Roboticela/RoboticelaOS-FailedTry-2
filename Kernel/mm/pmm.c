#include "mm/pmm.h"

#ifdef PMM_HOST_TEST
#include <stdio.h>
#define kprintf printf
#define kinfo printf
#else
#include "kio.h"
extern char __kernel_start[];
extern char __kernel_end[];
extern char kstack[];
extern char kstackEnd[];
#endif

typedef struct {
    PhysAddr base;
    uint64_t length;
} PmmSpan;

static struct {
    int ready;
    int debug;
    uint8_t *occupancy;
    uint8_t *sticky;
    PhysAddr bitmapPhys;
    size_t bitmapBytes;
    size_t storageBytes;
    size_t pageCount;
    size_t freePages;
    size_t allocHint;
    PhysAddr limit;
    PhysAddr kernelBase;
    uint64_t kernelSize;
    PhysAddr fbBase;
    uint64_t fbSize;
    const RoboBootInfo *info;
} gPmm;

#ifdef PMM_HOST_TEST
static uint8_t *gHostVirt;
static PhysAddr gHostPhys;
static uint64_t gHostLen;

void PmmHostBind(void *virt, PhysAddr phys, uint64_t len)
{
    gHostVirt = (uint8_t *)virt;
    gHostPhys = phys;
    gHostLen = len;
}

static void *PmmPhysPtr(PhysAddr p)
{
    uint64_t off;
    if (!gHostVirt || p < gHostPhys)
        return 0;
    off = (uint64_t)(p - gHostPhys);
    if (off >= gHostLen)
        return 0;
    return gHostVirt + (size_t)off;
}
#else
static void *PmmPhysPtr(PhysAddr p)
{
    return (void *)(uintptr_t)p;
}
#endif

static void PmmFill(void *dst, uint8_t v, size_t n)
{
    uint8_t *p = (uint8_t *)dst;
    if (!p)
        return;
    while (n--)
        *p++ = v;
}

static int BitGet(const uint8_t *b, size_t i)
{
    return (b[i >> 3] >> (i & 7u)) & 1;
}

static void BitSet(uint8_t *b, size_t i)
{
    b[i >> 3] = (uint8_t)(b[i >> 3] | (uint8_t)(1u << (i & 7u)));
}

static void BitClear(uint8_t *b, size_t i)
{
    b[i >> 3] = (uint8_t)(b[i >> 3] & (uint8_t)~(1u << (i & 7u)));
}

static void AddSpan(PmmSpan *v, size_t *n, size_t max, PhysAddr base, uint64_t length)
{
    if (!length || *n >= max)
        return;
    v[*n].base = base;
    v[*n].length = length;
    (*n)++;
}

static int SpanOverlap(PhysAddr a0, uint64_t aLen, const PmmSpan *s)
{
    return PhysRangesOverlap(a0, aLen, s->base, s->length);
}

static PhysAddr FindBitmapHome(const MemoryMap *map,
                                    size_t need,
                                    const PmmSpan *forbid,
                                    size_t nforbid)
{
    size_t ri;
    PhysAddr best = RoboInvalidPhysAddr;
    uint64_t need64 = (uint64_t)need;

    for (ri = 0; ri < map->count; ri++) {
        PhysAddr lo, hi, cand;
        if (map->regions[ri].type != RoboMemoryUsable)
            continue;
        if (!MemoryUsablePages(map->regions[ri].base, map->regions[ri].length, &lo, &hi))
            continue;
        cand = lo;
        if (cand == 0)
            cand = RoboPageSize;
        for (;;) {
            size_t fi;
            int hit = 0;
            PhysAddr end;
            if (cand >= hi)
                break;
            if (!PhysEnd(cand, need64, &end) || end > hi)
                break;
            for (fi = 0; fi < nforbid; fi++) {
                PhysAddr fend, next;
                if (!SpanOverlap(cand, need64, &forbid[fi]))
                    continue;
                if (!PhysEnd(forbid[fi].base, forbid[fi].length, &fend)) {
                    cand = hi;
                    hit = 1;
                    break;
                }
                next = PageAlignUp(fend);
                if (next == RoboInvalidPhysAddr || next <= cand)
                    next = cand + RoboPageSize;
                cand = next;
                hit = 1;
                break;
            }
            if (hit)
                continue;
            if (cand == 0) {
                cand = RoboPageSize;
                continue;
            }
            if (best == RoboInvalidPhysAddr || cand < best)
                best = cand;
            break;
        }
    }
    return best;
}

static void MarkReservedPage(size_t page)
{
    if (page >= gPmm.pageCount)
        return;
    if (!BitGet(gPmm.occupancy, page)) {
        BitSet(gPmm.occupancy, page);
        if (gPmm.freePages)
            gPmm.freePages--;
    }
    BitSet(gPmm.sticky, page);
}

static void MarkFreePage(size_t page)
{
    if (page >= gPmm.pageCount)
        return;
    if (BitGet(gPmm.occupancy, page)) {
        BitClear(gPmm.occupancy, page);
        gPmm.freePages++;
    }
    BitClear(gPmm.sticky, page);
}

static void ReserveRange(PhysAddr base, uint64_t length)
{
    PhysAddr end, lo, hi, addr;
    if (!length || !gPmm.pageCount)
        return;
    if (!PhysEnd(base, length, &end))
        return;
    lo = PageAlignDown(base);
    hi = PageAligned(end) ? end : PageAlignUp(end);
    if (hi == RoboInvalidPhysAddr)
        hi = gPmm.limit;
    if (lo >= gPmm.limit)
        return;
    if (hi > gPmm.limit)
        hi = gPmm.limit;
    for (addr = lo; addr < hi; addr += RoboPageSize)
        MarkReservedPage(PhysToPage(addr));
}

static void ReleaseUsable(const MemoryMap *map)
{
    size_t i;
    for (i = 0; i < map->count; i++) {
        PhysAddr lo, hi, addr;
        if (map->regions[i].type != RoboMemoryUsable)
            continue;
        if (!MemoryUsablePages(map->regions[i].base, map->regions[i].length, &lo, &hi))
            continue;
        if (hi > gPmm.limit)
            hi = gPmm.limit;
        for (addr = lo; addr < hi; addr += RoboPageSize)
            MarkFreePage(PhysToPage(addr));
    }
}

static void ReserveNonUsable(const MemoryMap *map)
{
    size_t i;
    for (i = 0; i < map->count; i++) {
        if (map->regions[i].type == RoboMemoryUsable)
            continue;
        ReserveRange(map->regions[i].base, map->regions[i].length);
    }
}

static void DumpMap(const MemoryMap *map)
{
    size_t i, n;
    n = map->count;
    if (n > 16)
        n = 16;
    for (i = 0; i < n; i++) {
        kprintf("PMM map[%llu] %s 0x%llx +0x%llx\n",
                (unsigned long long)i,
                MemoryTypeName(map->regions[i].type),
                (unsigned long long)map->regions[i].base,
                (unsigned long long)map->regions[i].length);
    }
    if (map->count > n)
        kprintf("PMM map  %llu more regions\n", (unsigned long long)(map->count - n));
}

int PmmReady(void)
{
    return gPmm.ready;
}

PhysAddr PmmBitmapAddress(void)
{
    return gPmm.bitmapPhys;
}

size_t PmmBitmapBytes(void)
{
    return gPmm.storageBytes;
}

PhysAddr PmmLimit(void)
{
    return gPmm.limit;
}

size_t PageCount(void)
{
    return gPmm.pageCount;
}

size_t FreePageCount(void)
{
    return gPmm.freePages;
}

size_t UsedPageCount(void)
{
    if (gPmm.pageCount < gPmm.freePages)
        return 0;
    return gPmm.pageCount - gPmm.freePages;
}

bool PageIsFree(PhysAddr page)
{
    size_t idx;
    if (!gPmm.ready || !PageAligned(page) || page >= gPmm.limit)
        return false;
    idx = PhysToPage(page);
    if (idx >= gPmm.pageCount)
        return false;
    return !BitGet(gPmm.occupancy, idx);
}

bool PageIsReserved(PhysAddr page)
{
    size_t idx;
    if (!gPmm.ready || !PageAligned(page) || page >= gPmm.limit)
        return true;
    idx = PhysToPage(page);
    if (idx >= gPmm.pageCount)
        return true;
    return BitGet(gPmm.sticky, idx) != 0;
}

PhysAddr PageAlloc(void)
{
    size_t i, start;
    if (!gPmm.ready || gPmm.freePages == 0)
        return RoboInvalidPhysAddr;
    start = gPmm.allocHint;
    for (i = start; i < gPmm.pageCount; i++) {
        if (!BitGet(gPmm.occupancy, i)) {
            BitSet(gPmm.occupancy, i);
            gPmm.freePages--;
            gPmm.allocHint = i + 1;
            return PageToPhys(i);
        }
    }
    for (i = 0; i < start; i++) {
        if (!BitGet(gPmm.occupancy, i)) {
            BitSet(gPmm.occupancy, i);
            gPmm.freePages--;
            gPmm.allocHint = i + 1;
            return PageToPhys(i);
        }
    }
    return RoboInvalidPhysAddr;
}

void PageFree(PhysAddr page)
{
    size_t idx;
    if (!gPmm.ready) {
        kprintf("PMM: free before init: 0x%llx\n", (unsigned long long)page);
        return;
    }
    if (!PageAligned(page)) {
        kprintf("PMM: unaligned free: 0x%llx\n", (unsigned long long)page);
        return;
    }
    if (page >= gPmm.limit) {
        kprintf("PMM: out-of-range free: 0x%llx\n", (unsigned long long)page);
        return;
    }
    idx = PhysToPage(page);
    if (idx >= gPmm.pageCount) {
        kprintf("PMM: out-of-range free: 0x%llx\n", (unsigned long long)page);
        return;
    }
    if (BitGet(gPmm.sticky, idx)) {
        kprintf("PMM: refuse reserved free: 0x%llx\n", (unsigned long long)page);
        return;
    }
    if (!BitGet(gPmm.occupancy, idx)) {
        kprintf("PMM: double free detected: 0x%llx\n", (unsigned long long)page);
        return;
    }
    BitClear(gPmm.occupancy, idx);
    gPmm.freePages++;
    if (idx < gPmm.allocHint)
        gPmm.allocHint = idx;
}

void PmmReport(void)
{
    unsigned long long managed, reserved;
    if (!gPmm.ready) {
        kprintf("PMM: not initialized\n");
        return;
    }
    managed = (unsigned long long)gPmm.pageCount * RoboPageSize;
    reserved = (unsigned long long)UsedPageCount();
    kprintf("Roboticela PMM\n");
    kprintf("--------------\n");
    kprintf("Page size:       %llu bytes\n", (unsigned long long)RoboPageSize);
    kprintf("Physical pages:  %llu\n", (unsigned long long)gPmm.pageCount);
    kprintf("Managed memory:  %llu MiB\n", managed / (1024ull * 1024ull));
    kprintf("Free pages:      %llu\n", (unsigned long long)gPmm.freePages);
    kprintf("Reserved pages:  %llu\n", reserved);
    kprintf("Bitmap:          %llu bytes\n", (unsigned long long)gPmm.storageBytes);
    kprintf("Bitmap address:  0x%llx\n", (unsigned long long)gPmm.bitmapPhys);
}

#ifndef PMM_HOST_TEST
void PmmFbReport(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    if (!gPmm.ready)
        return;
    KfbPuts("PMM  pages ", mute);
    KfbPutu((unsigned long long)gPmm.pageCount, white);
    KfbPuts("  free ", mute);
    KfbPutu((unsigned long long)gPmm.freePages, white);
    KfbPuts("\nPMM  bitmap ", mute);
    KfbPuthex((unsigned long long)gPmm.bitmapPhys, white);
    KfbPuts("  ", mute);
    KfbPutu((unsigned long long)gPmm.storageBytes, white);
    KfbPuts(" bytes\n", mute);
}
#endif

bool PmmInit(const RoboBootInfo *info)
{
    MemoryMap map;
    PmmSpan forbid[8];
    size_t nforbid = 0;
    PhysAddr limit = 0;
    PhysAddr home;
    size_t bitmapBytes, storage, storagePages;
    PhysAddr bootinfoPhys;
    uint64_t bootinfoSize;
    uint64_t fbSize;

    PmmFill(&gPmm, 0, sizeof(gPmm));

    kinfo("PMM init\n");
    if (!info || info->magic != RoboBootinfoMagic) {
        kprintf("PMM: missing bootinfo\n");
        return false;
    }
    if (info->version < 1) {
        kprintf("PMM: unsupported bootinfo version %u\n", (unsigned)info->version);
        return false;
    }
    gPmm.info = info;
    gPmm.debug = (info->flags & RoboBootDebug) != 0;

    if (!MemoryMapLoad(&map, info)) {
        kprintf("PMM: empty or invalid memory map\n");
        return false;
    }
    if (!MemoryMapUsableLimit(&map, &limit) || limit < RoboPageSize) {
        kprintf("PMM: no usable physical pages\n");
        return false;
    }
    if (gPmm.debug)
        DumpMap(&map);

    gPmm.limit = limit;
    gPmm.pageCount = (size_t)(limit >> RoboPageShift);
    if (!gPmm.pageCount || PageToPhys(gPmm.pageCount) != limit) {
        kprintf("PMM: page count overflow\n");
        PmmFill(&gPmm, 0, sizeof(gPmm));
        return false;
    }

    bitmapBytes = BitmapBytes(gPmm.pageCount);
    if (!bitmapBytes || bitmapBytes > ((size_t)-1) / 2u) {
        kprintf("PMM: bitmap size overflow\n");
        PmmFill(&gPmm, 0, sizeof(gPmm));
        return false;
    }
    storage = bitmapBytes * 2u;
    {
        PhysAddr aligned = PageAlignUp((PhysAddr)storage);
        if (aligned == RoboInvalidPhysAddr || aligned < (PhysAddr)storage) {
            kprintf("PMM: bitmap storage overflow\n");
            PmmFill(&gPmm, 0, sizeof(gPmm));
            return false;
        }
        storage = (size_t)aligned;
        storagePages = (size_t)(aligned / RoboPageSize);
    }
    if (!storagePages) {
        kprintf("PMM: bitmap storage empty\n");
        PmmFill(&gPmm, 0, sizeof(gPmm));
        return false;
    }

    gPmm.kernelBase = info->kernelBase;
    gPmm.kernelSize = info->kernelSize;
#ifndef PMM_HOST_TEST
    {
        PhysAddr ks = (PhysAddr)(uintptr_t)__kernel_start;
        PhysAddr ke = (PhysAddr)(uintptr_t)__kernel_end;
        PhysAddr ss = (PhysAddr)(uintptr_t)kstack;
        PhysAddr se = (PhysAddr)(uintptr_t)kstackEnd;
        if (ke > ks) {
            uint64_t klen = (uint64_t)(ke - ks);
            if (!gPmm.kernelSize) {
                gPmm.kernelBase = ks;
                gPmm.kernelSize = klen;
            } else {
                PhysAddr a0 = gPmm.kernelBase < ks ? gPmm.kernelBase : ks;
                PhysAddr a1 = gPmm.kernelBase + gPmm.kernelSize;
                if (ke > a1)
                    a1 = ke;
                gPmm.kernelBase = a0;
                gPmm.kernelSize = (uint64_t)(a1 - a0);
            }
        }
        if (se > ss)
            AddSpan(forbid, &nforbid, 8, ss, (uint64_t)(se - ss));
    }
#endif
    AddSpan(forbid, &nforbid, 8, gPmm.kernelBase, gPmm.kernelSize);

    bootinfoPhys = (info->version >= 2 && info->bootinfoAddress)
                        ? info->bootinfoAddress
                        : (PhysAddr)(uintptr_t)info;
    bootinfoSize = (info->version >= 2 && info->bootinfoSize)
                        ? info->bootinfoSize
                        : 8192ull;
    AddSpan(forbid, &nforbid, 8, bootinfoPhys, bootinfoSize);
    AddSpan(forbid, &nforbid, 8, info->initrdAddress, info->initrdSize);
    if (info->version >= 3 && info->debugLogAddress && info->debugLogSize)
        AddSpan(forbid, &nforbid, 8, info->debugLogAddress, info->debugLogSize);

    fbSize = BootinfoFramebufferSize(info);
    gPmm.fbBase = info->framebufferAddress;
    gPmm.fbSize = fbSize;
    AddSpan(forbid, &nforbid, 8, gPmm.fbBase, gPmm.fbSize);

    home = FindBitmapHome(&map, storage, forbid, nforbid);
    if (home == RoboInvalidPhysAddr) {
        kprintf("PMM: no room for bitmap\n");
        PmmFill(&gPmm, 0, sizeof(gPmm));
        return false;
    }

    gPmm.bitmapPhys = home;
    gPmm.bitmapBytes = bitmapBytes;
    gPmm.storageBytes = storage;
    gPmm.occupancy = (uint8_t *)PmmPhysPtr(home);
    if (!gPmm.occupancy) {
        kprintf("PMM: bitmap is not addressable\n");
        PmmFill(&gPmm, 0, sizeof(gPmm));
        return false;
    }
    gPmm.sticky = gPmm.occupancy + bitmapBytes;
    PmmFill(gPmm.occupancy, 0xFFu, storage);
    gPmm.freePages = 0;
    gPmm.allocHint = 0;

    kinfo("PMM  usable limit 0x%llx  pages %llu\n",
            (unsigned long long)limit, (unsigned long long)gPmm.pageCount);
    kinfo("PMM  bitmap 0x%llx  %llu bytes\n",
            (unsigned long long)home, (unsigned long long)storage);

    ReleaseUsable(&map);
    ReserveNonUsable(&map);
    /* Physical page 0 is never a safe identity-map pointer. */
    ReserveRange(0, RoboPageSize);
    ReserveRange(gPmm.kernelBase, gPmm.kernelSize);
    ReserveRange(bootinfoPhys, bootinfoSize);
    ReserveRange(info->initrdAddress, info->initrdSize);
    if (info->version >= 3 && info->debugLogAddress && info->debugLogSize)
        ReserveRange(info->debugLogAddress, info->debugLogSize);
    ReserveRange(gPmm.fbBase, gPmm.fbSize);
    ReserveRange(home, (uint64_t)storage);
#ifndef PMM_HOST_TEST
    ReserveRange((PhysAddr)(uintptr_t)kstack,
                  (uint64_t)((uintptr_t)kstackEnd - (uintptr_t)kstack));
#endif

    if (gPmm.freePages == 0) {
        kprintf("PMM: no free pages after reserve\n");
        PmmFill(&gPmm, 0, sizeof(gPmm));
        return false;
    }

    gPmm.ready = 1;
    kinfo("PMM  kernel 0x%llx +%llu  bootinfo 0x%llx +%llu\n",
            (unsigned long long)gPmm.kernelBase,
            (unsigned long long)gPmm.kernelSize,
            (unsigned long long)bootinfoPhys,
            (unsigned long long)bootinfoSize);
    kinfo("PMM  free %llu  reserved %llu\n",
            (unsigned long long)gPmm.freePages,
            (unsigned long long)UsedPageCount());
    return true;
}
