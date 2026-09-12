#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <internal/libc.h>
#include <robo/memory.h>

#define ALIGN 16u
#define MIN_PAYLOAD 16u

typedef struct RoboBlock {
    size_t size;
    int free;
    struct RoboBlock *next;
    struct RoboBlock *prev;
} RoboBlock;

static RoboBlock *g_head;

static size_t align_up(size_t n)
{
    return (n + (ALIGN - 1u)) & ~(size_t)(ALIGN - 1u);
}

static int size_add_ok(size_t a, size_t b, size_t *out)
{
    if (a > SIZE_MAX - b)
        return 0;
    *out = a + b;
    return 1;
}

#ifdef LIBC_HOST_TEST
static unsigned char g_arena[1u << 20];
static size_t g_arena_used;

static void *os_map(size_t n)
{
    size_t off = g_arena_used;
    n = align_up(n);
    if (off + n > sizeof(g_arena))
        return 0;
    g_arena_used += n;
    return g_arena + off;
}

#else

static void *os_map(size_t n)
{
    robo_long r;
    n = align_up(n);
    if (n < 4096u)
        n = 4096u;
    r = (robo_long)(uintptr_t)robo_mmap(0, n, ROBO_PROT_READ | ROBO_PROT_WRITE,
                                        ROBO_MAP_PRIVATE | ROBO_MAP_ANON, -1, 0);
    if (robo_set_err(r))
        return 0;
    return (void *)(uintptr_t)(uint64_t)r;
}

#endif

static void split_block(RoboBlock *b, size_t need)
{
    RoboBlock *n;
    size_t rest;
    unsigned char *p;

    if (b->size < need + sizeof(RoboBlock) + MIN_PAYLOAD)
        return;
    rest = b->size - need - sizeof(RoboBlock);
    p = (unsigned char *)(b + 1) + need;
    n = (RoboBlock *)p;
    n->size = rest;
    n->free = 1;
    n->next = b->next;
    n->prev = b;
    if (n->next)
        n->next->prev = n;
    b->next = n;
    b->size = need;
}

static void coalesce(RoboBlock *b)
{
    if (b->next && b->next->free) {
        b->size += sizeof(RoboBlock) + b->next->size;
        b->next = b->next->next;
        if (b->next)
            b->next->prev = b;
    }
    if (b->prev && b->prev->free) {
        b->prev->size += sizeof(RoboBlock) + b->size;
        b->prev->next = b->next;
        if (b->next)
            b->next->prev = b->prev;
    }
}

static RoboBlock *more_heap(size_t need)
{
    size_t chunk, total;
    void *p;
    RoboBlock *b;

    chunk = 64u * 1024u;
    if (need + sizeof(RoboBlock) + 64u > chunk)
        chunk = align_up(need + sizeof(RoboBlock) + 64u);
    if (!size_add_ok(chunk, 0, &total))
        return 0;
    p = os_map(chunk);
    if (!p)
        return 0;
    b = (RoboBlock *)p;
    b->size = chunk - sizeof(RoboBlock);
    b->free = 1;
    b->next = 0;
    b->prev = 0;
    if (!g_head)
        g_head = b;
    else {
        RoboBlock *t = g_head;
        while (t->next)
            t = t->next;
        t->next = b;
        b->prev = t;
        coalesce(b);
        if (b->prev && b->prev->free)
            b = b->prev;
    }
    return b;
}

void robo_heap_init(void)
{
}

void *malloc(size_t size)
{
    RoboBlock *b;
    size_t need;

    if (size == 0)
        size = 1;
    need = align_up(size);
    if (need < MIN_PAYLOAD)
        need = MIN_PAYLOAD;
    if (need < size) {
        errno = ENOMEM;
        return 0;
    }
    for (b = g_head; b; b = b->next) {
        if (b->free && b->size >= need) {
            split_block(b, need);
            b->free = 0;
            return b + 1;
        }
    }
    b = more_heap(need);
    if (!b || !b->free || b->size < need) {
        errno = ENOMEM;
        return 0;
    }
    split_block(b, need);
    b->free = 0;
    return b + 1;
}

void free(void *ptr)
{
    RoboBlock *b;
    if (!ptr)
        return;
    b = ((RoboBlock *)ptr) - 1;
    b->free = 1;
    coalesce(b);
}

void *calloc(size_t count, size_t size)
{
    size_t n;
    void *p;
    if (size && count > SIZE_MAX / size) {
        errno = ENOMEM;
        return 0;
    }
    n = count * size;
    p = malloc(n);
    if (p)
        memset(p, 0, n);
    return p;
}

void *realloc(void *ptr, size_t size)
{
    RoboBlock *b;
    void *n;
    size_t copy;

    if (!ptr)
        return malloc(size);
    if (size == 0) {
        free(ptr);
        return 0;
    }
    b = ((RoboBlock *)ptr) - 1;
    if (align_up(size) <= b->size)
        return ptr;
    if (b->next && b->next->free) {
        size_t combined = b->size + sizeof(RoboBlock) + b->next->size;
        if (combined >= align_up(size)) {
            b->size = combined;
            b->next = b->next->next;
            if (b->next)
                b->next->prev = b;
            split_block(b, align_up(size));
            return ptr;
        }
    }
    n = malloc(size);
    if (!n)
        return 0;
    copy = b->size < size ? b->size : size;
    memcpy(n, ptr, copy);
    free(ptr);
    return n;
}
