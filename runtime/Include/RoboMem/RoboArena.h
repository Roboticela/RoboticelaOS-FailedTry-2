/*
 * RoboArena — bump / arena allocator (Goals 2.2 / 2.5).
 *
 * Use for temporary / parse-scoped bulk allocations freed together via reset
 * or destroy. Not for individually freed long-lived objects.
 *
 * Ownership: the arena object and its backing buffer are owned by the
 * creator until robo_arena_destroy. Pointers returned by alloc are valid
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

typedef struct robo_arena robo_arena_t;

/**
 * Create an arena with a fixed backing capacity (bytes).
 * @return new arena, or NULL if capacity == 0 or backing allocation fails.
 * Caller owns the arena until robo_arena_destroy.
 */
robo_arena_t *robo_arena_create(size_t capacity);

/**
 * Allocate @p size bytes with default alignment (sizeof(void *)).
 * size == 0 or arena == NULL → NULL.
 * Returns NULL if remaining capacity is insufficient (overflow-safe).
 */
void *robo_arena_alloc(robo_arena_t *arena, size_t size);

/**
 * Allocate @p size bytes aligned to @p alignment.
 * alignment must be a non-zero power of two; otherwise NULL.
 * size == 0 or arena == NULL → NULL.
 */
void *robo_arena_alloc_aligned(robo_arena_t *arena, size_t size, size_t alignment);

/**
 * Reset the bump pointer to the start. All prior arena allocations become
 * invalid. Safe on NULL (no-op).
 */
void robo_arena_reset(robo_arena_t *arena);

/**
 * Free backing memory and the arena object. Safe on NULL (no-op).
 * After destroy, the pointer must not be used again.
 */
void robo_arena_destroy(robo_arena_t *arena);

/** Bytes still available before the next alignment padding (approx remaining). */
size_t robo_arena_bytes_remaining(const robo_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_ARENA_H */
