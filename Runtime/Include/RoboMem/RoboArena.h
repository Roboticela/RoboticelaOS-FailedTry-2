/*
 * RoboArena — bump / arena allocator (Goals 2.2 / 2.5).
 *
 * Use for temporary / parse-scoped bulk allocations freed together via reset
 * or destroy. Not for individually freed long-lived objects.
 *
 * Ownership: the arena object and its backing buffer are owned by the
 * creator until ArenaDestroy. Pointers returned by alloc are valid
 * until the next reset or destroy — there is no per-block free.
 *
 * See memory/README.md.
 */

#ifndef ROBO_ARENA_H
#define ROBO_ARENA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Arena Arena;

/**
 * Create an arena with a fixed backing capacity (bytes).
 * @return new arena, or NULL if capacity == 0 or backing allocation fails.
 * Caller owns the arena until ArenaDestroy.
 */
Arena *ArenaCreate(size_t capacity);

/**
 * Allocate @p size bytes with default alignment (sizeof(void *)).
 * size == 0 or arena == NULL → NULL.
 * Returns NULL if remaining capacity is insufficient (overflow-safe).
 */
void *ArenaAlloc(Arena *arena, size_t size);

/**
 * Allocate @p size bytes aligned to @p alignment.
 * alignment must be a non-zero power of two; otherwise NULL.
 * size == 0 or arena == NULL → NULL.
 */
void *ArenaAllocAligned(Arena *arena, size_t size, size_t alignment);

/**
 * Reset the bump pointer to the start. All prior arena allocations become
 * invalid. Safe on NULL (no-op).
 */
void ArenaReset(Arena *arena);

/**
 * Free backing memory and the arena object. Safe on NULL (no-op).
 * After destroy, the pointer must not be used again.
 */
void ArenaDestroy(Arena *arena);

/** Bytes still available before the next alignment padding (approx remaining). */
size_t ArenaBytesRemaining(const Arena *arena);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_ARENA_H */
