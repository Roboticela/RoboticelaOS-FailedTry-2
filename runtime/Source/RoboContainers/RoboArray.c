/*
 * RoboArray — Goal 3.1 dynamic array.
 *
 * Growth: geometric ×2 from a minimum of 8 elements (overflow-safe).
 * Elements are memcpy'd by value; the array owns only the contiguous buffer.
 */

#include "RoboContainers/RoboArray.h"
#include "RoboMem/RoboMem.h"

#include <stdint.h>
#include <string.h>

#define ROBO_ARRAY_MIN_CAP 8u

static size_t robo_array_bytes(size_t count, size_t element_size)
{
    if (count == 0) {
        return 0;
    }
    if (element_size == 0 || count > SIZE_MAX / element_size) {
        return 0; /* signal overflow to caller via 0 when count > 0 */
    }
    return count * element_size;
}

static int robo_array_bytes_overflow(size_t count, size_t element_size)
{
    if (count == 0) {
        return 0;
    }
    return element_size == 0 || count > SIZE_MAX / element_size;
}

static unsigned char *robo_array_slot(robo_array_t *array, size_t index)
{
    return array->data + index * array->element_size;
}

robo_result_t robo_array_init(robo_array_t *array, size_t element_size)
{
    if (!array || element_size == 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    array->data = NULL;
    array->size = 0;
    array->capacity = 0;
    array->element_size = element_size;
    return ROBO_OK;
}

void robo_array_destroy(robo_array_t *array)
{
    if (!array) {
        return;
    }
    robo_free(array->data);
    array->data = NULL;
    array->size = 0;
    array->capacity = 0;
    array->element_size = 0;
}

size_t robo_array_size(const robo_array_t *array)
{
    return array ? array->size : 0;
}

size_t robo_array_capacity(const robo_array_t *array)
{
    return array ? array->capacity : 0;
}

size_t robo_array_element_size(const robo_array_t *array)
{
    return array ? array->element_size : 0;
}

int robo_array_empty(const robo_array_t *array)
{
    return !array || array->size == 0;
}

void *robo_array_data(robo_array_t *array)
{
    return array ? (void *)array->data : NULL;
}

const void *robo_array_data_const(const robo_array_t *array)
{
    return array ? (const void *)array->data : NULL;
}

void *robo_array_get(robo_array_t *array, size_t index)
{
    if (!array || index >= array->size || !array->data) {
        return NULL;
    }
    return robo_array_slot(array, index);
}

const void *robo_array_get_const(const robo_array_t *array, size_t index)
{
    if (!array || index >= array->size || !array->data) {
        return NULL;
    }
    return array->data + index * array->element_size;
}

robo_result_t robo_array_set(robo_array_t *array, size_t index, const void *element)
{
    if (!array || !element || array->element_size == 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (index >= array->size || !array->data) {
        return ROBO_INVALID_ARGUMENT;
    }
    memcpy(robo_array_slot(array, index), element, array->element_size);
    return ROBO_OK;
}

static robo_result_t robo_array_grow_to(robo_array_t *array, size_t new_cap)
{
    size_t bytes;
    void *neu;

    if (new_cap < array->capacity) {
        return ROBO_OK;
    }
    if (robo_array_bytes_overflow(new_cap, array->element_size)) {
        return ROBO_OUT_OF_MEMORY; /* treat overflow as unsatisfiable */
    }
    bytes = robo_array_bytes(new_cap, array->element_size);
    neu = robo_realloc(array->data, bytes);
    if (!neu) {
        return ROBO_OUT_OF_MEMORY;
    }
    array->data = (unsigned char *)neu;
    array->capacity = new_cap;
    return ROBO_OK;
}

static size_t robo_array_next_capacity(size_t current, size_t needed)
{
    size_t cap;

    if (needed < ROBO_ARRAY_MIN_CAP) {
        cap = ROBO_ARRAY_MIN_CAP;
    } else {
        cap = needed;
    }
    if (current == 0) {
        return cap;
    }
    /* Geometric growth: repeatedly double until >= needed (overflow-safe). */
    while (current < needed) {
        if (current > SIZE_MAX / 2u) {
            return needed; /* cannot double; jump to exact if possible */
        }
        current *= 2u;
    }
    if (current < cap) {
        current = cap;
    }
    return current;
}

robo_result_t robo_array_reserve(robo_array_t *array, size_t capacity)
{
    if (!array || array->element_size == 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (capacity <= array->capacity) {
        return ROBO_OK;
    }
    return robo_array_grow_to(array, capacity);
}

robo_result_t robo_array_push(robo_array_t *array, const void *element)
{
    robo_result_t r;
    size_t needed;
    size_t new_cap;

    if (!array || !element || array->element_size == 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (array->size == SIZE_MAX) {
        return ROBO_OUT_OF_MEMORY;
    }
    needed = array->size + 1;
    if (needed > array->capacity) {
        new_cap = robo_array_next_capacity(array->capacity, needed);
        if (robo_array_bytes_overflow(new_cap, array->element_size)) {
            /* Fall back to exact needed if geometric overflowed. */
            if (robo_array_bytes_overflow(needed, array->element_size)) {
                return ROBO_OUT_OF_MEMORY;
            }
            new_cap = needed;
        }
        r = robo_array_grow_to(array, new_cap);
        if (r != ROBO_OK) {
            return r;
        }
    }
    memcpy(robo_array_slot(array, array->size), element, array->element_size);
    array->size++;
    return ROBO_OK;
}

robo_result_t robo_array_pop(robo_array_t *array, void *out_element)
{
    if (!array || array->element_size == 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (array->size == 0) {
        return ROBO_NOT_FOUND;
    }
    array->size--;
    if (out_element) {
        memcpy(out_element, robo_array_slot(array, array->size), array->element_size);
    }
    return ROBO_OK;
}

void robo_array_clear(robo_array_t *array)
{
    if (!array) {
        return;
    }
    array->size = 0;
}
