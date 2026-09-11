#ifndef ROBO_MM_HEAP_H
#define ROBO_MM_HEAP_H

#include "mm/page.h"

/*
 * Kernel heap — first-fit free-list on a dedicated higher-half window.
 *
 *   HEAP_BASE  0xFFFF800000100000   ROBO_VMM_HEAP_BASE
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

#define ROBO_HEAP_ALIGNMENT 16u
#define ROBO_HEAP_BASE      0xFFFF800000100000ull
#define ROBO_HEAP_SIZE      (256ull * 1024ull * 1024ull)
#define ROBO_HEAP_END       (ROBO_HEAP_BASE + ROBO_HEAP_SIZE)

bool robo_heap_init(void);
int robo_heap_ready(void);
void robo_heap_report(void);
#ifndef HEAP_HOST_TEST
void robo_heap_fb_report(void);
#endif
int robo_heap_selftest(void);
bool robo_heap_validate(void);

void *kmalloc(size_t size);
void kfree(void *ptr);
void *kcalloc(size_t count, size_t size);
void *krealloc(void *ptr, size_t new_size);

size_t robo_heap_total_bytes(void);
size_t robo_heap_used_bytes(void);
size_t robo_heap_free_bytes(void);
size_t robo_heap_page_count(void);
size_t robo_heap_allocation_count(void);

#endif
