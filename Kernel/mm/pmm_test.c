#include "mm/pmm.h"

#ifdef PMM_HOST_TEST
#include <stdio.h>
#define kprintf printf
#else
#include "kio.h"
#endif

static int UniquePages(const PhysAddr *pages, size_t n)
{
    size_t i, j;
    for (i = 0; i < n; i++) {
        if (pages[i] == RoboInvalidPhysAddr || !PageAligned(pages[i]))
            return 0;
        for (j = i + 1; j < n; j++) {
            if (pages[i] == pages[j])
                return 0;
        }
    }
    return 1;
}

static int overlaps(PhysAddr page, PhysAddr base, uint64_t length)
{
    if (!length)
        return 0;
    return PhysRangesOverlap(page, RoboPageSize, base, length);
}

static void RestoreDynamic(void)
{
    size_t i;
    size_t n = PageCount();
    for (i = 0; i < n; i++) {
        PhysAddr p = PageToPhys(i);
        if (PageIsReserved(p))
            continue;
        if (!PageIsFree(p))
            PageFree(p);
    }
}

int PmmSelftest(void)
{
    PhysAddr one, batch[100];
    PhysAddr probe[64];
    size_t initialFree, after, i, nOk;
    PhysAddr bitmap, pastEnd, unaligned;
    uint64_t bsize;

    if (!PmmReady()) {
        kprintf("PMM selftest: not ready\n");
        return 0;
    }

    initialFree = FreePageCount();
    if (initialFree < 200) {
        kprintf("PMM selftest: too few free pages\n");
        return 0;
    }

    one = PageAlloc();
    if (one == RoboInvalidPhysAddr || !PageAligned(one)) {
        kprintf("PMM selftest: alloc-one failed\n");
        return 0;
    }
    if (FreePageCount() != initialFree - 1) {
        kprintf("PMM selftest: alloc-one count\n");
        PageFree(one);
        return 0;
    }
    if (PageIsFree(one) || PageIsReserved(one)) {
        kprintf("PMM selftest: alloc-one flags\n");
        PageFree(one);
        return 0;
    }
    PageFree(one);
    if (FreePageCount() != initialFree) {
        kprintf("PMM selftest: free-one count\n");
        return 0;
    }

    after = FreePageCount();
    for (i = 0; i < 100; i++)
        batch[i] = PageAlloc();
    if (!UniquePages(batch, 100) || FreePageCount() != after - 100) {
        kprintf("PMM selftest: alloc-100 failed\n");
        for (i = 0; i < 100; i++) {
            if (batch[i] != RoboInvalidPhysAddr)
                PageFree(batch[i]);
        }
        return 0;
    }
    for (i = 0; i < 100; i++)
        PageFree(batch[i]);
    if (FreePageCount() != after) {
        kprintf("PMM selftest: free-100 count\n");
        return 0;
    }

    one = PageAlloc();
    after = FreePageCount();
    PageFree(one);
    PageFree(one);
    if (FreePageCount() != after + 1) {
        kprintf("PMM selftest: double-free corrupted counters\n");
        return 0;
    }
    one = PageAlloc();
    if (one == RoboInvalidPhysAddr) {
        kprintf("PMM selftest: realloc after double-free failed\n");
        return 0;
    }
    PageFree(one);

    bitmap = PmmBitmapAddress();
    bsize = (uint64_t)PmmBitmapBytes();
    nOk = 0;
    after = FreePageCount();
    for (i = 0; i < 64; i++) {
        probe[i] = PageAlloc();
        if (probe[i] == RoboInvalidPhysAddr)
            break;
        if (overlaps(probe[i], bitmap, bsize)) {
            kprintf("PMM selftest: allocated bitmap page 0x%llx\n",
                    (unsigned long long)probe[i]);
            RestoreDynamic();
            return 0;
        }
        nOk++;
    }
    if (nOk == 0) {
        kprintf("PMM selftest: reserved-probe alloc failed\n");
        return 0;
    }
    for (i = 0; i < nOk; i++)
        PageFree(probe[i]);
    if (FreePageCount() != after) {
        kprintf("PMM selftest: reserved-probe free count\n");
        return 0;
    }

    after = FreePageCount();
    PageFree(bitmap);
    if (PageIsFree(bitmap) || FreePageCount() != after) {
        kprintf("PMM selftest: bitmap became free\n");
        return 0;
    }

    unaligned = bitmap ? bitmap + 1 : 1;
    pastEnd = PmmLimit();
    PageFree(unaligned);
    PageFree(pastEnd);
    PageFree(RoboInvalidPhysAddr);
    if (FreePageCount() != after) {
        kprintf("PMM selftest: invalid free changed count\n");
        return 0;
    }

    one = PageAlloc();
    PageFree(one);
    one = PageAlloc();
    if (one == RoboInvalidPhysAddr) {
        kprintf("PMM selftest: realloc failed\n");
        return 0;
    }
    PageFree(one);

    after = FreePageCount();
    nOk = 0;
    for (;;) {
        PhysAddr p = PageAlloc();
        if (p == RoboInvalidPhysAddr)
            break;
        if (!PageAligned(p) || overlaps(p, bitmap, bsize)) {
            kprintf("PMM selftest: exhaust returned reserved 0x%llx\n",
                    (unsigned long long)p);
            RestoreDynamic();
            return 0;
        }
        nOk++;
    }
    if (FreePageCount() != 0 || nOk != after) {
        kprintf("PMM selftest: exhaust mismatch allocated %llu free %llu expect %llu\n",
                (unsigned long long)nOk,
                (unsigned long long)FreePageCount(),
                (unsigned long long)after);
        RestoreDynamic();
        return 0;
    }
    RestoreDynamic();
    if (FreePageCount() != after) {
        kprintf("PMM selftest: restore free %llu expect %llu\n",
                (unsigned long long)FreePageCount(),
                (unsigned long long)after);
        return 0;
    }

    kprintf("PMM selftest passed\n");
    return 1;
}
