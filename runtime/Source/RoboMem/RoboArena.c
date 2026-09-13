/*
 * RoboArena — Goal 2.2 bump allocator.
 *
 * Layout (one robo_malloc):
 *   [ robo_arena_t header | capacity bytes of bump storage ]
 */

#include "RoboMem/RoboArena.h"
#include "RoboMem/RoboMem.h"

#include <stdint.h>

struct robo_arena {
    unsigned char *base;
    size_t capacity;
    size_t offset;
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

robo_arena_t *robo_arena_create(size_t capacity)
{
    size_t header;
    size_t total;
    unsigned char *block;
    robo_arena_t *arena;
    size_t base_off;

    if (capacity == 0) {
        return NULL;
    }

    header = sizeof(robo_arena_t);
    /* Align payload start to max(default) pointer alignment. */
    if (!robo_align_up(header, sizeof(void *), &base_off)) {
        return NULL;
    }
    if (capacity > SIZE_MAX - base_off) {
        return NULL;
    }
    total = base_off + capacity;

    block = (unsigned char *)robo_malloc(total);
    if (!block) {
        return NULL;
    }

    arena = (robo_arena_t *)(void *)block;
    arena->base = block + base_off;
    arena->capacity = capacity;
    arena->offset = 0;
    return arena;
}

void *robo_arena_alloc_aligned(robo_arena_t *arena, size_t size, size_t alignment)
{
    uintptr_t cur;
    uintptr_t aligned;
    size_t aligned_off;
    size_t end;

    if (!arena || size == 0) {
        return NULL;
    }
    if (!robo_is_pow2(alignment)) {
        return NULL;
    }

    cur = (uintptr_t)(arena->base + arena->offset);
    if (cur > UINTPTR_MAX - (alignment - 1)) {
        return NULL;
    }
    aligned = (cur + (alignment - 1)) & ~(uintptr_t)(alignment - 1);
    aligned_off = (size_t)(aligned - (uintptr_t)arena->base);

    if (aligned_off > arena->capacity) {
        return NULL;
    }
    if (size > arena->capacity - aligned_off) {
        return NULL;
    }
    end = aligned_off + size;
    arena->offset = end;
    return arena->base + aligned_off;
}

void *robo_arena_alloc(robo_arena_t *arena, size_t size)
{
    return robo_arena_alloc_aligned(arena, size, sizeof(void *));
}

void robo_arena_reset(robo_arena_t *arena)
{
    if (!arena) {
        return;
    }
    arena->offset = 0;
}

void robo_arena_destroy(robo_arena_t *arena)
{
    if (!arena) {
        return;
    }
    /* Header and payload share one allocation. */
    arena->base = NULL;
    arena->capacity = 0;
    arena->offset = 0;
    robo_free(arena);
}

size_t robo_arena_bytes_remaining(const robo_arena_t *arena)
{
    if (!arena || arena->offset >= arena->capacity) {
        return 0;
    }
    return arena->capacity - arena->offset;
}
