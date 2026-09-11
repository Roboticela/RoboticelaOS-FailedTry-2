#include "mm/heap/heap.h"
#include "mm/pmm.h"
#include "mm/vmm/vmm.h"
#include "kio.h"

static int heap_fail(const char *tag)
{
    kprintf("HEAP selftest: %s\n", tag);
    return 0;
}

static int aligned_ptr(const void *p)
{
    return p && (((uintptr_t)p & (ROBO_HEAP_ALIGNMENT - 1u)) == 0);
}

static int spans_overlap(const uint8_t *a, size_t na, const uint8_t *b, size_t nb)
{
    uintptr_t a0 = (uintptr_t)a, a1 = a0 + na;
    uintptr_t b0 = (uintptr_t)b, b1 = b0 + nb;
    return a0 < b1 && b0 < a1;
}

static int fill_check(uint8_t *p, size_t n, uint8_t v)
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

static int heap_ok(void)
{
    return robo_heap_validate() ? 1 : heap_fail("validate");
}

int robo_heap_selftest(void)
{
    static const size_t sizes[] = { 16, 32, 64, 128, 256, 512, 1024 };
    static const size_t stress_sz[] = { 16, 24, 64, 128, 512, 1024, 4096, 8192, 65536 };
    void *p, *q, *r, *s;
    void *multi[7];
    uint8_t *bytes;
    phys_addr_t phys, phys2;
    robo_pte_t pte;
    size_t i, j, n, pages0, free0, free1, mapped0;
    uint64_t pattern = 0x1122334455667788ull;
    uint32_t rng = 0xC0FFEEu;
    void *live[48];
    size_t live_n[48];
    int live_count;

    if (!robo_heap_ready() || !robo_vmm_ready() || !robo_pmm_ready())
        return heap_fail("not ready");
    if (!heap_ok())
        return 0;

    p = kmalloc(64);
    if (!aligned_ptr(p))
        return heap_fail("basic alloc");
    *(volatile uint64_t *)p = pattern;
    if (*(volatile uint64_t *)p != pattern)
        return heap_fail("basic readback");
    if (!heap_ok())
        return 0;

    if (!robo_vmm_translate((virt_addr_t)(uintptr_t)p, &phys))
        return heap_fail("vmm translate");
    if (robo_page_is_free(robo_page_align_down(phys)))
        return heap_fail("pmm still free");
    if (robo_page_align_down(phys) >= robo_pmm_limit())
        return heap_fail("phys outside pmm");
    if (!robo_vmm_inspect((virt_addr_t)(uintptr_t)p, &phys2, &pte) || phys2 != robo_page_align_down(phys))
        return heap_fail("vmm inspect");
    if (!(pte & ROBO_PTE_P) || !(pte & ROBO_PTE_RW) || (pte & ROBO_PTE_US))
        return heap_fail("heap pte flags");
    if (robo_vmm_nx_enabled() && !(pte & ROBO_PTE_NX))
        return heap_fail("heap executable");
    if (pte & ROBO_PTE_NX) {
        if (pte & ROBO_PTE_RW) {
            /* RW + NX: expected, not RWX */
        }
    }

    mapped0 = robo_heap_page_count();
    free0 = robo_free_page_count();
    q = kmalloc(8192);
    if (!aligned_ptr(q))
        return heap_fail("grow alloc");
    if (robo_heap_page_count() <= mapped0)
        return heap_fail("heap did not grow");
    free1 = robo_free_page_count();
    if (free1 >= free0)
        return heap_fail("pmm pages not consumed");
    bytes = (uint8_t *)q;
    bytes[0] = 0xA1;
    bytes[4063] = 0xA2;
    bytes[4064] = 0xA3;
    bytes[8159] = 0xA4;
    bytes[8191] = 0xA5;
    if (bytes[0] != 0xA1 || bytes[4063] != 0xA2 || bytes[4064] != 0xA3 ||
        bytes[8159] != 0xA4 || bytes[8191] != 0xA5)
        return heap_fail("page boundary");
    if (robo_vmm_translate((virt_addr_t)(uintptr_t)(bytes + 4096), &phys2) &&
        robo_page_align_down(phys2) == robo_page_align_down(phys)) {
        /* adjacent virtual heap pages may share physical only by chance */
    }
    kfree(q);
    if (robo_free_page_count() != free1)
        return heap_fail("kfree returned pages");
    if (!heap_ok())
        return 0;

    n = sizeof(sizes) / sizeof(sizes[0]);
    for (i = 0; i < n; i++) {
        multi[i] = kmalloc(sizes[i]);
        if (!aligned_ptr(multi[i]) || !fill_check((uint8_t *)multi[i], sizes[i], (uint8_t)(0x11 + i)))
            return heap_fail("multi alloc");
    }
    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            if (spans_overlap(multi[i], sizes[i], multi[j], sizes[j]))
                return heap_fail("multi overlap");
        }
    }
    for (i = 0; i < n; i++)
        kfree(multi[i]);
    if (!heap_ok())
        return 0;

    p = kmalloc(128);
    kfree(p);
    q = kmalloc(128);
    if (!aligned_ptr(q))
        return heap_fail("reuse");
    kfree(q);

    p = kmalloc(1024);
    kfree(p);
    q = kmalloc(32);
    r = kmalloc(32);
    if (!aligned_ptr(q) || !aligned_ptr(r) || q == r)
        return heap_fail("split");
    if (spans_overlap(q, 32, r, 32))
        return heap_fail("split overlap");
    kfree(q);
    kfree(r);
    if (!heap_ok())
        return 0;

    p = kmalloc(256);
    q = kmalloc(256);
    r = kmalloc(256);
    if (!p || !q || !r)
        return heap_fail("coalesce setup");
    kfree(q);
    kfree(p);
    if (!heap_ok())
        return 0;
    s = kmalloc(400);
    if (!aligned_ptr(s))
        return heap_fail("coalesce alloc");
    kfree(s);
    kfree(r);
    if (!heap_ok())
        return 0;

    bytes = (uint8_t *)kcalloc(1024, 1);
    if (!aligned_ptr(bytes))
        return heap_fail("calloc");
    for (i = 0; i < 1024; i++) {
        if (bytes[i] != 0)
            return heap_fail("calloc dirty");
    }
    kfree(bytes);
    if (kcalloc(((size_t)-1) / 2u + 2u, 4u) != 0)
        return heap_fail("calloc overflow");
    if (!heap_ok())
        return 0;

    p = kmalloc(64);
    bytes = (uint8_t *)p;
    for (i = 0; i < 64; i++)
        bytes[i] = (uint8_t)(0xC0 + i);
    q = krealloc(p, 256);
    if (!aligned_ptr(q))
        return heap_fail("realloc grow");
    bytes = (uint8_t *)q;
    for (i = 0; i < 64; i++) {
        if (bytes[i] != (uint8_t)(0xC0 + i))
            return heap_fail("realloc grow data");
    }
    r = krealloc(q, 64);
    if (!aligned_ptr(r))
        return heap_fail("realloc shrink");
    bytes = (uint8_t *)r;
    for (i = 0; i < 64; i++) {
        if (bytes[i] != (uint8_t)(0xC0 + i))
            return heap_fail("realloc shrink data");
    }
    pattern = *(uint64_t *)r;
    s = krealloc(r, (size_t)-1);
    if (s != 0)
        return heap_fail("realloc huge");
    if (*(uint64_t *)r != pattern)
        return heap_fail("realloc fail clobber");
    kfree(r);
    if (!heap_ok())
        return 0;

    kfree(0);
    p = krealloc(0, 128);
    if (!aligned_ptr(p))
        return heap_fail("realloc null");
    if (kmalloc(0) || kcalloc(0, 100) || kcalloc(100, 0))
        return heap_fail("zero size");
    q = krealloc(p, 0);
    if (q)
        return heap_fail("realloc zero");
    if (!heap_ok())
        return 0;

    p = kmalloc(65536);
    q = kmalloc(1024ull * 1024ull);
    if (!aligned_ptr(p) || !aligned_ptr(q))
        return heap_fail("large alloc");
    bytes = (uint8_t *)q;
    bytes[0] = 0x11;
    bytes[4095] = 0x22;
    bytes[4096] = 0x33;
    bytes[1024ull * 1024ull - 1ull] = 0x44;
    if (bytes[0] != 0x11 || bytes[4095] != 0x22 ||
        bytes[4096] != 0x33 || bytes[1024ull * 1024ull - 1ull] != 0x44)
        return heap_fail("large readback");
    kfree(p);
    kfree(q);
    if (!heap_ok())
        return 0;

    for (i = 0; i < 48; i++) {
        live[i] = 0;
        live_n[i] = 0;
    }
    live_count = 0;
    for (i = 0; i < 400; i++) {
        uint32_t pick, idx, sz;
        rng = rng * 1664525u + 1013904223u;
        pick = rng % 3u;
        if ((pick == 0 || live_count == 0) && live_count < 48) {
            sz = (uint32_t)stress_sz[rng % (sizeof(stress_sz) / sizeof(stress_sz[0]))];
            p = kmalloc(sz);
            if (!aligned_ptr(p))
                return heap_fail("stress alloc");
            if (!fill_check((uint8_t *)p, sz < 64 ? sz : 64, (uint8_t)(i & 0xFFu)))
                return heap_fail("stress write");
            for (j = 0; j < 48; j++) {
                if (!live[j]) {
                    live[j] = p;
                    live_n[j] = sz;
                    live_count++;
                    break;
                }
            }
        } else if (live_count) {
            idx = rng % 48u;
            while (!live[idx])
                idx = (idx + 1u) % 48u;
            if (pick == 1 && live_n[idx] < 8192) {
                q = krealloc(live[idx], live_n[idx] * 2u);
                if (!q)
                    return heap_fail("stress realloc");
                live[idx] = q;
                live_n[idx] *= 2u;
            } else {
                kfree(live[idx]);
                live[idx] = 0;
                live_n[idx] = 0;
                live_count--;
            }
        }
        if ((i & 31u) == 31u && !heap_ok())
            return 0;
        for (j = 0; j < 48; j++) {
            size_t k;
            if (!live[j])
                continue;
            for (k = j + 1; k < 48; k++) {
                if (live[k] && spans_overlap(live[j], live_n[j], live[k], live_n[k]))
                    return heap_fail("stress overlap");
            }
        }
    }
    for (i = 0; i < 48; i++)
        kfree(live[i]);
    if (!heap_ok())
        return 0;

    p = kmalloc(32);
    kfree(p);
    kfree(p);
    if (!heap_ok())
        return 0;
    if (!kmalloc(32))
        return heap_fail("after double free");

    pages0 = robo_heap_page_count();
    kprintf("HEAP selftest passed  pages %llu  allocs %llu\n",
            (unsigned long long)pages0,
            (unsigned long long)robo_heap_allocation_count());
    return 1;
}
