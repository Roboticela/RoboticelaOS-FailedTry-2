/*
 * RoboPool — Goal 2.3 fixed-size free-list pool.
 *
 * Storage (one or more Malloc regions):
 *   - pool header
 *   - freeStack[capacity] of size_t indices
 *   - inUse[capacity] bytes (1 = allocated)
 *   - slots[capacity * stride], base pointer-aligned / stronger if needed
 */

#include "RoboMem/RoboPool.h"
#include "RoboMem/RoboMem.h"

#include <stdint.h>

struct Pool {
    unsigned char *slots;
    size_t *freeStack;
    unsigned char *inUse;
    size_t objectSize;
    size_t stride;
    size_t alignment;
    size_t capacity;
    size_t freeTop;
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

static size_t PoolChooseAlign(size_t objectSize)
{
    size_t align = sizeof(void *);
    if (IsPow2(objectSize) && objectSize > align) {
        align = objectSize;
    }
    return align;
}

Pool *PoolCreate(size_t objectSize, size_t capacity)
{
    Pool *pool;
    size_t align;
    size_t payload;
    size_t stride;
    size_t stackBytes;
    size_t useBytes;
    size_t i;
    unsigned char *block;
    size_t header;
    size_t offStack;
    size_t offUse;
    size_t offSlots;
    size_t total;

    if (objectSize == 0 || capacity == 0) {
        return NULL;
    }

    align = PoolChooseAlign(objectSize);
    payload = objectSize < sizeof(void *) ? sizeof(void *) : objectSize;
    if (!AlignUp(payload, align, &stride)) {
        return NULL;
    }
    if (capacity > SIZE_MAX / stride) {
        return NULL;
    }

    stackBytes = capacity * sizeof(size_t);
    useBytes = capacity;

    header = sizeof(Pool);
    if (!AlignUp(header, sizeof(void *), &offStack)) {
        return NULL;
    }
    if (offStack > SIZE_MAX - stackBytes) {
        return NULL;
    }
    if (!AlignUp(offStack + stackBytes, sizeof(void *), &offUse)) {
        return NULL;
    }
    if (offUse > SIZE_MAX - useBytes) {
        return NULL;
    }
    if (!AlignUp(offUse + useBytes, sizeof(void *), &offSlots)) {
        return NULL;
    }
    /* Reserve (align-1) so we can bump slots to absolute alignment. */
    if (offSlots > SIZE_MAX - (align - 1)) {
        return NULL;
    }
    if (offSlots + (align - 1) > SIZE_MAX - capacity * stride) {
        return NULL;
    }
    total = offSlots + (align - 1) + capacity * stride;

    block = (unsigned char *)Malloc(total);
    if (!block) {
        return NULL;
    }

    pool = (Pool *)(void *)block;
    pool->freeStack = (size_t *)(void *)(block + offStack);
    pool->inUse = block + offUse;
    {
        uintptr_t raw = (uintptr_t)(block + offSlots);
        uintptr_t alignedSlots = (raw + (align - 1)) & ~(uintptr_t)(align - 1);
        pool->slots = (unsigned char *)alignedSlots;
    }
    pool->objectSize = objectSize;
    pool->stride = stride;
    pool->alignment = align;
    pool->capacity = capacity;
    pool->freeTop = capacity;

    for (i = 0; i < capacity; i++) {
        pool->freeStack[i] = capacity - 1 - i;
        pool->inUse[i] = 0;
    }

    return pool;
}

void *PoolAlloc(Pool *pool)
{
    size_t index;

    if (!pool || pool->freeTop == 0) {
        return NULL;
    }
    pool->freeTop--;
    index = pool->freeStack[pool->freeTop];
    pool->inUse[index] = 1;
    return pool->slots + index * pool->stride;
}

static int PoolIndexOf(const Pool *pool, void *ptr, size_t *outIndex)
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
    *outIndex = index;
    return 1;
}

Result PoolFree(Pool *pool, void *ptr)
{
    size_t index;

    if (!pool) {
        return RoboInvalidArgument;
    }
    if (!ptr) {
        return RoboInvalidArgument;
    }
    if (!PoolIndexOf(pool, ptr, &index)) {
        return RoboInvalidArgument;
    }
    if (!pool->inUse[index]) {
        return RoboAlreadyExists; /* double-free */
    }
    if (pool->freeTop >= pool->capacity) {
        return RoboError; /* internal consistency */
    }
    pool->inUse[index] = 0;
    pool->freeStack[pool->freeTop] = index;
    pool->freeTop++;
    return RoboOk;
}

void PoolReset(Pool *pool)
{
    size_t i;

    if (!pool) {
        return;
    }
    pool->freeTop = pool->capacity;
    for (i = 0; i < pool->capacity; i++) {
        pool->freeStack[i] = pool->capacity - 1 - i;
        pool->inUse[i] = 0;
    }
}

void PoolDestroy(Pool *pool)
{
    if (!pool) {
        return;
    }
    pool->slots = NULL;
    pool->freeStack = NULL;
    pool->inUse = NULL;
    pool->capacity = 0;
    pool->freeTop = 0;
    RoboFree(pool);
}

size_t PoolObjectSize(const Pool *pool)
{
    return pool ? pool->objectSize : 0;
}

size_t PoolCapacity(const Pool *pool)
{
    return pool ? pool->capacity : 0;
}

size_t PoolAvailable(const Pool *pool)
{
    return pool ? pool->freeTop : 0;
}
