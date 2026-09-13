/*
 * RoboFont data source abstraction (Goal 5.1).
 *
 * Platform-independent byte provider. Goal 5.1 implements memory-backed
 * sources only (copy or borrow). Filesystem / app-resource / OS managers
 * are later goals — do not call platform font APIs here.
 *
 * Ownership:
 *   COPY  — source owns a heap copy; safe after caller frees original.
 *   BORROW — non-owning view; caller must keep bytes alive until source destroy
 *            (and until any faces that retain the source are destroyed).
 *
 * Thread safety: immutable after create. Concurrent retain/release requires
 * external synchronization (refcount is not atomic in Goal 5.1).
 */

#ifndef ROBO_FONT_SOURCE_H
#define ROBO_FONT_SOURCE_H

#include "RoboBase/RoboError.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum robo_font_source_kind {
    ROBO_FONT_SOURCE_MEMORY_COPY = 1,
    ROBO_FONT_SOURCE_MEMORY_BORROW = 2
} robo_font_source_kind_t;

typedef struct robo_font_source robo_font_source_t;

/**
 * Create a source that copies @p bytes[0..size).
 * Empty size → ROBO_INVALID_ARGUMENT. OOM → ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_font_source_create_memory_copy(const void *bytes,
                                                  size_t size,
                                                  robo_font_source_t **out);

/**
 * Create a non-owning view of @p bytes[0..size).
 * Caller guarantees lifetime until robo_font_source_release drops last ref
 * (including faces holding the source).
 */
robo_result_t robo_font_source_create_memory_borrow(const void *bytes,
                                                    size_t size,
                                                    robo_font_source_t **out);

/** Increment reference count. NULL is a no-op. */
void robo_font_source_retain(robo_font_source_t *src);

/**
 * Decrement reference count; frees when it reaches zero.
 * NULL is a no-op. After last release the pointer is invalid.
 */
void robo_font_source_release(robo_font_source_t *src);

robo_font_source_kind_t robo_font_source_kind(const robo_font_source_t *src);
size_t robo_font_source_size(const robo_font_source_t *src);

/**
 * Pointer to immutable bytes. Valid while @p src is alive.
 * NULL source → NULL.
 */
const uint8_t *robo_font_source_bytes(const robo_font_source_t *src);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_SOURCE_H */
