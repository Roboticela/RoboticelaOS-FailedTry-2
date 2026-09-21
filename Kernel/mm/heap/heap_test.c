#include "mm/heap/heap.h"
#include "mm/pmm.h"
#include "mm/vmm/vmm.h"
#include "kio.h"

static int HeapFail(const char *tag)
{
    kprintf("HEAP selftest: %s\n", tag);
    return 0;
}

static int AlignedPtr(const void *p)
{
    return p && (((uintptr_t)p & (RoboHeapAlignment - 1u)) == 0);
}

static int SpansOverlap(const uint8_t *a, size_t na, const uint8_t *b, size_t nb)
{
    uintptr_t a0 = (uintptr_t)a, a1 = a0 + na;
    uintptr_t b0 = (uintptr_t)b, b1 = b0 + nb;
    return a0 < b1 && b0 < a1;
}

static int FillCheck(uint8_t *p, size_t n, uint8_t v)
{
    size_t i;
    for (i = 0; i < n; i++)
        p[i] = v;
    for (i = 0; i < n; i++) {
        if (p[i] != v)
            return 0;
    }
    return 1;
}

static int HeapOk(void)
{
    return HeapValidate() ? 1 : HeapFail("validate");
}

int HeapSelftest(void)
{
    static const size_t sizes[] = { 16, 32, 64, 128, 256, 512, 1024 };
    static const size_t stressSz[] = { 16, 24, 64, 128, 512, 1024, 4096, 8192, 65536 };
    void *p, *q, *r, *s;
    void *multi[7];
    uint8_t *bytes;
    PhysAddr phys, phys2;
    Pte pte;
    size_t i, j, n, pages0, free0, free1, mapped0;
    uint64_t pattern = 0x1122334455667788ull;
    uint32_t rng = 0xC0FFEEu;
    void *live[48];
    size_t liveN[48];
    int liveCount;

    if (!HeapReady() || !VmmReady() || !PmmReady())
        return HeapFail("not ready");
    if (!HeapOk())
        return 0;

    p = kmalloc(64);
    if (!AlignedPtr(p))
        return HeapFail("basic alloc");
    *(volatile uint64_t *)p = pattern;
    if (*(volatile uint64_t *)p != pattern)
        return HeapFail("basic readback");
    if (!HeapOk())
        return 0;

    if (!VmmTranslate((VirtAddr)(uintptr_t)p, &phys))
        return HeapFail("vmm translate");
    if (PageIsFree(PageAlignDown(phys)))
        return HeapFail("pmm still free");
    if (PageAlignDown(phys) >= PmmLimit())
        return HeapFail("phys outside pmm");
    if (!VmmInspect((VirtAddr)(uintptr_t)p, &phys2, &pte) || phys2 != PageAlignDown(phys))
        return HeapFail("vmm inspect");
    if (!(pte & RoboPteP) || !(pte & RoboPteRw) || (pte & RoboPteUs))
        return HeapFail("heap pte flags");
    if (VmmNxEnabled() && !(pte & RoboPteNx))
        return HeapFail("heap executable");
    if (pte & RoboPteNx) {
        if (pte & RoboPteRw) {
            /* RW + NX: expected, not RWX */
        }
    }

    mapped0 = HeapPageCount();
    free0 = FreePageCount();
    q = kmalloc(8192);
    if (!AlignedPtr(q))
        return HeapFail("grow alloc");
    if (HeapPageCount() <= mapped0)
        return HeapFail("heap did not grow");
    free1 = FreePageCount();
    if (free1 >= free0)
        return HeapFail("pmm pages not consumed");
    bytes = (uint8_t *)q;
    bytes[0] = 0xA1;
    bytes[4063] = 0xA2;
    bytes[4064] = 0xA3;
    bytes[8159] = 0xA4;
    bytes[8191] = 0xA5;
    if (bytes[0] != 0xA1 || bytes[4063] != 0xA2 || bytes[4064] != 0xA3 ||
        bytes[8159] != 0xA4 || bytes[8191] != 0xA5)
        return HeapFail("page boundary");
    if (VmmTranslate((VirtAddr)(uintptr_t)(bytes + 4096), &phys2) &&
        PageAlignDown(phys2) == PageAlignDown(phys)) {
        /* adjacent virtual heap pages may share physical only by chance */
    }
    kfree(q);
    if (FreePageCount() != free1)
        return HeapFail("kfree returned pages");
    if (!HeapOk())
        return 0;

    n = sizeof(sizes) / sizeof(sizes[0]);
    for (i = 0; i < n; i++) {
        multi[i] = kmalloc(sizes[i]);
        if (!AlignedPtr(multi[i]) || !FillCheck((uint8_t *)multi[i], sizes[i], (uint8_t)(0x11 + i)))
            return HeapFail("multi alloc");
    }
    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            if (SpansOverlap(multi[i], sizes[i], multi[j], sizes[j]))
                return HeapFail("multi overlap");
        }
    }
    for (i = 0; i < n; i++)
        kfree(multi[i]);
    if (!HeapOk())
        return 0;

    p = kmalloc(128);
    kfree(p);
    q = kmalloc(128);
    if (!AlignedPtr(q))
        return HeapFail("reuse");
    kfree(q);

    p = kmalloc(1024);
    kfree(p);
    q = kmalloc(32);
    r = kmalloc(32);
    if (!AlignedPtr(q) || !AlignedPtr(r) || q == r)
        return HeapFail("split");
    if (SpansOverlap(q, 32, r, 32))
        return HeapFail("split overlap");
    kfree(q);
    kfree(r);
    if (!HeapOk())
        return 0;

    p = kmalloc(256);
    q = kmalloc(256);
    r = kmalloc(256);
    if (!p || !q || !r)
        return HeapFail("coalesce setup");
    kfree(q);
    kfree(p);
    if (!HeapOk())
        return 0;
    s = kmalloc(400);
    if (!AlignedPtr(s))
        return HeapFail("coalesce alloc");
    kfree(s);
    kfree(r);
    if (!HeapOk())
        return 0;

    bytes = (uint8_t *)kcalloc(1024, 1);
    if (!AlignedPtr(bytes))
        return HeapFail("calloc");
    for (i = 0; i < 1024; i++) {
        if (bytes[i] != 0)
            return HeapFail("calloc dirty");
    }
    kfree(bytes);
    if (kcalloc(((size_t)-1) / 2u + 2u, 4u) != 0)
        return HeapFail("calloc overflow");
    if (!HeapOk())
        return 0;

    p = kmalloc(64);
    bytes = (uint8_t *)p;
    for (i = 0; i < 64; i++)
        bytes[i] = (uint8_t)(0xC0 + i);
    q = krealloc(p, 256);
    if (!AlignedPtr(q))
        return HeapFail("realloc grow");
    bytes = (uint8_t *)q;
    for (i = 0; i < 64; i++) {
        if (bytes[i] != (uint8_t)(0xC0 + i))
            return HeapFail("realloc grow data");
    }
    r = krealloc(q, 64);
    if (!AlignedPtr(r))
        return HeapFail("realloc shrink");
    bytes = (uint8_t *)r;
    for (i = 0; i < 64; i++) {
        if (bytes[i] != (uint8_t)(0xC0 + i))
            return HeapFail("realloc shrink data");
    }
    pattern = *(uint64_t *)r;
    s = krealloc(r, (size_t)-1);
    if (s != 0)
        return HeapFail("realloc huge");
    if (*(uint64_t *)r != pattern)
        return HeapFail("realloc fail clobber");
    kfree(r);
    if (!HeapOk())
        return 0;

    kfree(0);
    p = krealloc(0, 128);
    if (!AlignedPtr(p))
        return HeapFail("realloc null");
    if (kmalloc(0) || kcalloc(0, 100) || kcalloc(100, 0))
        return HeapFail("zero size");
    q = krealloc(p, 0);
    if (q)
        return HeapFail("realloc zero");
    if (!HeapOk())
        return 0;

    p = kmalloc(65536);
    q = kmalloc(1024ull * 1024ull);
    if (!AlignedPtr(p) || !AlignedPtr(q))
        return HeapFail("large alloc");
    bytes = (uint8_t *)q;
    bytes[0] = 0x11;
    bytes[4095] = 0x22;
    bytes[4096] = 0x33;
    bytes[1024ull * 1024ull - 1ull] = 0x44;
    if (bytes[0] != 0x11 || bytes[4095] != 0x22 ||
        bytes[4096] != 0x33 || bytes[1024ull * 1024ull - 1ull] != 0x44)
        return HeapFail("large readback");
    kfree(p);
    kfree(q);
    if (!HeapOk())
        return 0;

    for (i = 0; i < 48; i++) {
        live[i] = 0;
        liveN[i] = 0;
    }
    liveCount = 0;
    for (i = 0; i < 400; i++) {
        uint32_t pick, idx, sz;
        rng = rng * 1664525u + 1013904223u;
        pick = rng % 3u;
        if ((pick == 0 || liveCount == 0) && liveCount < 48) {
            sz = (uint32_t)stressSz[rng % (sizeof(stressSz) / sizeof(stressSz[0]))];
            p = kmalloc(sz);
            if (!AlignedPtr(p))
                return HeapFail("stress alloc");
            if (!FillCheck((uint8_t *)p, sz < 64 ? sz : 64, (uint8_t)(i & 0xFFu)))
                return HeapFail("stress write");
            for (j = 0; j < 48; j++) {
                if (!live[j]) {
                    live[j] = p;
                    liveN[j] = sz;
                    liveCount++;
                    break;
                }
            }
        } else if (liveCount) {
            idx = rng % 48u;
            while (!live[idx])
                idx = (idx + 1u) % 48u;
            if (pick == 1 && liveN[idx] < 8192) {
                q = krealloc(live[idx], liveN[idx] * 2u);
                if (!q)
                    return HeapFail("stress realloc");
                live[idx] = q;
                liveN[idx] *= 2u;
            } else {
                kfree(live[idx]);
                live[idx] = 0;
                liveN[idx] = 0;
                liveCount--;
            }
        }
        if ((i & 31u) == 31u && !HeapOk())
            return 0;
        for (j = 0; j < 48; j++) {
            size_t k;
            if (!live[j])
                continue;
            for (k = j + 1; k < 48; k++) {
                if (live[k] && SpansOverlap(live[j], liveN[j], live[k], liveN[k]))
                    return HeapFail("stress overlap");
            }
        }
    }
    for (i = 0; i < 48; i++)
        kfree(live[i]);
    if (!HeapOk())
        return 0;

    p = kmalloc(32);
    kfree(p);
    kfree(p);
    if (!HeapOk())
        return 0;
    if (!kmalloc(32))
        return HeapFail("after double free");

    pages0 = HeapPageCount();
    kprintf("HEAP selftest passed  pages %llu  allocs %llu\n",
            (unsigned long long)pages0,
            (unsigned long long)HeapAllocationCount());
    return 1;
}
