/*
 * RoboMem — heap API (Goals 2.1 / 2.4 / 2.5).
 *
 * Selection: general-purpose heap. Prefer RoboArena for temporary bulk work,
 * RoboPool for fixed-size high-churn objects. See memory/README.md.
 *
 * When ROBO_MEM_DEBUG=1, macros capture __FILE__/__LINE__ for tracking.
 * Define ROBO_MEM_IMPLEMENTATION in RoboMem.c before including this header
 * so the implementation is not macro-wrapped.
 */

#ifndef ROBO_MEM_H
#define ROBO_MEM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ROBO_MEM_DEBUG
#define ROBO_MEM_DEBUG 0
#endif

void *robo_malloc_at(size_t size, const char *file, int line);
void *robo_calloc_at(size_t count, size_t size, const char *file, int line);
void *robo_realloc_at(void *ptr, size_t size, const char *file, int line);
void robo_free_at(void *ptr, const char *file, int line);

void *robo_malloc(size_t size);
void *robo_calloc(size_t count, size_t size);
void *robo_realloc(void *ptr, size_t size);
void robo_free(void *ptr);

void *robo_mem_copy(void *dst, const void *src, size_t n);
void *robo_mem_move(void *dst, const void *src, size_t n);
void *robo_mem_set(void *dst, int value, size_t n);
int robo_mem_compare(const void *a, const void *b, size_t n);

#if ROBO_MEM_DEBUG && !defined(ROBO_MEM_IMPLEMENTATION)
#define robo_malloc(sz) robo_malloc_at((sz), __FILE__, __LINE__)
#define robo_calloc(c, s) robo_calloc_at((c), (s), __FILE__, __LINE__)
#define robo_realloc(p, s) robo_realloc_at((p), (s), __FILE__, __LINE__)
#define robo_free(p) robo_free_at((p), __FILE__, __LINE__)
#endif

#ifdef __cplusplus
}
#endif

#endif /* ROBO_MEM_H */
