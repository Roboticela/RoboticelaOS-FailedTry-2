/*
 * RoboArray — dynamic contiguous array (Goal 3.1).
 *
 * Stores fixed-size elements by value in a growable buffer allocated via
 * RoboMem. Treat struct fields as private; use the accessors below.
 *
 * See containers/README.md.
 */

#ifndef ROBO_ARRAY_H
#define ROBO_ARRAY_H

#include "RoboBase/RoboError.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Array {
    unsigned char *data;
    size_t size;
    size_t capacity;
    size_t elementSize;
} Array;

/**
 * Initialize @p array for elements of @p elementSize bytes each.
 * Starts empty (size 0, capacity 0, data NULL).
 * @return RoboOk, or RoboInvalidArgument if array is NULL or elementSize 0.
 */
Result ArrayInit(Array *array, size_t elementSize);

/**
 * Free backing storage and reset to empty uninitialized-safe state.
 * Safe on NULL. After destroy, call init again before reuse.
 */
void ArrayDestroy(Array *array);

size_t ArraySize(const Array *array);
size_t ArrayCapacity(const Array *array);
size_t ArrayElementSize(const Array *array);

/** @return 1 if size == 0 (or array NULL), else 0. */
int ArrayEmpty(const Array *array);

/** Contiguous storage (NULL if capacity 0). */
void *ArrayData(Array *array);
const void *ArrayDataConst(const Array *array);

/**
 * Pointer to element at @p index, or NULL if out of range / NULL array.
 * Pointer is invalidated by push/reserve/clear/destroy that reallocate.
 */
void *ArrayGet(Array *array, size_t index);
const void *ArrayGetConst(const Array *array, size_t index);

/**
 * Copy @p element (elementSize bytes) into slot @p index.
 * @return RoboOk, RoboInvalidArgument (NULL/OOB), RoboNotFound unused.
 */
Result ArraySet(Array *array, size_t index, const void *element);

/**
 * Append a copy of @p element. Grows capacity as needed (geometric).
 * @return RoboOk, RoboInvalidArgument, RoboOutOfMemory.
 */
Result ArrayPush(Array *array, const void *element);

/**
 * Remove last element. If @p outElement is non-NULL, copy it there first.
 * @return RoboOk, RoboInvalidArgument (NULL array), RoboNotFound if empty.
 */
Result ArrayPop(Array *array, void *outElement);

/** Set size to 0; keeps capacity and buffer. Safe on NULL. */
void ArrayClear(Array *array);

/**
 * Ensure capacity >= @p capacity (element count). Never shrinks.
 * @return RoboOk, RoboInvalidArgument, RoboOutOfMemory.
 */
Result ArrayReserve(Array *array, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_ARRAY_H */
