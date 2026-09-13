/*
 * RoboPool — Goal 2.3 fixed-size free-list pool.
 *
 * Storage (one or more robo_malloc regions):
 *   - pool header
 *   - free_stack[capacity] of size_t indices
 *   - in_use[capacity] bytes (1 = allocated)
 *   - slots[capacity * stride], base pointer-aligned / stronger if needed
 */

#include "RoboMem/RoboPool.h"
#include "RoboMem/RoboMem.h"

#include <stdint.h>

struct robo_pool {
    unsigned char *slots;
    size_t *free_stack;
    unsigned char *in_use;
    size_t object_size;
    size_t stride;
    size_t alignment;
    size_t capacity;
    size_t free_top;
};

static int robo_is_pow2(size_t v)
{
    return v != 0 && (v & (v - 1)) == 0;
}

static int robo_align_up(size_t value, size_t alignment, size_t *out)
{
    size_t add;
    size_t aligned;

    if (!robo_is_pow2(alignment)) {
        return 0;
    }
    add = alignment - 1;
    if (value > SIZE_MAX - add) {
        return 0;
    }
    aligned = (value + add) & ~add;
    *out = aligned;
    return 1;
}

static size_t robo_pool_choose_align(size_t object_size)
{
    size_t align = sizeof(void *);
    if (robo_is_pow2(object_size) && object_size > align) {
        align = object_size;
    }
    return align;
}

robo_pool_t *robo_pool_create(size_t object_size, size_t capacity)
{
    robo_pool_t *pool;
    size_t align;
    size_t payload;
    size_t stride;
    size_t stack_bytes;
    size_t use_bytes;
    size_t i;
    unsigned char *block;
    size_t header;
    size_t off_stack;
    size_t off_use;
    size_t off_slots;
    size_t total;

    if (object_size == 0 || capacity == 0) {
        return NULL;
    }

    align = robo_pool_choose_align(object_size);
    payload = object_size < sizeof(void *) ? sizeof(void *) : object_size;
    if (!robo_align_up(payload, align, &stride)) {
        return NULL;
    }
    if (capacity > SIZE_MAX / stride) {
        return NULL;
    }

    stack_bytes = capacity * sizeof(size_t);
    use_bytes = capacity;

    header = sizeof(robo_pool_t);
    if (!robo_align_up(header, sizeof(void *), &off_stack)) {
        return NULL;
    }
    if (off_stack > SIZE_MAX - stack_bytes) {
        return NULL;
    }
    if (!robo_align_up(off_stack + stack_bytes, sizeof(void *), &off_use)) {
        return NULL;
    }
    if (off_use > SIZE_MAX - use_bytes) {
        return NULL;
    }
    if (!robo_align_up(off_use + use_bytes, sizeof(void *), &off_slots)) {
        return NULL;
    }
    /* Reserve (align-1) so we can bump slots to absolute alignment. */
    if (off_slots > SIZE_MAX - (align - 1)) {
        return NULL;
    }
    if (off_slots + (align - 1) > SIZE_MAX - capacity * stride) {
        return NULL;
    }
    total = off_slots + (align - 1) + capacity * stride;

    block = (unsigned char *)robo_malloc(total);
    if (!block) {
        return NULL;
    }

    pool = (robo_pool_t *)(void *)block;
    pool->free_stack = (size_t *)(void *)(block + off_stack);
    pool->in_use = block + off_use;
    {
        uintptr_t raw = (uintptr_t)(block + off_slots);
        uintptr_t aligned_slots = (raw + (align - 1)) & ~(uintptr_t)(align - 1);
        pool->slots = (unsigned char *)aligned_slots;
    }
    pool->object_size = object_size;
    pool->stride = stride;
    pool->alignment = align;
    pool->capacity = capacity;
    pool->free_top = capacity;

    for (i = 0; i < capacity; i++) {
        pool->free_stack[i] = capacity - 1 - i;
        pool->in_use[i] = 0;
    }

    return pool;
}

void *robo_pool_alloc(robo_pool_t *pool)
{
    size_t index;

    if (!pool || pool->free_top == 0) {
        return NULL;
    }
    pool->free_top--;
    index = pool->free_stack[pool->free_top];
    pool->in_use[index] = 1;
    return pool->slots + index * pool->stride;
}

static int robo_pool_index_of(const robo_pool_t *pool, void *ptr, size_t *out_index)
{
    uintptr_t base;
    uintptr_t addr;
    uintptr_t delta;
    size_t index;

    if (!pool || !ptr) {
        return 0;
    }
    base = (uintptr_t)pool->slots;
    addr = (uintptr_t)ptr;
    if (addr < base) {
        return 0;
    }
    delta = addr - base;
    if (delta % pool->stride != 0) {
        return 0;
    }
    index = (size_t)(delta / pool->stride);
    if (index >= pool->capacity) {
        return 0;
    }
    *out_index = index;
    return 1;
}

robo_result_t robo_pool_free(robo_pool_t *pool, void *ptr)
{
    size_t index;

    if (!pool) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!ptr) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_pool_index_of(pool, ptr, &index)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!pool->in_use[index]) {
        return ROBO_ALREADY_EXISTS; /* double-free */
    }
    if (pool->free_top >= pool->capacity) {
        return ROBO_ERROR; /* internal consistency */
    }
    pool->in_use[index] = 0;
    pool->free_stack[pool->free_top] = index;
    pool->free_top++;
    return ROBO_OK;
}

void robo_pool_reset(robo_pool_t *pool)
{
    size_t i;

    if (!pool) {
        return;
    }
    pool->free_top = pool->capacity;
    for (i = 0; i < pool->capacity; i++) {
        pool->free_stack[i] = pool->capacity - 1 - i;
        pool->in_use[i] = 0;
    }
}

void robo_pool_destroy(robo_pool_t *pool)
{
    if (!pool) {
        return;
    }
    pool->slots = NULL;
    pool->free_stack = NULL;
    pool->in_use = NULL;
    pool->capacity = 0;
    pool->free_top = 0;
    robo_free(pool);
}

size_t robo_pool_object_size(const robo_pool_t *pool)
{
    return pool ? pool->object_size : 0;
}

size_t robo_pool_capacity(const robo_pool_t *pool)
{
    return pool ? pool->capacity : 0;
}

size_t robo_pool_available(const robo_pool_t *pool)
{
    return pool ? pool->free_top : 0;
}
