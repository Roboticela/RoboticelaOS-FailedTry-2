/*
 * RoboPool — fixed-size object pool (Goals 2.3 / 2.5).
 *
 * Use for high-frequency equal-sized objects with individual free/reuse
 * (e.g. nodes, events). Not for irregular sizes or temporary bulk scope.
 *
 * Ownership: the pool is owned by the creator until robo_pool_destroy.
 * Objects from robo_pool_alloc are owned by the caller until
 * robo_pool_free / reset / destroy.
 *
 * See memory/README.md.
 */

#ifndef ROBO_POOL_H
#define ROBO_POOL_H

#include "RoboBase/RoboError.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_pool robo_pool_t;

/**
 * Create a pool of @p capacity objects of @p object_size bytes each.
 * Zero size or capacity → NULL. Backing storage from RoboMem.
 */
robo_pool_t *robo_pool_create(size_t object_size, size_t capacity);

/**
 * Allocate one free slot. NULL if pool is NULL or exhausted.
 * Returned pointer is aligned per pool stride rules (see docs).
 */
void *robo_pool_alloc(robo_pool_t *pool);

/**
 * Return a slot to the pool.
 * @return ROBO_OK on success
 * @return ROBO_INVALID_ARGUMENT if pool/ptr invalid or ptr not from this pool
 * @return ROBO_ALREADY_EXISTS on double-free
 */
robo_result_t robo_pool_free(robo_pool_t *pool, void *ptr);

/** Mark all slots free. Prior object pointers become invalid. NULL → no-op. */
void robo_pool_reset(robo_pool_t *pool);

/** Destroy pool and backing memory. NULL → no-op. */
void robo_pool_destroy(robo_pool_t *pool);

size_t robo_pool_object_size(const robo_pool_t *pool);
size_t robo_pool_capacity(const robo_pool_t *pool);
size_t robo_pool_available(const robo_pool_t *pool);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_POOL_H */
