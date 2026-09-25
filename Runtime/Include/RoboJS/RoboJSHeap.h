/*
 * RoboJS heap (Phase 16.1) — one runtime's owned memory, freed as a whole.
 *
 * ## Why there is no per-object free
 *
 * A JavaScript object graph is cyclic by nature: a closure captures the environment that holds the
 * function; a DOM wrapper holds a listener whose closure holds the wrapper. Reference counting
 * cannot free that, and a tracing collector is a phase of its own. RoboJS therefore states its
 * strategy rather than leaving it undefined: **the heap is a chunked bump allocator whose entire
 * contents are released at once**, on `JsHeapReset` or `JsHeapDestroy`.
 *
 * What that buys is that no cycle is ever a leak and no collection can run in the middle of a
 * shaping call; what it costs is that memory grows until the runtime is reset. `Documentation/
 * RoboJS/Memory.md` states that trade and `JsHeapBytes`/`JsHeapAllocationCount` are the two numbers
 * a caller can watch to decide when a reset is due.
 *
 * ## Reallocation is an abandon, not a move
 *
 * `JsHeapRealloc` allocates a fresh block and copies. The old block stays in its chunk, dead, until
 * the reset that frees the chunk. That is deliberate: a bump allocator cannot grow a block in place
 * without invalidating every pointer after it, and *nothing in this module may hold a pointer into
 * a reallocating array*. A caller that needs a stable pointer across growth must allocate the
 * array's storage once, at a capacity it can state.
 */

#ifndef ROBO_JS_HEAP_H
#define ROBO_JS_HEAP_H

#include "RoboBase/RoboError.h"
#include "RoboJS/RoboJSTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A chunk of the bump allocator. Private to the implementation's shape, public for stack use. */
typedef struct JsHeapChunk {
    struct JsHeapChunk *next;
    size_t size;
    size_t used;
    unsigned char *data;
} JsHeapChunk;

typedef struct JsHeap {
    JsHeapChunk *current;
    size_t chunkBytes;
    size_t totalBytes;
    size_t liveBytes;
    size_t allocationCount;
    size_t chunkCount;
    /**
     * Per-allocation policies the runtime owns, read here because every allocation goes through
     * here anyway. `maxProperties` bounds one object's property count and `maxStringLength` one
     * string's bytes; 0 means the module's default. A heap on its own (a value test) keeps both 0.
     */
    int32_t maxProperties;
    int32_t maxStringLength;
} JsHeap;

/**
 * Create a heap whose first chunk holds @p chunkBytes (0 → `RoboJsDefaultHeapChunkBytes`).
 * @return NULL when the chunk allocation fails.
 */
JsHeap *JsHeapCreate(size_t chunkBytes);

/** Release every chunk. Safe on NULL. */
void JsHeapDestroy(JsHeap *heap);

/**
 * Release every chunk's contents but keep the heap usable — the one lifecycle door (16.1's reset).
 * @return RoboOk; RoboInvalidArgument on NULL.
 */
Result JsHeapReset(JsHeap *heap);

/**
 * Allocate @p size bytes, 16-byte aligned, zeroed. @p size 0 or a NULL heap → NULL.
 * Returns NULL when the allocation fails; the heap is unchanged by a failure.
 */
void *JsHeapAlloc(JsHeap *heap, size_t size);

/**
 * Array-shaped helper: @p count elements of @p elementSize, zeroed and overflow-checked.
 * Returns NULL for a zero element size or a size that would overflow `size_t`.
 */
void *JsHeapAllocArray(JsHeap *heap, size_t count, size_t elementSize);

/**
 * Allocate a fresh @p size-byte block and copy @p previous into it. @p previous may be NULL.
 * The old block is abandoned in place (see the header comment), never freed individually.
 */
void *JsHeapRealloc(JsHeap *heap, const void *previous, size_t previousSize, size_t size);

/**
 * Copy @p length bytes of @p utf8 into the heap with a trailing NUL, and answer the copy.
 * @p length < 0 means "strlen". A NULL @p utf8 answers an empty string rather than NULL, so no
 * caller has to test for NULL before using a JavaScript string.
 */
const char *JsHeapString(JsHeap *heap, const char *utf8, int32_t length);

/**
 * Set the two per-allocation policies. 0 or a negative value restores the module default. Called by
 * `JsRuntimeCreate` from the runtime's configuration.
 * @return RoboOk; RoboInvalidArgument on NULL.
 */
Result JsHeapSetLimits(JsHeap *heap, int32_t maxProperties, int32_t maxStringLength);

/** The effective property cap of @p heap (never 0). */
int32_t JsHeapMaxProperties(const JsHeap *heap);
/** The effective string-byte cap of @p heap (never 0). */
int32_t JsHeapMaxStringLength(const JsHeap *heap);

/** Bytes handed out since creation (or since the last reset). */
size_t JsHeapLiveBytes(const JsHeap *heap);

/** Live string/object allocations since creation (or since the last reset). */
size_t JsHeapAllocationCount(const JsHeap *heap);

/** Number of chunks the heap is holding — the one number that shows growth without a reset. */
size_t JsHeapChunkCount(const JsHeap *heap);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_JS_HEAP_H */
