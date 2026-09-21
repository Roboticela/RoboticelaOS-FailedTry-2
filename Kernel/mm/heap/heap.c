#include "mm/heap/heap.h"
#include "mm/pmm.h"
#include "mm/vmm/vmm.h"
#include "kio.h"

#if RoboHeapBase != RoboVmmHeapBase
#error kernel heap base must match the VMM-reserved heap window
#endif

#define RoboHeapMagicUsed 0x48454131u
#define RoboHeapMagicFree 0x48454632u
#define RoboHeapVmFlags   (RoboVmRead | RoboVmWrite | RoboVmKernel)

typedef struct HeapBlock {
    size_t size;
    uint32_t magic;
    uint16_t free;
    uint16_t pad;
    struct HeapBlock *next;
    struct HeapBlock *prev;
} HeapBlock;

#define RoboHeapHeader ((size_t)sizeof(HeapBlock))

_Static_assert(sizeof(HeapBlock) == 32, "heap header must be 32 bytes");

#ifndef HEAP_HOST_TEST
static uint64_t HeapIrqSave(void)
{
    uint64_t f;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(f) : : "memory");
    return f;
}
static void HeapIrqRestore(uint64_t f)
{
    __asm__ volatile ("pushq %0; popfq" : : "r"(f) : "memory");
}
#else
static uint64_t HeapIrqSave(void) { return 0; }
static void HeapIrqRestore(uint64_t f) { (void)f; }
#endif

static struct {
    int ready;
    VirtAddr mappedEnd;
    HeapBlock *head;
    HeapBlock *tail;
    size_t pages;
} gHeap;

static void HeapFill(void *dst, uint8_t v, size_t n)
{
    uint8_t *p = (uint8_t *)dst;
    while (n--)
        *p++ = v;
}

static void HeapCopy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--)
        *d++ = *s++;
}

static size_t HeapAlign(size_t n)
{
    size_t a = RoboHeapAlignment;
    if (n > ((size_t)-1) - (a - 1u))
        return 0;
    return (n + (a - 1u)) & ~(a - 1u);
}

static int HeapAddOk(size_t a, size_t b, size_t *out)
{
    if (a > ((size_t)-1) - b)
        return 0;
    if (out)
        *out = a + b;
    return 1;
}

static VirtAddr BlockAddr(const HeapBlock *b)
{
    return (VirtAddr)(uintptr_t)b;
}

static VirtAddr BlockEnd(const HeapBlock *b)
{
    return BlockAddr(b) + (VirtAddr)b->size;
}

static void *BlockUser(HeapBlock *b)
{
    return (void *)(b + 1);
}

static size_t BlockUserSize(const HeapBlock *b)
{
    if (!b || b->size < RoboHeapHeader)
        return 0;
    return b->size - RoboHeapHeader;
}

static int BlockInHeap(const HeapBlock *b)
{
    VirtAddr a, e;
    if (!b)
        return 0;
    a = BlockAddr(b);
    if (a < RoboHeapBase || a >= gHeap.mappedEnd)
        return 0;
    if (b->size < RoboHeapHeader || (b->size & (RoboHeapAlignment - 1u)))
        return 0;
    if (!HeapAddOk((size_t)a, b->size, 0))
        return 0;
    e = BlockEnd(b);
    return e > a && e <= gHeap.mappedEnd;
}

static size_t BlockNeed(size_t user)
{
    size_t aligned, total;
    aligned = HeapAlign(user);
    if (!aligned)
        return 0;
    if (!HeapAddOk(RoboHeapHeader, aligned, &total))
        return 0;
    return total;
}

static void InsertAfter(HeapBlock *pos, HeapBlock *n)
{
    n->prev = pos;
    n->next = pos->next;
    if (pos->next)
        pos->next->prev = n;
    else
        gHeap.tail = n;
    pos->next = n;
}

static void UnlinkBlock(HeapBlock *b)
{
    if (b->prev)
        b->prev->next = b->next;
    else
        gHeap.head = b->next;
    if (b->next)
        b->next->prev = b->prev;
    else
        gHeap.tail = b->prev;
    b->next = 0;
    b->prev = 0;
}

static void MarkFree(HeapBlock *b)
{
    b->free = 1;
    b->magic = RoboHeapMagicFree;
}

static void MarkUsed(HeapBlock *b)
{
    b->free = 0;
    b->magic = RoboHeapMagicUsed;
}

static void SplitBlock(HeapBlock *b, size_t need)
{
    HeapBlock *rest;
    size_t remain;
    if (!b || b->size < need)
        return;
    remain = b->size - need;
    if (remain < RoboHeapHeader + RoboHeapAlignment)
        return;
    rest = (HeapBlock *)(uintptr_t)(BlockAddr(b) + (VirtAddr)need);
    rest->size = remain;
    rest->pad = 0;
    MarkFree(rest);
    b->size = need;
    InsertAfter(b, rest);
}

static void CoalesceBlock(HeapBlock *b)
{
    if (!b || !b->free)
        return;
    while (b->next && b->next->free && BlockEnd(b) == BlockAddr(b->next)) {
        HeapBlock *n = b->next;
        if (!HeapAddOk(b->size, n->size, &b->size))
            return;
        UnlinkBlock(n);
    }
    while (b->prev && b->prev->free && BlockEnd(b->prev) == BlockAddr(b)) {
        HeapBlock *p = b->prev;
        if (!HeapAddOk(p->size, b->size, &p->size))
            return;
        UnlinkBlock(b);
        b = p;
    }
}

static int HeapGrow(size_t extra)
{
    size_t pages, i;
    VirtAddr start, va, room;
    PhysAddr pa;
    HeapBlock *n;

    if (!extra)
        return 0;
    extra = (size_t)PageAlignUp((PhysAddr)extra);
    if (!extra || extra == (size_t)RoboInvalidPhysAddr)
        return 0;
    if (gHeap.mappedEnd > RoboHeapEnd)
        return 0;
    room = RoboHeapEnd - gHeap.mappedEnd;
    if ((VirtAddr)extra > room)
        return 0;
    pages = extra / (size_t)RoboPageSize;
    if (!pages)
        return 0;

    start = gHeap.mappedEnd;
    va = start;
    for (i = 0; i < pages; i++) {
        pa = PageAlloc();
        if (pa == RoboInvalidPhysAddr)
            goto rollback;
        if (!VmmMap(va, pa, RoboHeapVmFlags)) {
            PageFree(pa);
            goto rollback;
        }
        va += RoboPageSize;
    }

    gHeap.mappedEnd = va;
    gHeap.pages += pages;
    kprintf("HEAP  grow %llu pages  0x%llx-0x%llx\n",
            (unsigned long long)pages,
            (unsigned long long)start,
            (unsigned long long)va);

    if (gHeap.tail && gHeap.tail->free && BlockEnd(gHeap.tail) == start) {
        if (!HeapAddOk(gHeap.tail->size, extra, &gHeap.tail->size))
            return 0;
        return 1;
    }

    n = (HeapBlock *)(uintptr_t)start;
    n->size = extra;
    n->pad = 0;
    n->next = 0;
    n->prev = gHeap.tail;
    MarkFree(n);
    if (gHeap.tail)
        gHeap.tail->next = n;
    else
        gHeap.head = n;
    gHeap.tail = n;
    return 1;

rollback:
    while (va > start) {
        PhysAddr got;
        va -= RoboPageSize;
        if (VmmUnmap(va, &got))
            PageFree(got);
    }
    kprintf("HEAP: grow failed\n");
    return 0;
}

static int HeapGrowFor(size_t need)
{
    size_t have = 0;
    size_t extra;
    if (gHeap.tail && gHeap.tail->free && BlockEnd(gHeap.tail) == gHeap.mappedEnd)
        have = gHeap.tail->size;
    if (have >= need)
        return 1;
    extra = need - have;
    return HeapGrow(extra);
}

static void *TakeBlock(HeapBlock *b, size_t need)
{
    SplitBlock(b, need);
    MarkUsed(b);
    return BlockUser(b);
}

static void *HeapFind(size_t need)
{
    HeapBlock *b;
    for (b = gHeap.head; b; b = b->next) {
        if (!BlockInHeap(b))
            return 0;
        if (b->free && b->magic == RoboHeapMagicFree && b->size >= need)
            return TakeBlock(b, need);
    }
    return 0;
}

static HeapBlock *BlockFromUser(void *ptr)
{
    uintptr_t u, hdr;
    HeapBlock *b;
    if (!ptr)
        return 0;
    u = (uintptr_t)ptr;
    if (u < RoboHeapHeader)
        return 0;
    if (u & (RoboHeapAlignment - 1u))
        return 0;
    hdr = u - RoboHeapHeader;
    if (hdr < RoboHeapBase || hdr >= gHeap.mappedEnd)
        return 0;
    b = (HeapBlock *)hdr;
    if (!BlockInHeap(b))
        return 0;
    if ((uintptr_t)BlockUser(b) != u)
        return 0;
    if (BlockEnd(b) > gHeap.mappedEnd)
        return 0;
    return b;
}

bool HeapValidate(void)
{
    HeapBlock *b, *last;
    VirtAddr expect;
    size_t seen = 0;

    if (!gHeap.ready)
        return false;
    if (gHeap.mappedEnd < RoboHeapBase || gHeap.mappedEnd > RoboHeapEnd)
        return false;
    if ((gHeap.mappedEnd - RoboHeapBase) != (VirtAddr)gHeap.pages * RoboPageSize)
        return false;
    if (!gHeap.head || !gHeap.tail)
        return false;
    if (BlockAddr(gHeap.head) != RoboHeapBase)
        return false;

    expect = RoboHeapBase;
    last = 0;
    for (b = gHeap.head; b; last = b, b = b->next) {
        if (!BlockInHeap(b))
            return false;
        if (BlockAddr(b) != expect)
            return false;
        if (b->prev != last)
            return false;
        if (last && last->next != b)
            return false;
        if (b->free) {
            if (b->magic != RoboHeapMagicFree)
                return false;
            if (last && last->free)
                return false;
        } else if (b->magic != RoboHeapMagicUsed) {
            return false;
        }
        expect = BlockEnd(b);
        seen++;
        if (seen > gHeap.pages * (RoboPageSize / (RoboHeapHeader + RoboHeapAlignment)))
            return false;
    }
    if (expect != gHeap.mappedEnd)
        return false;
    if (last != gHeap.tail)
        return false;
    return true;
}

static void HeapStatWalk(size_t *used, size_t *freeBytes, size_t *allocs)
{
    HeapBlock *b;
    size_t u = 0, f = 0, n = 0;
    for (b = gHeap.head; b; b = b->next) {
        if (b->free)
            f += b->size;
        else {
            u += b->size;
            n++;
        }
    }
    if (used)
        *used = u;
    if (freeBytes)
        *freeBytes = f;
    if (allocs)
        *allocs = n;
}

int HeapReady(void)
{
    return gHeap.ready;
}

size_t HeapTotalBytes(void)
{
    return gHeap.ready ? (size_t)(gHeap.mappedEnd - RoboHeapBase) : 0;
}

size_t HeapUsedBytes(void)
{
    size_t u = 0;
    if (gHeap.ready)
        HeapStatWalk(&u, 0, 0);
    return u;
}

size_t HeapFreeBytes(void)
{
    size_t f = 0;
    if (gHeap.ready)
        HeapStatWalk(0, &f, 0);
    return f;
}

size_t HeapPageCount(void)
{
    return gHeap.pages;
}

size_t HeapAllocationCount(void)
{
    size_t n = 0;
    if (gHeap.ready)
        HeapStatWalk(0, 0, &n);
    return n;
}

void *kmalloc(size_t size)
{
    size_t need;
    void *p;
    int tries;
    uint64_t flags;

    if (!gHeap.ready || !size)
        return 0;
    need = BlockNeed(size);
    if (!need)
        return 0;
    flags = HeapIrqSave();
    for (tries = 0; tries < 2; tries++) {
        p = HeapFind(need);
        if (p) {
            HeapIrqRestore(flags);
            return p;
        }
        if (!HeapGrowFor(need)) {
            HeapIrqRestore(flags);
            return 0;
        }
    }
    HeapIrqRestore(flags);
    return 0;
}

void kfree(void *ptr)
{
    HeapBlock *b;
    uint64_t flags;
    if (!ptr)
        return;
    flags = HeapIrqSave();
    if (!gHeap.ready) {
        HeapIrqRestore(flags);
        kprintf("HEAP: free before init\n");
        return;
    }
    b = BlockFromUser(ptr);
    if (!b) {
        HeapIrqRestore(flags);
        kprintf("HEAP: invalid free\n");
        return;
    }
    if (b->free || b->magic == RoboHeapMagicFree) {
        HeapIrqRestore(flags);
        kprintf("HEAP: double free detected\n");
        return;
    }
    if (b->magic != RoboHeapMagicUsed) {
        HeapIrqRestore(flags);
        kprintf("HEAP: corrupted metadata\n");
        return;
    }
    HeapFill(BlockUser(b), 0xDDu, BlockUserSize(b));
    MarkFree(b);
    CoalesceBlock(b);
    HeapIrqRestore(flags);
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
    HeapFill(p, 0, bytes);
    return p;
}

void *krealloc(void *ptr, size_t newSize)
{
    HeapBlock *b;
    void *p;
    size_t old, copy;
    if (!ptr)
        return kmalloc(newSize);
    if (!newSize) {
        kfree(ptr);
        return 0;
    }
    b = BlockFromUser(ptr);
    if (!b || b->free || b->magic != RoboHeapMagicUsed) {
        kprintf("HEAP: realloc of invalid pointer\n");
        return 0;
    }
    old = BlockUserSize(b);
    if (newSize <= old)
        return ptr;
    p = kmalloc(newSize);
    if (!p)
        return 0;
    copy = old;
    if (copy > newSize)
        copy = newSize;
    HeapCopy(p, ptr, copy);
    kfree(ptr);
    return p;
}

void HeapReport(void)
{
    size_t used, freeBytes, allocs;
    if (!gHeap.ready) {
        kprintf("HEAP: not initialized\n");
        return;
    }
    HeapStatWalk(&used, &freeBytes, &allocs);
    kprintf("Roboticela Heap\n");
    kprintf("---------------\n");
    kprintf("Strategy:        first-fit free list\n");
    kprintf("Alignment:       %u\n", (unsigned)RoboHeapAlignment);
    kprintf("Window:          0x%llx-0x%llx\n",
            (unsigned long long)RoboHeapBase,
            (unsigned long long)RoboHeapEnd);
    kprintf("Mapped:          0x%llx-0x%llx  %llu pages\n",
            (unsigned long long)RoboHeapBase,
            (unsigned long long)gHeap.mappedEnd,
            (unsigned long long)gHeap.pages);
    kprintf("Used:            %llu  free %llu  allocs %llu\n",
            (unsigned long long)used,
            (unsigned long long)freeBytes,
            (unsigned long long)allocs);
}

#ifndef HEAP_HOST_TEST
void HeapFbReport(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    if (!gHeap.ready)
        return;
    KfbPuts("HEAP  ", mute);
    KfbPuthex((unsigned long long)RoboHeapBase, white);
    KfbPuts("  pages ", mute);
    KfbPutu((unsigned long long)gHeap.pages, white);
    KfbPuts("  allocs ", mute);
    KfbPutu((unsigned long long)HeapAllocationCount(), white);
    KfbPuts("\n", mute);
}
#endif

bool HeapInit(void)
{
    HeapFill(&gHeap, 0, sizeof(gHeap));
    kprintf("HEAP init\n");
    if (!PmmReady() || !VmmReady()) {
        kprintf("HEAP: PMM/VMM not ready\n");
        return false;
    }
    gHeap.mappedEnd = RoboHeapBase;
    if (!HeapGrow((size_t)RoboPageSize)) {
        kprintf("HEAP: initial map failed\n");
        HeapFill(&gHeap, 0, sizeof(gHeap));
        return false;
    }
    gHeap.ready = 1;
    if (!HeapValidate()) {
        kprintf("HEAP: initial validate failed\n");
        gHeap.ready = 0;
        return false;
    }
    kprintf("HEAP ready  base 0x%llx  end 0x%llx  mapped %llu\n",
            (unsigned long long)RoboHeapBase,
            (unsigned long long)RoboHeapEnd,
            (unsigned long long)gHeap.pages);
    return true;
}
