/*
 * RoboPool — fixed-size object pool (Goals 2.3 / 2.5).
 *
 * Use for high-frequency equal-sized objects with individual free/reuse
 * (e.g. nodes, events). Not for irregular sizes or temporary bulk scope.
 *
 * Ownership: the pool is owned by the creator until PoolDestroy.
 * Objects from PoolAlloc are owned by the caller until
 * PoolFree / reset / destroy.
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

typedef struct Pool Pool;

/**
 * Create a pool of @p capacity objects of @p objectSize bytes each.
 * Zero size or capacity → NULL. Backing storage from RoboMem.
 */
Pool *PoolCreate(size_t objectSize, size_t capacity);

/**
 * Allocate one free slot. NULL if pool is NULL or exhausted.
 * Returned pointer is aligned per pool stride rules (see docs).
 */
void *PoolAlloc(Pool *pool);

/**
 * Return a slot to the pool.
 * @return RoboOk on success
 * @return RoboInvalidArgument if pool/ptr invalid or ptr not from this pool
 * @return RoboAlreadyExists on double-free
 */
Result PoolFree(Pool *pool, void *ptr);

/** Mark all slots free. Prior object pointers become invalid. NULL → no-op. */
void PoolReset(Pool *pool);

/** Destroy pool and backing memory. NULL → no-op. */
void PoolDestroy(Pool *pool);

size_t PoolObjectSize(const Pool *pool);
size_t PoolCapacity(const Pool *pool);
size_t PoolAvailable(const Pool *pool);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_POOL_H */
