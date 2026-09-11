#include "mm/pmm.h"

#ifdef PMM_HOST_TEST
#include <stdio.h>
#define kprintf printf
#else
#include "kio.h"
extern char __kernel_start[];
extern char __kernel_end[];
extern char kstack[];
extern char kstack_end[];
#endif

typedef struct {
    phys_addr_t base;
    uint64_t length;
} pmm_span_t;

static struct {
    int ready;
    int debug;
    uint8_t *occupancy;
    uint8_t *sticky;
    phys_addr_t bitmap_phys;
    size_t bitmap_bytes;
    size_t storage_bytes;
    size_t page_count;
    size_t free_pages;
    size_t alloc_hint;
    phys_addr_t limit;
    phys_addr_t kernel_base;
    uint64_t kernel_size;
    phys_addr_t fb_base;
    uint64_t fb_size;
    const RoboBootInfo *info;
} g_pmm;

#ifdef PMM_HOST_TEST
static uint8_t *g_host_virt;
static phys_addr_t g_host_phys;
static uint64_t g_host_len;

void robo_pmm_host_bind(void *virt, phys_addr_t phys, uint64_t len)
{
    g_host_virt = (uint8_t *)virt;
    g_host_phys = phys;
    g_host_len = len;
}

static void *pmm_phys_ptr(phys_addr_t p)
{
    uint64_t off;
    if (!g_host_virt || p < g_host_phys)
        return 0;
    off = (uint64_t)(p - g_host_phys);
    if (off >= g_host_len)
        return 0;
    return g_host_virt + (size_t)off;
}
#else
static void *pmm_phys_ptr(phys_addr_t p)
{
    return (void *)(uintptr_t)p;
}
#endif

static void pmm_fill(void *dst, uint8_t v, size_t n)
{
    uint8_t *p = (uint8_t *)dst;
    if (!p)
        return;
    while (n--)
        *p++ = v;
}

static int bit_get(const uint8_t *b, size_t i)
{
    return (b[i >> 3] >> (i & 7u)) & 1;
}

static void bit_set(uint8_t *b, size_t i)
{
    b[i >> 3] = (uint8_t)(b[i >> 3] | (uint8_t)(1u << (i & 7u)));
}

static void bit_clear(uint8_t *b, size_t i)
{
    b[i >> 3] = (uint8_t)(b[i >> 3] & (uint8_t)~(1u << (i & 7u)));
}

static void add_span(pmm_span_t *v, size_t *n, size_t max, phys_addr_t base, uint64_t length)
{
    if (!length || *n >= max)
        return;
    v[*n].base = base;
    v[*n].length = length;
    (*n)++;
}

static int span_overlap(phys_addr_t a0, uint64_t a_len, const pmm_span_t *s)
{
    return robo_phys_ranges_overlap(a0, a_len, s->base, s->length);
}

static phys_addr_t find_bitmap_home(const robo_memory_map_t *map,
                                    size_t need,
                                    const pmm_span_t *forbid,
                                    size_t nforbid)
{
    size_t ri;
    phys_addr_t best = ROBO_INVALID_PHYS_ADDR;
    uint64_t need64 = (uint64_t)need;

    for (ri = 0; ri < map->count; ri++) {
        phys_addr_t lo, hi, cand;
        if (map->regions[ri].type != ROBO_MEMORY_USABLE)
            continue;
        if (!robo_memory_usable_pages(map->regions[ri].base, map->regions[ri].length, &lo, &hi))
            continue;
        cand = lo;
        if (cand == 0)
            cand = ROBO_PAGE_SIZE;
        for (;;) {
            size_t fi;
            int hit = 0;
            phys_addr_t end;
            if (cand >= hi)
                break;
            if (!robo_phys_end(cand, need64, &end) || end > hi)
                break;
            for (fi = 0; fi < nforbid; fi++) {
                phys_addr_t fend, next;
                if (!span_overlap(cand, need64, &forbid[fi]))
                    continue;
                if (!robo_phys_end(forbid[fi].base, forbid[fi].length, &fend)) {
                    cand = hi;
                    hit = 1;
                    break;
                }
                next = robo_page_align_up(fend);
                if (next == ROBO_INVALID_PHYS_ADDR || next <= cand)
                    next = cand + ROBO_PAGE_SIZE;
                cand = next;
                hit = 1;
                break;
            }
            if (hit)
                continue;
            if (cand == 0) {
                cand = ROBO_PAGE_SIZE;
                continue;
            }
            if (best == ROBO_INVALID_PHYS_ADDR || cand < best)
                best = cand;
            break;
        }
    }
    return best;
}

static void mark_reserved_page(size_t page)
{
    if (page >= g_pmm.page_count)
        return;
    if (!bit_get(g_pmm.occupancy, page)) {
        bit_set(g_pmm.occupancy, page);
        if (g_pmm.free_pages)
            g_pmm.free_pages--;
    }
    bit_set(g_pmm.sticky, page);
}

static void mark_free_page(size_t page)
{
    if (page >= g_pmm.page_count)
        return;
    if (bit_get(g_pmm.occupancy, page)) {
        bit_clear(g_pmm.occupancy, page);
        g_pmm.free_pages++;
    }
    bit_clear(g_pmm.sticky, page);
}

static void reserve_range(phys_addr_t base, uint64_t length)
{
    phys_addr_t end, lo, hi, addr;
    if (!length || !g_pmm.page_count)
        return;
    if (!robo_phys_end(base, length, &end))
        return;
    lo = robo_page_align_down(base);
    hi = robo_page_aligned(end) ? end : robo_page_align_up(end);
    if (hi == ROBO_INVALID_PHYS_ADDR)
        hi = g_pmm.limit;
    if (lo >= g_pmm.limit)
        return;
    if (hi > g_pmm.limit)
        hi = g_pmm.limit;
    for (addr = lo; addr < hi; addr += ROBO_PAGE_SIZE)
        mark_reserved_page(robo_phys_to_page(addr));
}

static void release_usable(const robo_memory_map_t *map)
{
    size_t i;
    for (i = 0; i < map->count; i++) {
        phys_addr_t lo, hi, addr;
        if (map->regions[i].type != ROBO_MEMORY_USABLE)
            continue;
        if (!robo_memory_usable_pages(map->regions[i].base, map->regions[i].length, &lo, &hi))
            continue;
        if (hi > g_pmm.limit)
            hi = g_pmm.limit;
        for (addr = lo; addr < hi; addr += ROBO_PAGE_SIZE)
            mark_free_page(robo_phys_to_page(addr));
    }
}

static void reserve_non_usable(const robo_memory_map_t *map)
{
    size_t i;
    for (i = 0; i < map->count; i++) {
        if (map->regions[i].type == ROBO_MEMORY_USABLE)
            continue;
        reserve_range(map->regions[i].base, map->regions[i].length);
    }
}

static void dump_map(const robo_memory_map_t *map)
{
    size_t i, n;
    n = map->count;
    if (n > 16)
        n = 16;
    for (i = 0; i < n; i++) {
        kprintf("PMM map[%llu] %s 0x%llx +0x%llx\n",
                (unsigned long long)i,
                robo_memory_type_name(map->regions[i].type),
                (unsigned long long)map->regions[i].base,
                (unsigned long long)map->regions[i].length);
    }
    if (map->count > n)
        kprintf("PMM map  %llu more regions\n", (unsigned long long)(map->count - n));
}

int robo_pmm_ready(void)
{
    return g_pmm.ready;
}

phys_addr_t robo_pmm_bitmap_address(void)
{
    return g_pmm.bitmap_phys;
}

size_t robo_pmm_bitmap_bytes(void)
{
    return g_pmm.storage_bytes;
}

phys_addr_t robo_pmm_limit(void)
{
    return g_pmm.limit;
}

size_t robo_page_count(void)
{
    return g_pmm.page_count;
}

size_t robo_free_page_count(void)
{
    return g_pmm.free_pages;
}

size_t robo_used_page_count(void)
{
    if (g_pmm.page_count < g_pmm.free_pages)
        return 0;
    return g_pmm.page_count - g_pmm.free_pages;
}

bool robo_page_is_free(phys_addr_t page)
{
    size_t idx;
    if (!g_pmm.ready || !robo_page_aligned(page) || page >= g_pmm.limit)
        return false;
    idx = robo_phys_to_page(page);
    if (idx >= g_pmm.page_count)
        return false;
    return !bit_get(g_pmm.occupancy, idx);
}

bool robo_page_is_reserved(phys_addr_t page)
{
    size_t idx;
    if (!g_pmm.ready || !robo_page_aligned(page) || page >= g_pmm.limit)
        return true;
    idx = robo_phys_to_page(page);
    if (idx >= g_pmm.page_count)
        return true;
    return bit_get(g_pmm.sticky, idx) != 0;
}

phys_addr_t robo_page_alloc(void)
{
    size_t i, start;
    if (!g_pmm.ready || g_pmm.free_pages == 0)
        return ROBO_INVALID_PHYS_ADDR;
    start = g_pmm.alloc_hint;
    for (i = start; i < g_pmm.page_count; i++) {
        if (!bit_get(g_pmm.occupancy, i)) {
            bit_set(g_pmm.occupancy, i);
            g_pmm.free_pages--;
            g_pmm.alloc_hint = i + 1;
            return robo_page_to_phys(i);
        }
    }
    for (i = 0; i < start; i++) {
        if (!bit_get(g_pmm.occupancy, i)) {
            bit_set(g_pmm.occupancy, i);
            g_pmm.free_pages--;
            g_pmm.alloc_hint = i + 1;
            return robo_page_to_phys(i);
        }
    }
    return ROBO_INVALID_PHYS_ADDR;
}

void robo_page_free(phys_addr_t page)
{
    size_t idx;
    if (!g_pmm.ready) {
        kprintf("PMM: free before init: 0x%llx\n", (unsigned long long)page);
        return;
    }
    if (!robo_page_aligned(page)) {
        kprintf("PMM: unaligned free: 0x%llx\n", (unsigned long long)page);
        return;
    }
    if (page >= g_pmm.limit) {
        kprintf("PMM: out-of-range free: 0x%llx\n", (unsigned long long)page);
        return;
    }
    idx = robo_phys_to_page(page);
    if (idx >= g_pmm.page_count) {
        kprintf("PMM: out-of-range free: 0x%llx\n", (unsigned long long)page);
        return;
    }
    if (bit_get(g_pmm.sticky, idx)) {
        kprintf("PMM: refuse reserved free: 0x%llx\n", (unsigned long long)page);
        return;
    }
    if (!bit_get(g_pmm.occupancy, idx)) {
        kprintf("PMM: double free detected: 0x%llx\n", (unsigned long long)page);
        return;
    }
    bit_clear(g_pmm.occupancy, idx);
    g_pmm.free_pages++;
    if (idx < g_pmm.alloc_hint)
        g_pmm.alloc_hint = idx;
}

void robo_pmm_report(void)
{
    unsigned long long managed, reserved;
    if (!g_pmm.ready) {
        kprintf("PMM: not initialized\n");
        return;
    }
    managed = (unsigned long long)g_pmm.page_count * ROBO_PAGE_SIZE;
    reserved = (unsigned long long)robo_used_page_count();
    kprintf("Roboticela PMM\n");
    kprintf("--------------\n");
    kprintf("Page size:       %llu bytes\n", (unsigned long long)ROBO_PAGE_SIZE);
    kprintf("Physical pages:  %llu\n", (unsigned long long)g_pmm.page_count);
    kprintf("Managed memory:  %llu MiB\n", managed / (1024ull * 1024ull));
    kprintf("Free pages:      %llu\n", (unsigned long long)g_pmm.free_pages);
    kprintf("Reserved pages:  %llu\n", reserved);
    kprintf("Bitmap:          %llu bytes\n", (unsigned long long)g_pmm.storage_bytes);
    kprintf("Bitmap address:  0x%llx\n", (unsigned long long)g_pmm.bitmap_phys);
}

#ifndef PMM_HOST_TEST
void robo_pmm_fb_report(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    if (!g_pmm.ready)
        return;
    kfb_puts("PMM  pages ", mute);
    kfb_putu((unsigned long long)g_pmm.page_count, white);
    kfb_puts("  free ", mute);
    kfb_putu((unsigned long long)g_pmm.free_pages, white);
    kfb_puts("\nPMM  bitmap ", mute);
    kfb_puthex((unsigned long long)g_pmm.bitmap_phys, white);
    kfb_puts("  ", mute);
    kfb_putu((unsigned long long)g_pmm.storage_bytes, white);
    kfb_puts(" bytes\n", mute);
}
#endif

bool robo_pmm_init(const RoboBootInfo *info)
{
    robo_memory_map_t map;
    pmm_span_t forbid[8];
    size_t nforbid = 0;
    phys_addr_t limit = 0;
    phys_addr_t home;
    size_t bitmap_bytes, storage, storage_pages;
    phys_addr_t bootinfo_phys;
    uint64_t bootinfo_size;
    uint64_t fb_size;

    pmm_fill(&g_pmm, 0, sizeof(g_pmm));

    kprintf("PMM init\n");
    if (!info || info->magic != ROBO_BOOTINFO_MAGIC) {
        kprintf("PMM: missing bootinfo\n");
        return false;
    }
    if (info->version < 1) {
        kprintf("PMM: unsupported bootinfo version %u\n", (unsigned)info->version);
        return false;
    }
    g_pmm.info = info;
    g_pmm.debug = (info->flags & ROBO_BOOT_DEBUG) != 0;

    if (!robo_memory_map_load(&map, info)) {
        kprintf("PMM: empty or invalid memory map\n");
        return false;
    }
    if (!robo_memory_map_usable_limit(&map, &limit) || limit < ROBO_PAGE_SIZE) {
        kprintf("PMM: no usable physical pages\n");
        return false;
    }
    if (g_pmm.debug)
        dump_map(&map);

    g_pmm.limit = limit;
    g_pmm.page_count = (size_t)(limit >> ROBO_PAGE_SHIFT);
    if (!g_pmm.page_count || robo_page_to_phys(g_pmm.page_count) != limit) {
        kprintf("PMM: page count overflow\n");
        pmm_fill(&g_pmm, 0, sizeof(g_pmm));
        return false;
    }

    bitmap_bytes = robo_bitmap_bytes(g_pmm.page_count);
    if (!bitmap_bytes || bitmap_bytes > ((size_t)-1) / 2u) {
        kprintf("PMM: bitmap size overflow\n");
        pmm_fill(&g_pmm, 0, sizeof(g_pmm));
        return false;
    }
    storage = bitmap_bytes * 2u;
    {
        phys_addr_t aligned = robo_page_align_up((phys_addr_t)storage);
        if (aligned == ROBO_INVALID_PHYS_ADDR || aligned < (phys_addr_t)storage) {
            kprintf("PMM: bitmap storage overflow\n");
            pmm_fill(&g_pmm, 0, sizeof(g_pmm));
            return false;
        }
        storage = (size_t)aligned;
        storage_pages = (size_t)(aligned / ROBO_PAGE_SIZE);
    }
    if (!storage_pages) {
        kprintf("PMM: bitmap storage empty\n");
        pmm_fill(&g_pmm, 0, sizeof(g_pmm));
        return false;
    }

    g_pmm.kernel_base = info->kernel_base;
    g_pmm.kernel_size = info->kernel_size;
#ifndef PMM_HOST_TEST
    {
        phys_addr_t ks = (phys_addr_t)(uintptr_t)__kernel_start;
        phys_addr_t ke = (phys_addr_t)(uintptr_t)__kernel_end;
        phys_addr_t ss = (phys_addr_t)(uintptr_t)kstack;
        phys_addr_t se = (phys_addr_t)(uintptr_t)kstack_end;
        if (ke > ks) {
            uint64_t klen = (uint64_t)(ke - ks);
            if (!g_pmm.kernel_size) {
                g_pmm.kernel_base = ks;
                g_pmm.kernel_size = klen;
            } else {
                phys_addr_t a0 = g_pmm.kernel_base < ks ? g_pmm.kernel_base : ks;
                phys_addr_t a1 = g_pmm.kernel_base + g_pmm.kernel_size;
                if (ke > a1)
                    a1 = ke;
                g_pmm.kernel_base = a0;
                g_pmm.kernel_size = (uint64_t)(a1 - a0);
            }
        }
        if (se > ss)
            add_span(forbid, &nforbid, 8, ss, (uint64_t)(se - ss));
    }
#endif
    add_span(forbid, &nforbid, 8, g_pmm.kernel_base, g_pmm.kernel_size);

    bootinfo_phys = (info->version >= 2 && info->bootinfo_address)
                        ? info->bootinfo_address
                        : (phys_addr_t)(uintptr_t)info;
    bootinfo_size = (info->version >= 2 && info->bootinfo_size)
                        ? info->bootinfo_size
                        : 8192ull;
    add_span(forbid, &nforbid, 8, bootinfo_phys, bootinfo_size);
    add_span(forbid, &nforbid, 8, info->initrd_address, info->initrd_size);

    fb_size = robo_bootinfo_framebuffer_size(info);
    g_pmm.fb_base = info->framebuffer_address;
    g_pmm.fb_size = fb_size;
    add_span(forbid, &nforbid, 8, g_pmm.fb_base, g_pmm.fb_size);

    home = find_bitmap_home(&map, storage, forbid, nforbid);
    if (home == ROBO_INVALID_PHYS_ADDR) {
        kprintf("PMM: no room for bitmap\n");
        pmm_fill(&g_pmm, 0, sizeof(g_pmm));
        return false;
    }

    g_pmm.bitmap_phys = home;
    g_pmm.bitmap_bytes = bitmap_bytes;
    g_pmm.storage_bytes = storage;
    g_pmm.occupancy = (uint8_t *)pmm_phys_ptr(home);
    if (!g_pmm.occupancy) {
        kprintf("PMM: bitmap is not addressable\n");
        pmm_fill(&g_pmm, 0, sizeof(g_pmm));
        return false;
    }
    g_pmm.sticky = g_pmm.occupancy + bitmap_bytes;
    pmm_fill(g_pmm.occupancy, 0xFFu, storage);
    g_pmm.free_pages = 0;
    g_pmm.alloc_hint = 0;

    kprintf("PMM  usable limit 0x%llx  pages %llu\n",
            (unsigned long long)limit, (unsigned long long)g_pmm.page_count);
    kprintf("PMM  bitmap 0x%llx  %llu bytes\n",
            (unsigned long long)home, (unsigned long long)storage);

    release_usable(&map);
    reserve_non_usable(&map);
    /* Physical page 0 is never a safe identity-map pointer. */
    reserve_range(0, ROBO_PAGE_SIZE);
    reserve_range(g_pmm.kernel_base, g_pmm.kernel_size);
    reserve_range(bootinfo_phys, bootinfo_size);
    reserve_range(info->initrd_address, info->initrd_size);
    reserve_range(g_pmm.fb_base, g_pmm.fb_size);
    reserve_range(home, (uint64_t)storage);
#ifndef PMM_HOST_TEST
    reserve_range((phys_addr_t)(uintptr_t)kstack,
                  (uint64_t)((uintptr_t)kstack_end - (uintptr_t)kstack));
#endif

    if (g_pmm.free_pages == 0) {
        kprintf("PMM: no free pages after reserve\n");
        pmm_fill(&g_pmm, 0, sizeof(g_pmm));
        return false;
    }

    g_pmm.ready = 1;
    kprintf("PMM  kernel 0x%llx +%llu  bootinfo 0x%llx +%llu\n",
            (unsigned long long)g_pmm.kernel_base,
            (unsigned long long)g_pmm.kernel_size,
            (unsigned long long)bootinfo_phys,
            (unsigned long long)bootinfo_size);
    kprintf("PMM  free %llu  reserved %llu\n",
            (unsigned long long)g_pmm.free_pages,
            (unsigned long long)robo_used_page_count());
    return true;
}
