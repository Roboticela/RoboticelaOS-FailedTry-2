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

typedef enum FontSourceKind {
    RoboFontSourceMemoryCopy = 1,
    RoboFontSourceMemoryBorrow = 2
} FontSourceKind;

typedef struct FontSource FontSource;

/**
 * Create a source that copies @p bytes[0..size).
 * Empty size → RoboInvalidArgument. OOM → RoboOutOfMemory.
 */
Result FontSourceCreateMemoryCopy(const void *bytes,
                                                  size_t size,
                                                  FontSource **out);

/**
 * Create a non-owning view of @p bytes[0..size).
 * Caller guarantees lifetime until FontSourceRelease drops last ref
 * (including faces holding the source).
 */
Result FontSourceCreateMemoryBorrow(const void *bytes,
                                                    size_t size,
                                                    FontSource **out);

/** Increment reference count. NULL is a no-op. */
void FontSourceRetain(FontSource *src);

/**
 * Decrement reference count; frees when it reaches zero.
 * NULL is a no-op. After last release the pointer is invalid.
 */
void FontSourceRelease(FontSource *src);

FontSourceKind MakeFontSourceKind(const FontSource *src);
size_t FontSourceSize(const FontSource *src);

/**
 * Pointer to immutable bytes. Valid while @p src is alive.
 * NULL source → NULL.
 */
const uint8_t *FontSourceBytes(const FontSource *src);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_SOURCE_H */
