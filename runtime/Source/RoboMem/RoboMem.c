/*
 * RoboMem — Goal 2.1 heap + Goal 2.4 optional debug allocator.
 */

#define ROBO_MEM_IMPLEMENTATION
#include "RoboMem/RoboMem.h"
#include "RoboMem/RoboMemDebug.h"

#include "RoboBase/RoboLog.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROBO_MEM_MAGIC 0x524D454Du /* 'RMEM' */
#define ROBO_MEM_STATE_LIVE 1u
#define ROBO_MEM_STATE_FREED 2u
#define ROBO_MEM_GUARD_SIZE 16
#define ROBO_MEM_GUARD_BYTE 0xABu

typedef struct robo_mem_block {
    uint32_t magic;
    uint32_t state;
    size_t size;
    size_t id;
    const char *file;
    int line;
    void *sys_base; /* original malloc pointer (may precede this header) */
    struct robo_mem_block *next;
    struct robo_mem_block *prev;
} robo_mem_block_t;

/* User pointer alignment: at least pointer size; prefer max_align_t (C11). */
#ifdef __STDC_VERSION__
#if __STDC_VERSION__ >= 201112L
#define ROBO_MEM_USER_ALIGN ((size_t)_Alignof(max_align_t))
#else
#define ROBO_MEM_USER_ALIGN (sizeof(void *))
#endif
#else
#define ROBO_MEM_USER_ALIGN (sizeof(void *))
#endif

static size_t g_robo_mem_fail_remaining = 0;

#if ROBO_MEM_DEBUG
#define ROBO_MEM_QUARANTINE_MAX 64

static int g_robo_mem_dbg_on = 1;
static size_t g_robo_mem_next_id = 1;
static robo_mem_block_t *g_robo_mem_live = NULL;
static robo_mem_block_t *g_robo_mem_quarantine = NULL;
static size_t g_robo_mem_quarantine_count = 0;
static robo_mem_stats_t g_robo_mem_stats;
static robo_mem_fault_handler_fn g_robo_mem_fault = NULL;
static void *g_robo_mem_fault_ud = NULL;
#endif

void robo_mem_debug_fail_next(size_t count)
{
    g_robo_mem_fail_remaining = count;
}

static int robo_mem_should_fail(void)
{
    if (g_robo_mem_fail_remaining == 0) {
        return 0;
    }
    g_robo_mem_fail_remaining--;
    return 1;
}

static int robo_mem_size_overflow(size_t count, size_t size)
{
    if (count == 0 || size == 0) {
        return 0;
    }
    if (count > SIZE_MAX / size) {
        return 1;
    }
    return 0;
}

static void *robo_sys_malloc(size_t size)
{
    return malloc(size);
}

static void *robo_sys_calloc(size_t count, size_t size)
{
    return calloc(count, size);
}

static void *robo_sys_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

static void robo_sys_free(void *ptr)
{
    free(ptr);
}

#if ROBO_MEM_DEBUG

static void robo_mem_fault(const char *kind, const char *message)
{
    if (g_robo_mem_fault) {
        g_robo_mem_fault(kind, message, g_robo_mem_fault_ud);
        return;
    }
    robo_log_error("RoboMemDebug %s: %s", kind, message ? message : "");
    abort();
}

static unsigned char *robo_mem_user_from_block(robo_mem_block_t *block)
{
    return (unsigned char *)(block + 1) + ROBO_MEM_GUARD_SIZE;
}

static unsigned char *robo_mem_front_guard(robo_mem_block_t *block)
{
    return (unsigned char *)(block + 1);
}

static unsigned char *robo_mem_back_guard(robo_mem_block_t *block)
{
    return robo_mem_user_from_block(block) + block->size;
}

static void robo_mem_fill_guards(robo_mem_block_t *block)
{
    memset(robo_mem_front_guard(block), ROBO_MEM_GUARD_BYTE, ROBO_MEM_GUARD_SIZE);
    memset(robo_mem_back_guard(block), ROBO_MEM_GUARD_BYTE, ROBO_MEM_GUARD_SIZE);
}

static int robo_mem_guards_ok(robo_mem_block_t *block)
{
    size_t i;
    unsigned char *f = robo_mem_front_guard(block);
    unsigned char *b = robo_mem_back_guard(block);
    for (i = 0; i < ROBO_MEM_GUARD_SIZE; i++) {
        if (f[i] != ROBO_MEM_GUARD_BYTE || b[i] != ROBO_MEM_GUARD_BYTE) {
            return 0;
        }
    }
    return 1;
}

static void robo_mem_link(robo_mem_block_t *block)
{
    block->prev = NULL;
    block->next = g_robo_mem_live;
    if (g_robo_mem_live) {
        g_robo_mem_live->prev = block;
    }
    g_robo_mem_live = block;
}

static void robo_mem_unlink_from(robo_mem_block_t **head, robo_mem_block_t *block)
{
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        *head = block->next;
    }
    if (block->next) {
        block->next->prev = block->prev;
    }
    block->next = NULL;
    block->prev = NULL;
}

static void robo_mem_unlink(robo_mem_block_t *block)
{
    robo_mem_unlink_from(&g_robo_mem_live, block);
}

static void robo_mem_quarantine_push(robo_mem_block_t *block)
{
    block->prev = NULL;
    block->next = g_robo_mem_quarantine;
    if (g_robo_mem_quarantine) {
        g_robo_mem_quarantine->prev = block;
    }
    g_robo_mem_quarantine = block;
    g_robo_mem_quarantine_count++;

    while (g_robo_mem_quarantine_count > ROBO_MEM_QUARANTINE_MAX) {
        robo_mem_block_t *tail = g_robo_mem_quarantine;
        while (tail && tail->next) {
            tail = tail->next;
        }
        if (!tail) {
            break;
        }
        robo_mem_unlink_from(&g_robo_mem_quarantine, tail);
        g_robo_mem_quarantine_count--;
        {
            void *base = tail->sys_base ? tail->sys_base : (void *)tail;
            tail->magic = 0;
            robo_sys_free(base);
        }
    }
}

static size_t robo_mem_user_align(void)
{
    size_t a = ROBO_MEM_USER_ALIGN;
    if (a < sizeof(void *)) {
        a = sizeof(void *);
    }
    /* Require power-of-two for mask alignment. */
    if (a == 0 || (a & (a - 1)) != 0) {
        a = sizeof(void *);
    }
    return a;
}

static size_t robo_mem_total_bytes(size_t user_size)
{
    size_t prefix = sizeof(robo_mem_block_t) + ROBO_MEM_GUARD_SIZE;
    size_t suffix = ROBO_MEM_GUARD_SIZE;
    size_t align = robo_mem_user_align();
    size_t body;

    if (user_size > SIZE_MAX - prefix || prefix + user_size > SIZE_MAX - suffix) {
        return 0;
    }
    body = prefix + user_size + suffix;
    /* Extra slack so the user pointer can be aligned within the slab. */
    if (body > SIZE_MAX - align) {
        return 0;
    }
    return body + align;
}

static void *robo_mem_track_alloc(size_t size, const char *file, int line, int zero)
{
    size_t total;
    size_t align;
    size_t front;
    unsigned char *raw;
    uintptr_t min_user;
    uintptr_t user_addr;
    robo_mem_block_t *block;
    void *user;

    if (size == 0) {
        return NULL;
    }
    if (robo_mem_should_fail()) {
        return NULL;
    }
    total = robo_mem_total_bytes(size);
    if (total == 0) {
        return NULL;
    }
    raw = (unsigned char *)robo_sys_malloc(total);
    if (!raw) {
        return NULL;
    }

    align = robo_mem_user_align();
    front = sizeof(robo_mem_block_t) + ROBO_MEM_GUARD_SIZE;
    min_user = (uintptr_t)(raw + front);
    user_addr = (min_user + (align - 1)) & ~(uintptr_t)(align - 1);
    block = (robo_mem_block_t *)(void *)((unsigned char *)user_addr - ROBO_MEM_GUARD_SIZE -
                                         sizeof(robo_mem_block_t));
    if ((unsigned char *)block < raw) {
        robo_sys_free(raw);
        return NULL;
    }

    block->magic = ROBO_MEM_MAGIC;
    block->state = ROBO_MEM_STATE_LIVE;
    block->size = size;
    block->id = g_robo_mem_next_id++;
    block->file = file ? file : "?";
    block->line = line;
    block->sys_base = raw;
    block->next = NULL;
    block->prev = NULL;
    robo_mem_fill_guards(block);
    user = robo_mem_user_from_block(block);
    if ((uintptr_t)user != user_addr) {
        robo_sys_free(raw);
        return NULL;
    }
    if (zero) {
        memset(user, 0, size);
    }
    robo_mem_link(block);

    g_robo_mem_stats.live_count++;
    g_robo_mem_stats.live_bytes += size;
    g_robo_mem_stats.total_allocs++;
    if (g_robo_mem_stats.live_count > g_robo_mem_stats.peak_live_count) {
        g_robo_mem_stats.peak_live_count = g_robo_mem_stats.live_count;
    }
    if (g_robo_mem_stats.live_bytes > g_robo_mem_stats.peak_live_bytes) {
        g_robo_mem_stats.peak_live_bytes = g_robo_mem_stats.live_bytes;
    }
    return user;
}

static int robo_mem_is_tracked(robo_mem_block_t *block)
{
    return block && block->magic == ROBO_MEM_MAGIC;
}

/** Locate a user pointer via live/quarantine lists — never probe arbitrary addresses. */
static robo_mem_block_t *robo_mem_find_user(void *user)
{
    robo_mem_block_t *b;
    if (!user) {
        return NULL;
    }
    for (b = g_robo_mem_live; b; b = b->next) {
        if (robo_mem_user_from_block(b) == user) {
            return b;
        }
    }
    for (b = g_robo_mem_quarantine; b; b = b->next) {
        if (robo_mem_user_from_block(b) == user) {
            return b;
        }
    }
    return NULL;
}

#endif /* ROBO_MEM_DEBUG */

void *robo_malloc_at(size_t size, const char *file, int line)
{
#if ROBO_MEM_DEBUG
    if (g_robo_mem_dbg_on) {
        return robo_mem_track_alloc(size, file, line, 0);
    }
#else
    (void)file;
    (void)line;
#endif
    if (size == 0) {
        return NULL;
    }
    if (robo_mem_should_fail()) {
        return NULL;
    }
    return robo_sys_malloc(size);
}

void *robo_calloc_at(size_t count, size_t size, const char *file, int line)
{
#if ROBO_MEM_DEBUG
    if (g_robo_mem_dbg_on) {
        size_t bytes;
        if (count == 0 || size == 0) {
            return NULL;
        }
        if (robo_mem_size_overflow(count, size)) {
            return NULL;
        }
        bytes = count * size;
        return robo_mem_track_alloc(bytes, file, line, 1);
    }
#else
    (void)file;
    (void)line;
#endif
    if (count == 0 || size == 0) {
        return NULL;
    }
    if (robo_mem_size_overflow(count, size)) {
        return NULL;
    }
    if (robo_mem_should_fail()) {
        return NULL;
    }
    return robo_sys_calloc(count, size);
}

void robo_free_at(void *ptr, const char *file, int line)
{
#if ROBO_MEM_DEBUG
    robo_mem_block_t *block;
    char msg[256];

    (void)file;
    (void)line;
    if (!ptr) {
        return;
    }
    block = robo_mem_find_user(ptr);
    if (!block) {
        if (g_robo_mem_dbg_on) {
            robo_mem_fault("invalid-free", "pointer is not a RoboMem debug allocation");
            return;
        }
        robo_sys_free(ptr);
        return;
    }
    if (!robo_mem_is_tracked(block)) {
        robo_mem_fault("invalid-free", "allocation metadata is corrupted");
        return;
    }
    if (block->state == ROBO_MEM_STATE_FREED) {
        snprintf(msg, sizeof(msg), "double-free of allocation #%zu (%s:%d)",
                 block->id, block->file, block->line);
        robo_mem_fault("double-free", msg);
        return;
    }
    if (block->state != ROBO_MEM_STATE_LIVE) {
        robo_mem_fault("invalid-free", "allocation metadata is corrupted");
        return;
    }
    if (!robo_mem_guards_ok(block)) {
        snprintf(msg, sizeof(msg), "guard corruption on allocation #%zu (%s:%d)",
                 block->id, block->file, block->line);
        robo_mem_fault("guard-corruption", msg);
        /* Reclaim even after a non-aborting handler so live lists stay consistent. */
        robo_mem_unlink(block);
        if (g_robo_mem_stats.live_count > 0) {
            g_robo_mem_stats.live_count--;
        }
        if (g_robo_mem_stats.live_bytes >= block->size) {
            g_robo_mem_stats.live_bytes -= block->size;
        } else {
            g_robo_mem_stats.live_bytes = 0;
        }
        g_robo_mem_stats.total_frees++;
        block->state = ROBO_MEM_STATE_FREED;
        robo_mem_quarantine_push(block);
        return;
    }
    robo_mem_unlink(block);
    if (g_robo_mem_stats.live_count > 0) {
        g_robo_mem_stats.live_count--;
    }
    if (g_robo_mem_stats.live_bytes >= block->size) {
        g_robo_mem_stats.live_bytes -= block->size;
    } else {
        g_robo_mem_stats.live_bytes = 0;
    }
    g_robo_mem_stats.total_frees++;
    block->state = ROBO_MEM_STATE_FREED;
    memset(robo_mem_user_from_block(block), 0xDD, block->size);
    robo_mem_quarantine_push(block);
#else
    (void)file;
    (void)line;
    robo_sys_free(ptr);
#endif
}

void *robo_realloc_at(void *ptr, size_t size, const char *file, int line)
{
    if (size == 0) {
        robo_free_at(ptr, file, line);
        return NULL;
    }
    if (!ptr) {
        return robo_malloc_at(size, file, line);
    }

#if ROBO_MEM_DEBUG
    if (g_robo_mem_dbg_on) {
        robo_mem_block_t *block = robo_mem_find_user(ptr);
        void *neu;
        size_t copy;
        if (!block || !robo_mem_is_tracked(block) ||
            block->state != ROBO_MEM_STATE_LIVE) {
            robo_mem_fault("invalid-realloc", "realloc of non-live debug block");
            return NULL;
        }
        if (!robo_mem_guards_ok(block)) {
            robo_mem_fault("guard-corruption", "guard corruption before realloc");
            return NULL;
        }
        neu = robo_malloc_at(size, file, line);
        if (!neu) {
            return NULL;
        }
        copy = block->size < size ? block->size : size;
        memcpy(neu, ptr, copy);
        robo_free_at(ptr, file, line);
        return neu;
    }
#else
    (void)file;
    (void)line;
#endif
    if (robo_mem_should_fail()) {
        return NULL;
    }
    return robo_sys_realloc(ptr, size);
}

void *robo_malloc(size_t size)
{
    return robo_malloc_at(size, "?", 0);
}

void *robo_calloc(size_t count, size_t size)
{
    return robo_calloc_at(count, size, "?", 0);
}

void *robo_realloc(void *ptr, size_t size)
{
    return robo_realloc_at(ptr, size, "?", 0);
}

void robo_free(void *ptr)
{
    robo_free_at(ptr, "?", 0);
}

void *robo_mem_copy(void *dst, const void *src, size_t n)
{
    if (n == 0) {
        return dst;
    }
    if (!dst || !src) {
        return NULL;
    }
    return memcpy(dst, src, n);
}

void *robo_mem_move(void *dst, const void *src, size_t n)
{
    if (n == 0) {
        return dst;
    }
    if (!dst || !src) {
        return NULL;
    }
    return memmove(dst, src, n);
}

void *robo_mem_set(void *dst, int value, size_t n)
{
    if (n == 0) {
        return dst;
    }
    if (!dst) {
        return NULL;
    }
    return memset(dst, value, n);
}

int robo_mem_compare(const void *a, const void *b, size_t n)
{
    if (n == 0) {
        return 0;
    }
    if (!a || !b) {
        if (a == b) {
            return 0;
        }
        return a ? 1 : -1;
    }
    return memcmp(a, b, n);
}

void robo_mem_debug_enable(void)
{
#if ROBO_MEM_DEBUG
    g_robo_mem_dbg_on = 1;
#endif
}

void robo_mem_debug_disable(void)
{
#if ROBO_MEM_DEBUG
    g_robo_mem_dbg_on = 0;
#endif
}

int robo_mem_debug_is_enabled(void)
{
#if ROBO_MEM_DEBUG
    return g_robo_mem_dbg_on;
#else
    return 0;
#endif
}

void robo_mem_debug_set_fault_handler(robo_mem_fault_handler_fn handler, void *userdata)
{
#if ROBO_MEM_DEBUG
    g_robo_mem_fault = handler;
    g_robo_mem_fault_ud = userdata;
#else
    (void)handler;
    (void)userdata;
#endif
}

void robo_mem_debug_get_stats(robo_mem_stats_t *out)
{
    if (!out) {
        return;
    }
#if ROBO_MEM_DEBUG
    *out = g_robo_mem_stats;
#else
    memset(out, 0, sizeof(*out));
#endif
}

void robo_mem_debug_reset_stats(void)
{
#if ROBO_MEM_DEBUG
    memset(&g_robo_mem_stats, 0, sizeof(g_robo_mem_stats));
#endif
}

size_t robo_mem_debug_live_count(void)
{
#if ROBO_MEM_DEBUG
    return g_robo_mem_stats.live_count;
#else
    return 0;
#endif
}

void robo_mem_debug_report(void)
{
#if ROBO_MEM_DEBUG
    robo_mem_block_t *b;
    const char *base;
    const char *p;

    fprintf(stderr, "RoboMemDebug report\n");
    fprintf(stderr, "Active allocations: %zu\n", g_robo_mem_stats.live_count);
    fprintf(stderr, "Total allocated bytes (live): %zu\n", g_robo_mem_stats.live_bytes);
    fprintf(stderr, "Peak live count: %zu\n", g_robo_mem_stats.peak_live_count);
    fprintf(stderr, "Peak live bytes: %zu\n", g_robo_mem_stats.peak_live_bytes);
    fprintf(stderr, "Total allocs: %zu  Total frees: %zu\n",
            g_robo_mem_stats.total_allocs, g_robo_mem_stats.total_frees);
    for (b = g_robo_mem_live; b; b = b->next) {
        base = b->file ? b->file : "?";
        for (p = base; *p; p++) {
            if (*p == '/' || *p == '\\') {
                base = p + 1;
            }
        }
        fprintf(stderr, "Allocation #%zu: %zu bytes at %s:%d\n",
                b->id, b->size, base, b->line);
    }
#else
    fprintf(stderr, "RoboMemDebug: not compiled in (ROBO_MEM_DEBUG=0)\n");
#endif
}

int robo_mem_debug_check(void *ptr)
{
#if ROBO_MEM_DEBUG
    robo_mem_block_t *block;
    if (!ptr) {
        for (block = g_robo_mem_live; block; block = block->next) {
            if (!robo_mem_guards_ok(block)) {
                return 1;
            }
        }
        return 0;
    }
    block = robo_mem_find_user(ptr);
    if (!block || !robo_mem_is_tracked(block) ||
        block->state != ROBO_MEM_STATE_LIVE) {
        return 1;
    }
    return robo_mem_guards_ok(block) ? 0 : 1;
#else
    (void)ptr;
    return 0;
#endif
}

void robo_mem_debug_corrupt_guard_for_test(void *ptr, int back)
{
#if ROBO_MEM_DEBUG
    robo_mem_block_t *block;
    unsigned char *g;
    if (!ptr) {
        return;
    }
    block = robo_mem_find_user(ptr);
    if (!block || !robo_mem_is_tracked(block) ||
        block->state != ROBO_MEM_STATE_LIVE) {
        return;
    }
    g = back ? robo_mem_back_guard(block) : robo_mem_front_guard(block);
    g[0] = (unsigned char)(ROBO_MEM_GUARD_BYTE ^ 0xFFu);
#else
    (void)ptr;
    (void)back;
#endif
}
