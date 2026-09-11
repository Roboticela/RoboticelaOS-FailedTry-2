#ifndef ROBO_MM_PAGE_H
#define ROBO_MM_PAGE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef uint64_t phys_addr_t;
typedef uint64_t virt_addr_t;

#define ROBO_PAGE_SHIFT 12u
#define ROBO_PAGE_SIZE  (1ull << ROBO_PAGE_SHIFT)
#define ROBO_PAGE_MASK  (ROBO_PAGE_SIZE - 1ull)
#define ROBO_INVALID_PHYS_ADDR (~(phys_addr_t)0)
#define ROBO_INVALID_VIRT_ADDR (~(virt_addr_t)0)

bool robo_page_aligned(phys_addr_t address);
phys_addr_t robo_page_align_down(phys_addr_t address);
phys_addr_t robo_page_align_up(phys_addr_t address);

bool robo_phys_end(phys_addr_t base, uint64_t length, phys_addr_t *end);
bool robo_phys_ranges_overlap(phys_addr_t a0, uint64_t a_len, phys_addr_t b0, uint64_t b_len);

size_t robo_phys_to_page(phys_addr_t address);
phys_addr_t robo_page_to_phys(size_t page);

size_t robo_bitmap_bytes(size_t page_count);
size_t robo_bitmap_pages(size_t page_count);

#endif
