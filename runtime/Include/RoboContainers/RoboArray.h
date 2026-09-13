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

typedef struct robo_array {
    unsigned char *data;
    size_t size;
    size_t capacity;
    size_t element_size;
} robo_array_t;

/**
 * Initialize @p array for elements of @p element_size bytes each.
 * Starts empty (size 0, capacity 0, data NULL).
 * @return ROBO_OK, or ROBO_INVALID_ARGUMENT if array is NULL or element_size 0.
 */
robo_result_t robo_array_init(robo_array_t *array, size_t element_size);

/**
 * Free backing storage and reset to empty uninitialized-safe state.
 * Safe on NULL. After destroy, call init again before reuse.
 */
void robo_array_destroy(robo_array_t *array);

size_t robo_array_size(const robo_array_t *array);
size_t robo_array_capacity(const robo_array_t *array);
size_t robo_array_element_size(const robo_array_t *array);

/** @return 1 if size == 0 (or array NULL), else 0. */
int robo_array_empty(const robo_array_t *array);

/** Contiguous storage (NULL if capacity 0). */
void *robo_array_data(robo_array_t *array);
const void *robo_array_data_const(const robo_array_t *array);

/**
 * Pointer to element at @p index, or NULL if out of range / NULL array.
 * Pointer is invalidated by push/reserve/clear/destroy that reallocate.
 */
void *robo_array_get(robo_array_t *array, size_t index);
const void *robo_array_get_const(const robo_array_t *array, size_t index);

/**
 * Copy @p element (element_size bytes) into slot @p index.
 * @return ROBO_OK, ROBO_INVALID_ARGUMENT (NULL/OOB), ROBO_NOT_FOUND unused.
 */
robo_result_t robo_array_set(robo_array_t *array, size_t index, const void *element);

/**
 * Append a copy of @p element. Grows capacity as needed (geometric).
 * @return ROBO_OK, ROBO_INVALID_ARGUMENT, ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_array_push(robo_array_t *array, const void *element);

/**
 * Remove last element. If @p out_element is non-NULL, copy it there first.
 * @return ROBO_OK, ROBO_INVALID_ARGUMENT (NULL array), ROBO_NOT_FOUND if empty.
 */
robo_result_t robo_array_pop(robo_array_t *array, void *out_element);

/** Set size to 0; keeps capacity and buffer. Safe on NULL. */
void robo_array_clear(robo_array_t *array);

/**
 * Ensure capacity >= @p capacity (element count). Never shrinks.
 * @return ROBO_OK, ROBO_INVALID_ARGUMENT, ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_array_reserve(robo_array_t *array, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_ARRAY_H */
