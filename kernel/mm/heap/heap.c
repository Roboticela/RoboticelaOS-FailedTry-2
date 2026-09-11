#include "mm/heap/heap.h"
#include "mm/pmm.h"
#include "mm/vmm/vmm.h"
#include "kio.h"

#if ROBO_HEAP_BASE != ROBO_VMM_HEAP_BASE
#error kernel heap base must match the VMM-reserved heap window
#endif

#define ROBO_HEAP_MAGIC_USED 0x48454131u
#define ROBO_HEAP_MAGIC_FREE 0x48454632u
#define ROBO_HEAP_VM_FLAGS   (ROBO_VM_READ | ROBO_VM_WRITE | ROBO_VM_KERNEL)

typedef struct robo_heap_block {
    size_t size;
    uint32_t magic;
    uint16_t free;
    uint16_t pad;
    struct robo_heap_block *next;
    struct robo_heap_block *prev;
} robo_heap_block_t;

#define ROBO_HEAP_HEADER ((size_t)sizeof(robo_heap_block_t))

_Static_assert(sizeof(robo_heap_block_t) == 32, "heap header must be 32 bytes");

static struct {
    int ready;
    virt_addr_t mapped_end;
    robo_heap_block_t *head;
    robo_heap_block_t *tail;
    size_t pages;
} g_heap;

static void heap_fill(void *dst, uint8_t v, size_t n)
{
    uint8_t *p = (uint8_t *)dst;
    while (n--)
        *p++ = v;
}

static void heap_copy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--)
        *d++ = *s++;
}

static size_t heap_align(size_t n)
{
    size_t a = ROBO_HEAP_ALIGNMENT;
    if (n > ((size_t)-1) - (a - 1u))
        return 0;
    return (n + (a - 1u)) & ~(a - 1u);
}

static int heap_add_ok(size_t a, size_t b, size_t *out)
{
    if (a > ((size_t)-1) - b)
        return 0;
    if (out)
        *out = a + b;
    return 1;
}

static virt_addr_t block_addr(const robo_heap_block_t *b)
{
    return (virt_addr_t)(uintptr_t)b;
}

static virt_addr_t block_end(const robo_heap_block_t *b)
{
    return block_addr(b) + (virt_addr_t)b->size;
}

static void *block_user(robo_heap_block_t *b)
{
    return (void *)(b + 1);
}

static size_t block_user_size(const robo_heap_block_t *b)
{
    if (!b || b->size < ROBO_HEAP_HEADER)
        return 0;
    return b->size - ROBO_HEAP_HEADER;
}

static int block_in_heap(const robo_heap_block_t *b)
{
    virt_addr_t a, e;
    if (!b)
        return 0;
    a = block_addr(b);
    if (a < ROBO_HEAP_BASE || a >= g_heap.mapped_end)
        return 0;
    if (b->size < ROBO_HEAP_HEADER || (b->size & (ROBO_HEAP_ALIGNMENT - 1u)))
        return 0;
    if (!heap_add_ok((size_t)a, b->size, 0))
        return 0;
    e = block_end(b);
    return e > a && e <= g_heap.mapped_end;
}

static size_t block_need(size_t user)
{
    size_t aligned, total;
    aligned = heap_align(user);
    if (!aligned)
        return 0;
    if (!heap_add_ok(ROBO_HEAP_HEADER, aligned, &total))
        return 0;
    return total;
}

static void insert_after(robo_heap_block_t *pos, robo_heap_block_t *n)
{
    n->prev = pos;
    n->next = pos->next;
    if (pos->next)
        pos->next->prev = n;
    else
        g_heap.tail = n;
    pos->next = n;
}

static void unlink_block(robo_heap_block_t *b)
{
    if (b->prev)
        b->prev->next = b->next;
    else
        g_heap.head = b->next;
    if (b->next)
        b->next->prev = b->prev;
    else
        g_heap.tail = b->prev;
    b->next = 0;
    b->prev = 0;
}

static void mark_free(robo_heap_block_t *b)
{
    b->free = 1;
    b->magic = ROBO_HEAP_MAGIC_FREE;
}

static void mark_used(robo_heap_block_t *b)
{
    b->free = 0;
    b->magic = ROBO_HEAP_MAGIC_USED;
}

static void split_block(robo_heap_block_t *b, size_t need)
{
    robo_heap_block_t *rest;
    size_t remain;
    if (!b || b->size < need)
        return;
    remain = b->size - need;
    if (remain < ROBO_HEAP_HEADER + ROBO_HEAP_ALIGNMENT)
        return;
    rest = (robo_heap_block_t *)(uintptr_t)(block_addr(b) + (virt_addr_t)need);
    rest->size = remain;
    rest->pad = 0;
    mark_free(rest);
    b->size = need;
    insert_after(b, rest);
}

static void coalesce_block(robo_heap_block_t *b)
{
    if (!b || !b->free)
        return;
    while (b->next && b->next->free && block_end(b) == block_addr(b->next)) {
        robo_heap_block_t *n = b->next;
        if (!heap_add_ok(b->size, n->size, &b->size))
            return;
        unlink_block(n);
    }
    while (b->prev && b->prev->free && block_end(b->prev) == block_addr(b)) {
        robo_heap_block_t *p = b->prev;
        if (!heap_add_ok(p->size, b->size, &p->size))
            return;
        unlink_block(b);
        b = p;
    }
}

static int heap_grow(size_t extra)
{
    size_t pages, i;
    virt_addr_t start, va, room;
    phys_addr_t pa;
    robo_heap_block_t *n;

    if (!extra)
        return 0;
    extra = (size_t)robo_page_align_up((phys_addr_t)extra);
    if (!extra || extra == (size_t)ROBO_INVALID_PHYS_ADDR)
        return 0;
    if (g_heap.mapped_end > ROBO_HEAP_END)
        return 0;
    room = ROBO_HEAP_END - g_heap.mapped_end;
    if ((virt_addr_t)extra > room)
        return 0;
    pages = extra / (size_t)ROBO_PAGE_SIZE;
    if (!pages)
        return 0;

    start = g_heap.mapped_end;
    va = start;
    for (i = 0; i < pages; i++) {
        pa = robo_page_alloc();
        if (pa == ROBO_INVALID_PHYS_ADDR)
            goto rollback;
        if (!robo_vmm_map(va, pa, ROBO_HEAP_VM_FLAGS)) {
            robo_page_free(pa);
            goto rollback;
        }
        va += ROBO_PAGE_SIZE;
    }

    g_heap.mapped_end = va;
    g_heap.pages += pages;
    kprintf("HEAP  grow %llu pages  0x%llx-0x%llx\n",
            (unsigned long long)pages,
            (unsigned long long)start,
            (unsigned long long)va);

    if (g_heap.tail && g_heap.tail->free && block_end(g_heap.tail) == start) {
        if (!heap_add_ok(g_heap.tail->size, extra, &g_heap.tail->size))
            return 0;
        return 1;
    }

    n = (robo_heap_block_t *)(uintptr_t)start;
    n->size = extra;
    n->pad = 0;
    n->next = 0;
    n->prev = g_heap.tail;
    mark_free(n);
    if (g_heap.tail)
        g_heap.tail->next = n;
    else
        g_heap.head = n;
    g_heap.tail = n;
    return 1;

rollback:
    while (va > start) {
        phys_addr_t got;
        va -= ROBO_PAGE_SIZE;
        if (robo_vmm_unmap(va, &got))
            robo_page_free(got);
    }
    kprintf("HEAP: grow failed\n");
    return 0;
}

static int heap_grow_for(size_t need)
{
    size_t have = 0;
    size_t extra;
    if (g_heap.tail && g_heap.tail->free && block_end(g_heap.tail) == g_heap.mapped_end)
        have = g_heap.tail->size;
    if (have >= need)
        return 1;
    extra = need - have;
    return heap_grow(extra);
}

static void *take_block(robo_heap_block_t *b, size_t need)
{
    split_block(b, need);
    mark_used(b);
    return block_user(b);
}

static void *heap_find(size_t need)
{
    robo_heap_block_t *b;
    for (b = g_heap.head; b; b = b->next) {
        if (!block_in_heap(b))
            return 0;
        if (b->free && b->magic == ROBO_HEAP_MAGIC_FREE && b->size >= need)
            return take_block(b, need);
    }
    return 0;
}

static robo_heap_block_t *block_from_user(void *ptr)
{
    uintptr_t u, hdr;
    robo_heap_block_t *b;
    if (!ptr)
        return 0;
    u = (uintptr_t)ptr;
    if (u < ROBO_HEAP_HEADER)
        return 0;
    if (u & (ROBO_HEAP_ALIGNMENT - 1u))
        return 0;
    hdr = u - ROBO_HEAP_HEADER;
    if (hdr < ROBO_HEAP_BASE || hdr >= g_heap.mapped_end)
        return 0;
    b = (robo_heap_block_t *)hdr;
    if (!block_in_heap(b))
        return 0;
    if ((uintptr_t)block_user(b) != u)
        return 0;
    if (block_end(b) > g_heap.mapped_end)
        return 0;
    return b;
}

bool robo_heap_validate(void)
{
    robo_heap_block_t *b, *last;
    virt_addr_t expect;
    size_t seen = 0;

    if (!g_heap.ready)
        return false;
    if (g_heap.mapped_end < ROBO_HEAP_BASE || g_heap.mapped_end > ROBO_HEAP_END)
        return false;
    if ((g_heap.mapped_end - ROBO_HEAP_BASE) != (virt_addr_t)g_heap.pages * ROBO_PAGE_SIZE)
        return false;
    if (!g_heap.head || !g_heap.tail)
        return false;
    if (block_addr(g_heap.head) != ROBO_HEAP_BASE)
        return false;

    expect = ROBO_HEAP_BASE;
    last = 0;
    for (b = g_heap.head; b; last = b, b = b->next) {
        if (!block_in_heap(b))
            return false;
        if (block_addr(b) != expect)
            return false;
        if (b->prev != last)
            return false;
        if (last && last->next != b)
            return false;
        if (b->free) {
            if (b->magic != ROBO_HEAP_MAGIC_FREE)
                return false;
            if (last && last->free)
                return false;
        } else if (b->magic != ROBO_HEAP_MAGIC_USED) {
            return false;
        }
        expect = block_end(b);
        seen++;
        if (seen > g_heap.pages * (ROBO_PAGE_SIZE / (ROBO_HEAP_HEADER + ROBO_HEAP_ALIGNMENT)))
            return false;
    }
    if (expect != g_heap.mapped_end)
        return false;
    if (last != g_heap.tail)
        return false;
    return true;
}

static void heap_stat_walk(size_t *used, size_t *free_bytes, size_t *allocs)
{
    robo_heap_block_t *b;
    size_t u = 0, f = 0, n = 0;
    for (b = g_heap.head; b; b = b->next) {
        if (b->free)
            f += b->size;
        else {
            u += b->size;
            n++;
        }
    }
    if (used)
        *used = u;
    if (free_bytes)
        *free_bytes = f;
    if (allocs)
        *allocs = n;
}

int robo_heap_ready(void)
{
    return g_heap.ready;
}

size_t robo_heap_total_bytes(void)
{
    return g_heap.ready ? (size_t)(g_heap.mapped_end - ROBO_HEAP_BASE) : 0;
}

size_t robo_heap_used_bytes(void)
{
    size_t u = 0;
    if (g_heap.ready)
        heap_stat_walk(&u, 0, 0);
    return u;
}

size_t robo_heap_free_bytes(void)
{
    size_t f = 0;
    if (g_heap.ready)
        heap_stat_walk(0, &f, 0);
    return f;
}

size_t robo_heap_page_count(void)
{
    return g_heap.pages;
}

size_t robo_heap_allocation_count(void)
{
    size_t n = 0;
    if (g_heap.ready)
        heap_stat_walk(0, 0, &n);
    return n;
}

void *kmalloc(size_t size)
{
    size_t need;
    void *p;
    int tries;

    if (!g_heap.ready || !size)
        return 0;
    need = block_need(size);
    if (!need)
        return 0;
    for (tries = 0; tries < 2; tries++) {
        p = heap_find(need);
        if (p)
            return p;
        if (!heap_grow_for(need))
            return 0;
    }
    return 0;
}

void kfree(void *ptr)
{
    robo_heap_block_t *b;
    if (!ptr)
        return;
    if (!g_heap.ready) {
        kprintf("HEAP: free before init\n");
        return;
    }
    b = block_from_user(ptr);
    if (!b) {
        kprintf("HEAP: invalid free\n");
        return;
    }
    if (b->free || b->magic == ROBO_HEAP_MAGIC_FREE) {
        kprintf("HEAP: double free detected\n");
        return;
    }
    if (b->magic != ROBO_HEAP_MAGIC_USED) {
        kprintf("HEAP: corrupted metadata\n");
        return;
    }
    heap_fill(block_user(b), 0xDDu, block_user_size(b));
    mark_free(b);
    coalesce_block(b);
}

void *kcalloc(size_t count, size_t size)
{
    size_t bytes;
    void *p;
    if (!count || !size)
        return 0;
    if (size > ((size_t)-1) / count)
        return 0;
    bytes = count * size;
    p = kmalloc(bytes);
    if (!p)
        return 0;
    heap_fill(p, 0, bytes);
    return p;
}

void *krealloc(void *ptr, size_t new_size)
{
    robo_heap_block_t *b;
    void *p;
    size_t old, copy;
    if (!ptr)
        return kmalloc(new_size);
    if (!new_size) {
        kfree(ptr);
        return 0;
    }
    b = block_from_user(ptr);
    if (!b || b->free || b->magic != ROBO_HEAP_MAGIC_USED) {
        kprintf("HEAP: realloc of invalid pointer\n");
        return 0;
    }
    old = block_user_size(b);
    if (new_size <= old)
        return ptr;
    p = kmalloc(new_size);
    if (!p)
        return 0;
    copy = old;
    if (copy > new_size)
        copy = new_size;
    heap_copy(p, ptr, copy);
    kfree(ptr);
    return p;
}

void robo_heap_report(void)
{
    size_t used, free_bytes, allocs;
    if (!g_heap.ready) {
        kprintf("HEAP: not initialized\n");
        return;
    }
    heap_stat_walk(&used, &free_bytes, &allocs);
    kprintf("Roboticela Heap\n");
    kprintf("---------------\n");
    kprintf("Strategy:        first-fit free list\n");
    kprintf("Alignment:       %u\n", (unsigned)ROBO_HEAP_ALIGNMENT);
    kprintf("Window:          0x%llx-0x%llx\n",
            (unsigned long long)ROBO_HEAP_BASE,
            (unsigned long long)ROBO_HEAP_END);
    kprintf("Mapped:          0x%llx-0x%llx  %llu pages\n",
            (unsigned long long)ROBO_HEAP_BASE,
            (unsigned long long)g_heap.mapped_end,
            (unsigned long long)g_heap.pages);
    kprintf("Used:            %llu  free %llu  allocs %llu\n",
            (unsigned long long)used,
            (unsigned long long)free_bytes,
            (unsigned long long)allocs);
}

#ifndef HEAP_HOST_TEST
void robo_heap_fb_report(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    if (!g_heap.ready)
        return;
    kfb_puts("HEAP  ", mute);
    kfb_puthex((unsigned long long)ROBO_HEAP_BASE, white);
    kfb_puts("  pages ", mute);
    kfb_putu((unsigned long long)g_heap.pages, white);
    kfb_puts("  allocs ", mute);
    kfb_putu((unsigned long long)robo_heap_allocation_count(), white);
    kfb_puts("\n", mute);
}
#endif

bool robo_heap_init(void)
{
    heap_fill(&g_heap, 0, sizeof(g_heap));
    kprintf("HEAP init\n");
    if (!robo_pmm_ready() || !robo_vmm_ready()) {
        kprintf("HEAP: PMM/VMM not ready\n");
        return false;
    }
    g_heap.mapped_end = ROBO_HEAP_BASE;
    if (!heap_grow((size_t)ROBO_PAGE_SIZE)) {
        kprintf("HEAP: initial map failed\n");
        heap_fill(&g_heap, 0, sizeof(g_heap));
        return false;
    }
    g_heap.ready = 1;
    if (!robo_heap_validate()) {
        kprintf("HEAP: initial validate failed\n");
        g_heap.ready = 0;
        return false;
    }
    kprintf("HEAP ready  base 0x%llx  end 0x%llx  mapped %llu\n",
            (unsigned long long)ROBO_HEAP_BASE,
            (unsigned long long)ROBO_HEAP_END,
            (unsigned long long)g_heap.pages);
    return true;
}
