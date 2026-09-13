/*
 * RoboMemDebug — debugging allocator controls (Goals 2.4 / 2.5).
 *
 * Compiled in when ROBO_MEM_DEBUG=1 (Debug builds). Release builds provide
 * lightweight stubs so call sites still link.
 *
 * OOM injection (robo_mem_debug_fail_next) lives here — not part of the
 * production allocation surface in RoboMem.h.
 */

#ifndef ROBO_MEM_DEBUG_H
#define ROBO_MEM_DEBUG_H

#include "RoboMem/RoboMem.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ROBO_MEM_DEBUG
#define ROBO_MEM_DEBUG 0
#endif

typedef struct robo_mem_stats {
    size_t live_count;
    size_t peak_live_count;
    size_t live_bytes;
    size_t peak_live_bytes;
    size_t total_allocs;
    size_t total_frees;
} robo_mem_stats_t;

typedef void (*robo_mem_fault_handler_fn)(const char *kind,
                                         const char *message,
                                         void *userdata);

void robo_mem_debug_enable(void);
void robo_mem_debug_disable(void);
int robo_mem_debug_is_enabled(void);

/** Print active allocations and totals to stderr. */
void robo_mem_debug_report(void);

void robo_mem_debug_get_stats(robo_mem_stats_t *out);
void robo_mem_debug_reset_stats(void);

/**
 * Validate guard bytes for one pointer (or all live if ptr == NULL).
 * @return 0 if OK, non-zero if corruption / unknown pointer when checking one.
 */
int robo_mem_debug_check(void *ptr);

/** Count of currently tracked live allocations (0 if debug off / disabled). */
size_t robo_mem_debug_live_count(void);

/**
 * Fault handler for double-free / invalid-free / guard corruption.
 * Default logs and aborts. Tests may install a non-aborting handler.
 */
void robo_mem_debug_set_fault_handler(robo_mem_fault_handler_fn handler,
                                      void *userdata);

/**
 * Force the next @p count heap allocations to fail (NULL).
 * Used by tests for OOM paths. Not thread-safe.
 */
void robo_mem_debug_fail_next(size_t count);

/**
 * Test helper: overwrite front (back==0) or back (back!=0) guard bytes.
 * Mutates allocator-owned guard storage only — avoids UB in the test process.
 * No-op when ROBO_MEM_DEBUG=0.
 */
void robo_mem_debug_corrupt_guard_for_test(void *ptr, int back);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_MEM_DEBUG_H */
