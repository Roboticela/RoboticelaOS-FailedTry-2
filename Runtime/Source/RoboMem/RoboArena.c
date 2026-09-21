/*
 * RoboArena — Goal 2.2 bump allocator.
 *
 * Layout (one Malloc):
 *   [ Arena header | capacity bytes of bump storage ]
 */

#include "RoboMem/RoboArena.h"
#include "RoboMem/RoboMem.h"

#include <stdint.h>

struct Arena {
    unsigned char *base;
    size_t capacity;
    size_t offset;
};

static int IsPow2(size_t v)
{
    return v != 0 && (v & (v - 1)) == 0;
}

static int AlignUp(size_t value, size_t alignment, size_t *out)
{
    size_t add;
    size_t aligned;

    if (!IsPow2(alignment)) {
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

Arena *ArenaCreate(size_t capacity)
{
    size_t header;
    size_t total;
    unsigned char *block;
    Arena *arena;
    size_t baseOff;

    if (capacity == 0) {
        return NULL;
    }

    header = sizeof(Arena);
    /* Align payload start to max(default) pointer alignment. */
    if (!AlignUp(header, sizeof(void *), &baseOff)) {
        return NULL;
    }
    if (capacity > SIZE_MAX - baseOff) {
        return NULL;
    }
    total = baseOff + capacity;

    block = (unsigned char *)Malloc(total);
    if (!block) {
        return NULL;
    }

    arena = (Arena *)(void *)block;
    arena->base = block + baseOff;
    arena->capacity = capacity;
    arena->offset = 0;
    return arena;
}

void *ArenaAllocAligned(Arena *arena, size_t size, size_t alignment)
{
    uintptr_t cur;
    uintptr_t aligned;
    size_t alignedOff;
    size_t end;

    if (!arena || size == 0) {
        return NULL;
    }
    if (!IsPow2(alignment)) {
        return NULL;
    }

    cur = (uintptr_t)(arena->base + arena->offset);
    if (cur > UINTPTR_MAX - (alignment - 1)) {
        return NULL;
    }
    aligned = (cur + (alignment - 1)) & ~(uintptr_t)(alignment - 1);
    alignedOff = (size_t)(aligned - (uintptr_t)arena->base);

    if (alignedOff > arena->capacity) {
        return NULL;
    }
    if (size > arena->capacity - alignedOff) {
        return NULL;
    }
    end = alignedOff + size;
    arena->offset = end;
    return arena->base + alignedOff;
}

void *ArenaAlloc(Arena *arena, size_t size)
{
    return ArenaAllocAligned(arena, size, sizeof(void *));
}

void ArenaReset(Arena *arena)
{
    if (!arena) {
        return;
    }
    arena->offset = 0;
}

void ArenaDestroy(Arena *arena)
{
    if (!arena) {
        return;
    }
    /* Header and payload share one allocation. */
    arena->base = NULL;
    arena->capacity = 0;
    arena->offset = 0;
    RoboFree(arena);
}

size_t ArenaBytesRemaining(const Arena *arena)
{
    if (!arena || arena->offset >= arena->capacity) {
        return 0;
    }
    return arena->capacity - arena->offset;
}
