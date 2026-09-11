#include "mm/pmm.h"

#ifdef PMM_HOST_TEST
#include <stdio.h>
#define kprintf printf
#else
#include "kio.h"
#endif

static int unique_pages(const phys_addr_t *pages, size_t n)
{
    size_t i, j;
    for (i = 0; i < n; i++) {
        if (pages[i] == ROBO_INVALID_PHYS_ADDR || !robo_page_aligned(pages[i]))
            return 0;
        for (j = i + 1; j < n; j++) {
            if (pages[i] == pages[j])
                return 0;
        }
    }
    return 1;
}

static int overlaps(phys_addr_t page, phys_addr_t base, uint64_t length)
{
    if (!length)
        return 0;
    return robo_phys_ranges_overlap(page, ROBO_PAGE_SIZE, base, length);
}

static void restore_dynamic(void)
{
    size_t i;
    size_t n = robo_page_count();
    for (i = 0; i < n; i++) {
        phys_addr_t p = robo_page_to_phys(i);
        if (robo_page_is_reserved(p))
            continue;
        if (!robo_page_is_free(p))
            robo_page_free(p);
    }
}

int robo_pmm_selftest(void)
{
    phys_addr_t one, batch[100];
    phys_addr_t probe[64];
    size_t initial_free, after, i, n_ok;
    phys_addr_t bitmap, past_end, unaligned;
    uint64_t bsize;

    if (!robo_pmm_ready()) {
        kprintf("PMM selftest: not ready\n");
        return 0;
    }

    initial_free = robo_free_page_count();
    if (initial_free < 200) {
        kprintf("PMM selftest: too few free pages\n");
        return 0;
    }

    one = robo_page_alloc();
    if (one == ROBO_INVALID_PHYS_ADDR || !robo_page_aligned(one)) {
        kprintf("PMM selftest: alloc-one failed\n");
        return 0;
    }
    if (robo_free_page_count() != initial_free - 1) {
        kprintf("PMM selftest: alloc-one count\n");
        robo_page_free(one);
        return 0;
    }
    if (robo_page_is_free(one) || robo_page_is_reserved(one)) {
        kprintf("PMM selftest: alloc-one flags\n");
        robo_page_free(one);
        return 0;
    }
    robo_page_free(one);
    if (robo_free_page_count() != initial_free) {
        kprintf("PMM selftest: free-one count\n");
        return 0;
    }

    after = robo_free_page_count();
    for (i = 0; i < 100; i++)
        batch[i] = robo_page_alloc();
    if (!unique_pages(batch, 100) || robo_free_page_count() != after - 100) {
        kprintf("PMM selftest: alloc-100 failed\n");
        for (i = 0; i < 100; i++) {
            if (batch[i] != ROBO_INVALID_PHYS_ADDR)
                robo_page_free(batch[i]);
        }
        return 0;
    }
    for (i = 0; i < 100; i++)
        robo_page_free(batch[i]);
    if (robo_free_page_count() != after) {
        kprintf("PMM selftest: free-100 count\n");
        return 0;
    }

    one = robo_page_alloc();
    after = robo_free_page_count();
    robo_page_free(one);
    robo_page_free(one);
    if (robo_free_page_count() != after + 1) {
        kprintf("PMM selftest: double-free corrupted counters\n");
        return 0;
    }
    one = robo_page_alloc();
    if (one == ROBO_INVALID_PHYS_ADDR) {
        kprintf("PMM selftest: realloc after double-free failed\n");
        return 0;
    }
    robo_page_free(one);

    bitmap = robo_pmm_bitmap_address();
    bsize = (uint64_t)robo_pmm_bitmap_bytes();
    n_ok = 0;
    after = robo_free_page_count();
    for (i = 0; i < 64; i++) {
        probe[i] = robo_page_alloc();
        if (probe[i] == ROBO_INVALID_PHYS_ADDR)
            break;
        if (overlaps(probe[i], bitmap, bsize)) {
            kprintf("PMM selftest: allocated bitmap page 0x%llx\n",
                    (unsigned long long)probe[i]);
            restore_dynamic();
            return 0;
        }
        n_ok++;
    }
    if (n_ok == 0) {
        kprintf("PMM selftest: reserved-probe alloc failed\n");
        return 0;
    }
    for (i = 0; i < n_ok; i++)
        robo_page_free(probe[i]);
    if (robo_free_page_count() != after) {
        kprintf("PMM selftest: reserved-probe free count\n");
        return 0;
    }

    after = robo_free_page_count();
    robo_page_free(bitmap);
    if (robo_page_is_free(bitmap) || robo_free_page_count() != after) {
        kprintf("PMM selftest: bitmap became free\n");
        return 0;
    }

    unaligned = bitmap ? bitmap + 1 : 1;
    past_end = robo_pmm_limit();
    robo_page_free(unaligned);
    robo_page_free(past_end);
    robo_page_free(ROBO_INVALID_PHYS_ADDR);
    if (robo_free_page_count() != after) {
        kprintf("PMM selftest: invalid free changed count\n");
        return 0;
    }

    one = robo_page_alloc();
    robo_page_free(one);
    one = robo_page_alloc();
    if (one == ROBO_INVALID_PHYS_ADDR) {
        kprintf("PMM selftest: realloc failed\n");
        return 0;
    }
    robo_page_free(one);

    after = robo_free_page_count();
    n_ok = 0;
    for (;;) {
        phys_addr_t p = robo_page_alloc();
        if (p == ROBO_INVALID_PHYS_ADDR)
            break;
        if (!robo_page_aligned(p) || overlaps(p, bitmap, bsize)) {
            kprintf("PMM selftest: exhaust returned reserved 0x%llx\n",
                    (unsigned long long)p);
            restore_dynamic();
            return 0;
        }
        n_ok++;
    }
    if (robo_free_page_count() != 0 || n_ok != after) {
        kprintf("PMM selftest: exhaust mismatch allocated %llu free %llu expect %llu\n",
                (unsigned long long)n_ok,
                (unsigned long long)robo_free_page_count(),
                (unsigned long long)after);
        restore_dynamic();
        return 0;
    }
    restore_dynamic();
    if (robo_free_page_count() != after) {
        kprintf("PMM selftest: restore free %llu expect %llu\n",
                (unsigned long long)robo_free_page_count(),
                (unsigned long long)after);
        return 0;
    }

    kprintf("PMM selftest passed\n");
    return 1;
}
