#ifndef ROBO_MM_HEAP_H
#define ROBO_MM_HEAP_H

#include "mm/page.h"

/*
 * Kernel heap — first-fit free-list on a dedicated higher-half window.
 *
 *   HEAP_BASE  0xFFFF800000100000   RoboVmmHeapBase
 *   HEAP_END   HEAP_BASE + 256 MiB
 *
 * The window sits above the VMM test range (0xFFFF800000000000) and
 * outside the identity-mapped kernel image, stack, framebuffer, and
 * boot mappings. Pages are mapped on demand:
 *
 *   kmalloc -> free list -> (grow) PMM page_alloc + VMM map
 *
 * Heap pages are kernel-only, writable, and non-executable.
 * Empty pages are not returned to the PMM in this phase.
 *
 * kmalloc(0), kcalloc(0, n), and kcalloc(n, 0) return NULL.
 * kfree(NULL) is a no-op.
 * krealloc(NULL, n) is kmalloc(n).
 * krealloc(p, 0) frees p and returns NULL.
 * krealloc failure leaves the original allocation valid.
 */

#define RoboHeapAlignment 16u
#define RoboHeapBase      0xFFFF800000100000ull
#define RoboHeapSize      (256ull * 1024ull * 1024ull)
#define RoboHeapEnd       (RoboHeapBase + RoboHeapSize)

bool HeapInit(void);
int HeapReady(void);
void HeapReport(void);
#ifndef HEAP_HOST_TEST
void HeapFbReport(void);
#endif
int HeapSelftest(void);
bool HeapValidate(void);

void *kmalloc(size_t size);
void kfree(void *ptr);
void *kcalloc(size_t count, size_t size);
void *krealloc(void *ptr, size_t newSize);

size_t HeapTotalBytes(void);
size_t HeapUsedBytes(void);
size_t HeapFreeBytes(void);
size_t HeapPageCount(void);
size_t HeapAllocationCount(void);

#endif
