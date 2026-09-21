/*
 * RoboMem — heap API (Goals 2.1 / 2.4 / 2.5).
 *
 * Selection: general-purpose heap. Prefer RoboArena for temporary bulk work,
 * RoboPool for fixed-size high-churn objects. See memory/README.md.
 *
 * When RoboMemDebug=1, macros capture __FILE__/__LINE__ for tracking.
 * Define RoboMemImplementation in RoboMem.c before including this header
 * so the implementation is not macro-wrapped.
 */

#ifndef ROBO_MEM_H
#define ROBO_MEM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RoboMemDebug
#define RoboMemDebug 0
#endif

void *MallocAt(size_t size, const char *file, int line);
void *CallocAt(size_t count, size_t size, const char *file, int line);
void *ReallocAt(void *ptr, size_t size, const char *file, int line);
void FreeAt(void *ptr, const char *file, int line);

void *Malloc(size_t size);
void *RoboCalloc(size_t count, size_t size);
void *Realloc(void *ptr, size_t size);
void RoboFree(void *ptr);

void *RoboMemCopy(void *dst, const void *src, size_t n);
void *MemMove(void *dst, const void *src, size_t n);
void *MemSet(void *dst, int value, size_t n);
int MemCompare(const void *a, const void *b, size_t n);

#if RoboMemDebug && !defined(RoboMemImplementation)
#define Malloc(sz) MallocAt((sz), __FILE__, __LINE__)
#define RoboCalloc(c, s) CallocAt((c), (s), __FILE__, __LINE__)
#define Realloc(p, s) ReallocAt((p), (s), __FILE__, __LINE__)
#define RoboFree(p) FreeAt((p), __FILE__, __LINE__)
#endif

#ifdef __cplusplus
}
#endif

#endif /* ROBO_MEM_H */
