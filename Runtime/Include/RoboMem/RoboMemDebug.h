/*
 * RoboMemDebug — debugging allocator controls (Goals 2.4 / 2.5).
 *
 * Compiled in when RoboMemDebug=1 (Debug builds). Release builds provide
 * lightweight stubs so call sites still link.
 *
 * OOM injection (MemDebugFailNext) lives here — not part of the
 * production allocation surface in RoboMem.h.
 */

#ifndef ROBO_MEM_DEBUG_H
#define ROBO_MEM_DEBUG_H

#include "RoboMem/RoboMem.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RoboMemDebug
#define RoboMemDebug 0
#endif

typedef struct MemStats {
    size_t liveCount;
    size_t peakLiveCount;
    size_t liveBytes;
    size_t peakLiveBytes;
    size_t totalAllocs;
    size_t totalFrees;
} MemStats;

typedef void (*MemFaultHandlerFn)(const char *kind,
                                         const char *message,
                                         void *userdata);

void MemDebugEnable(void);
void MemDebugDisable(void);
int MemDebugIsEnabled(void);

/** Print active allocations and totals to stderr. */
void MemDebugReport(void);

void MemDebugGetStats(MemStats *out);
void MemDebugResetStats(void);

/**
 * Validate guard bytes for one pointer (or all live if ptr == NULL).
 * @return 0 if OK, non-zero if corruption / unknown pointer when checking one.
 */
int MemDebugCheck(void *ptr);

/** Count of currently tracked live allocations (0 if debug off / disabled). */
size_t MemDebugLiveCount(void);

/**
 * Fault handler for double-free / invalid-free / guard corruption.
 * Default logs and aborts. Tests may install a non-aborting handler.
 */
void MemDebugSetFaultHandler(MemFaultHandlerFn handler,
                                      void *userdata);

/**
 * Force the next @p count heap allocations to fail (NULL).
 * Used by tests for OOM paths. Not thread-safe.
 */
void MemDebugFailNext(size_t count);

/**
 * Test helper: overwrite front (back==0) or back (back!=0) guard bytes.
 * Mutates allocator-owned guard storage only — avoids UB in the test process.
 * No-op when RoboMemDebug=0.
 */
void MemDebugCorruptGuardForTest(void *ptr, int back);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_MEM_DEBUG_H */
