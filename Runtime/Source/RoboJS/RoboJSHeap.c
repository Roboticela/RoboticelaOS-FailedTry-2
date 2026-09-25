/*
 * The RoboJS heap (see `RoboJSHeap.h`).
 *
 * A singly-linked list of chunks with a bump pointer, aligned to 16 bytes so every value, object and
 * environment is safe to place on any of them. `JsHeapReset` walks the list and frees it, which is
 * the whole of this module's reclamation: nothing here frees a single object, and that is why a
 * cyclic object graph costs nothing to keep and nothing to drop.
 */

#include "RoboJS/RoboJSHeap.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

/** Every allocation starts on a 16-byte boundary, so a chunk's data must start on one too. */
#define JsHeapAlign 16u

static size_t JsHeapAlignUp(size_t size)
{
    return (size + (JsHeapAlign - 1u)) & ~((size_t)JsHeapAlign - 1u);
}

static int JsHeapAddChunk(JsHeap *heap, size_t minimum)
{
    JsHeapChunk *chunk;
    size_t bytes;

    bytes = heap->chunkBytes != 0 ? heap->chunkBytes : RoboJsDefaultHeapChunkBytes;
    if (minimum > bytes) {
        bytes = JsHeapAlignUp(minimum);
    }
    chunk = (JsHeapChunk *)RoboCalloc(1, sizeof(JsHeapChunk));
    if (chunk == NULL) {
        return 0;
    }
    chunk->data = (unsigned char *)RoboCalloc(1, bytes);
    if (chunk->data == NULL) {
        RoboFree(chunk);
        return 0;
    }
    chunk->size = bytes;
    chunk->used = 0;
    chunk->next = heap->current;
    heap->current = chunk;
    heap->chunkCount += 1;
    heap->totalBytes += bytes;
    return 1;
}

JsHeap *JsHeapCreate(size_t chunkBytes)
{
    JsHeap *heap;

    heap = (JsHeap *)RoboCalloc(1, sizeof(JsHeap));
    if (heap == NULL) {
        return NULL;
    }
    heap->chunkBytes = chunkBytes;
    heap->maxProperties = (int32_t)RoboJsDefaultMaxProperties;
    heap->maxStringLength = (int32_t)RoboJsDefaultMaxStringLength;
    if (!JsHeapAddChunk(heap, 0)) {
        RoboFree(heap);
        return NULL;
    }
    return heap;
}

void JsHeapDestroy(JsHeap *heap)
{
    JsHeapChunk *chunk;

    if (heap == NULL) {
        return;
    }
    chunk = heap->current;
    while (chunk != NULL) {
        JsHeapChunk *next = chunk->next;

        RoboFree(chunk->data);
        RoboFree(chunk);
        chunk = next;
    }
    RoboFree(heap);
}

Result JsHeapReset(JsHeap *heap)
{
    size_t chunkBytes;
    JsHeapChunk *chunk;

    if (heap == NULL) {
        return RoboInvalidArgument;
    }
    chunkBytes = heap->chunkBytes;
    chunk = heap->current;
    while (chunk != NULL) {
        JsHeapChunk *next = chunk->next;

        RoboFree(chunk->data);
        RoboFree(chunk);
        chunk = next;
    }
    heap->current = NULL;
    heap->totalBytes = 0;
    heap->liveBytes = 0;
    heap->allocationCount = 0;
    heap->chunkCount = 0;
    heap->chunkBytes = chunkBytes;
    /* The limits survive a reset: they are the caller's policy, not the script's state. */
    if (!JsHeapAddChunk(heap, 0)) {
        return RoboOutOfMemory;
    }
    return RoboOk;
}

void *JsHeapAlloc(JsHeap *heap, size_t size)
{
    void *result;

    if (heap == NULL || size == 0) {
        return NULL;
    }
    size = JsHeapAlignUp(size);
    if (heap->current == NULL || heap->current->used + size > heap->current->size) {
        if (!JsHeapAddChunk(heap, size)) {
            return NULL;
        }
    }
    result = (void *)(heap->current->data + heap->current->used);
    heap->current->used += size;
    heap->liveBytes += size;
    heap->allocationCount += 1;
    return result;
}

void *JsHeapAllocArray(JsHeap *heap, size_t count, size_t elementSize)
{
    if (count == 0 || elementSize == 0) {
        return NULL;
    }
    if (count > ((size_t)-1) / elementSize) {
        return NULL;
    }
    return JsHeapAlloc(heap, count * elementSize);
}

void *JsHeapRealloc(JsHeap *heap, const void *previous, size_t previousSize, size_t size)
{
    void *fresh;

    if (heap == NULL || size == 0) {
        return NULL;
    }
    fresh = JsHeapAlloc(heap, size);
    if (fresh == NULL) {
        return NULL;
    }
    if (previous != NULL && previousSize != 0) {
        memcpy(fresh, previous, previousSize < size ? previousSize : size);
    }
    return fresh;
}

Result JsHeapSetLimits(JsHeap *heap, int32_t maxProperties, int32_t maxStringLength)
{
    if (heap == NULL) {
        return RoboInvalidArgument;
    }
    heap->maxProperties = maxProperties;
    heap->maxStringLength = maxStringLength;
    return RoboOk;
}

int32_t JsHeapMaxProperties(const JsHeap *heap)
{
    if (heap == NULL || heap->maxProperties <= 0) {
        return (int32_t)RoboJsDefaultMaxProperties;
    }
    return heap->maxProperties;
}

int32_t JsHeapMaxStringLength(const JsHeap *heap)
{
    if (heap == NULL || heap->maxStringLength <= 0) {
        return (int32_t)RoboJsDefaultMaxStringLength;
    }
    return heap->maxStringLength;
}

const char *JsHeapString(JsHeap *heap, const char *utf8, int32_t length)
{
    char *copy;
    size_t bytes;

    if (length < 0) {
        bytes = utf8 != NULL ? strlen(utf8) : 0u;
    } else {
        bytes = (size_t)length;
    }
    if (utf8 == NULL) {
        bytes = 0;
    }
    if (bytes > (size_t)JsHeapMaxStringLength(heap)) {
        /* The cap is a policy, not a suggestion: a caller that asked for a megabyte string gets
         * NULL and can report a RangeError, rather than a heap that grew because a script said so. */
        return NULL;
    }
    copy = (char *)JsHeapAlloc(heap, bytes + 1u);
    if (copy == NULL) {
        return NULL;
    }
    if (bytes != 0) {
        memcpy(copy, utf8, bytes);
    }
    copy[bytes] = '\0';
    return copy;
}

size_t JsHeapLiveBytes(const JsHeap *heap)
{
    return heap != NULL ? heap->liveBytes : 0u;
}

size_t JsHeapAllocationCount(const JsHeap *heap)
{
    return heap != NULL ? heap->allocationCount : 0u;
}

size_t JsHeapChunkCount(const JsHeap *heap)
{
    return heap != NULL ? heap->chunkCount : 0u;
}
