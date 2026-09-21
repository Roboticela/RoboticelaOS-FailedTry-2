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

#define RoboArrayMinCap 8u

static size_t ArrayBytes(size_t count, size_t elementSize)
{
    if (count == 0) {
        return 0;
    }
    if (elementSize == 0 || count > SIZE_MAX / elementSize) {
        return 0; /* signal overflow to caller via 0 when count > 0 */
    }
    return count * elementSize;
}

static int ArrayBytesOverflow(size_t count, size_t elementSize)
{
    if (count == 0) {
        return 0;
    }
    return elementSize == 0 || count > SIZE_MAX / elementSize;
}

static unsigned char *ArraySlot(Array *array, size_t index)
{
    return array->data + index * array->elementSize;
}

Result ArrayInit(Array *array, size_t elementSize)
{
    if (!array || elementSize == 0) {
        return RoboInvalidArgument;
    }
    array->data = NULL;
    array->size = 0;
    array->capacity = 0;
    array->elementSize = elementSize;
    return RoboOk;
}

void ArrayDestroy(Array *array)
{
    if (!array) {
        return;
    }
    RoboFree(array->data);
    array->data = NULL;
    array->size = 0;
    array->capacity = 0;
    array->elementSize = 0;
}

size_t ArraySize(const Array *array)
{
    return array ? array->size : 0;
}

size_t ArrayCapacity(const Array *array)
{
    return array ? array->capacity : 0;
}

size_t ArrayElementSize(const Array *array)
{
    return array ? array->elementSize : 0;
}

int ArrayEmpty(const Array *array)
{
    return !array || array->size == 0;
}

void *ArrayData(Array *array)
{
    return array ? (void *)array->data : NULL;
}

const void *ArrayDataConst(const Array *array)
{
    return array ? (const void *)array->data : NULL;
}

void *ArrayGet(Array *array, size_t index)
{
    if (!array || index >= array->size || !array->data) {
        return NULL;
    }
    return ArraySlot(array, index);
}

const void *ArrayGetConst(const Array *array, size_t index)
{
    if (!array || index >= array->size || !array->data) {
        return NULL;
    }
    return array->data + index * array->elementSize;
}

Result ArraySet(Array *array, size_t index, const void *element)
{
    if (!array || !element || array->elementSize == 0) {
        return RoboInvalidArgument;
    }
    if (index >= array->size || !array->data) {
        return RoboInvalidArgument;
    }
    memcpy(ArraySlot(array, index), element, array->elementSize);
    return RoboOk;
}

static Result ArrayGrowTo(Array *array, size_t newCap)
{
    size_t bytes;
    void *neu;

    if (newCap < array->capacity) {
        return RoboOk;
    }
    if (ArrayBytesOverflow(newCap, array->elementSize)) {
        return RoboOutOfMemory; /* treat overflow as unsatisfiable */
    }
    bytes = ArrayBytes(newCap, array->elementSize);
    neu = Realloc(array->data, bytes);
    if (!neu) {
        return RoboOutOfMemory;
    }
    array->data = (unsigned char *)neu;
    array->capacity = newCap;
    return RoboOk;
}

static size_t ArrayNextCapacity(size_t current, size_t needed)
{
    size_t cap;

    if (needed < RoboArrayMinCap) {
        cap = RoboArrayMinCap;
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

Result ArrayReserve(Array *array, size_t capacity)
{
    if (!array || array->elementSize == 0) {
        return RoboInvalidArgument;
    }
    if (capacity <= array->capacity) {
        return RoboOk;
    }
    return ArrayGrowTo(array, capacity);
}

Result ArrayPush(Array *array, const void *element)
{
    Result r;
    size_t needed;
    size_t newCap;

    if (!array || !element || array->elementSize == 0) {
        return RoboInvalidArgument;
    }
    if (array->size == SIZE_MAX) {
        return RoboOutOfMemory;
    }
    needed = array->size + 1;
    if (needed > array->capacity) {
        newCap = ArrayNextCapacity(array->capacity, needed);
        if (ArrayBytesOverflow(newCap, array->elementSize)) {
            /* Fall back to exact needed if geometric overflowed. */
            if (ArrayBytesOverflow(needed, array->elementSize)) {
                return RoboOutOfMemory;
            }
            newCap = needed;
        }
        r = ArrayGrowTo(array, newCap);
        if (r != RoboOk) {
            return r;
        }
    }
    memcpy(ArraySlot(array, array->size), element, array->elementSize);
    array->size++;
    return RoboOk;
}

Result ArrayPop(Array *array, void *outElement)
{
    if (!array || array->elementSize == 0) {
        return RoboInvalidArgument;
    }
    if (array->size == 0) {
        return RoboNotFound;
    }
    array->size--;
    if (outElement) {
        memcpy(outElement, ArraySlot(array, array->size), array->elementSize);
    }
    return RoboOk;
}

void ArrayClear(Array *array)
{
    if (!array) {
        return;
    }
    array->size = 0;
}
