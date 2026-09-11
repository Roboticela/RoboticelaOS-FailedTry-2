#include "mm/page.h"

bool robo_page_aligned(phys_addr_t address)
{
    return (address & ROBO_PAGE_MASK) == 0;
}

phys_addr_t robo_page_align_down(phys_addr_t address)
{
    return address & ~ROBO_PAGE_MASK;
}

phys_addr_t robo_page_align_up(phys_addr_t address)
{
    if (robo_page_aligned(address))
        return address;
    if (address > ROBO_INVALID_PHYS_ADDR - ROBO_PAGE_MASK)
        return ROBO_INVALID_PHYS_ADDR;
    return (address + ROBO_PAGE_MASK) & ~ROBO_PAGE_MASK;
}

bool robo_phys_end(phys_addr_t base, uint64_t length, phys_addr_t *end)
{
    if (!end)
        return false;
    if (length == 0) {
        *end = base;
        return true;
    }
    if (base > ROBO_INVALID_PHYS_ADDR - length)
        return false;
    *end = base + length;
    return true;
}

bool robo_phys_ranges_overlap(phys_addr_t a0, uint64_t a_len, phys_addr_t b0, uint64_t b_len)
{
    phys_addr_t a1, b1;
    if (a_len == 0 || b_len == 0)
        return false;
    if (!robo_phys_end(a0, a_len, &a1) || !robo_phys_end(b0, b_len, &b1))
        return true; /* overflow: treat as overlapping / unsafe */
    return a0 < b1 && b0 < a1;
}

size_t robo_phys_to_page(phys_addr_t address)
{
    return (size_t)(address >> ROBO_PAGE_SHIFT);
}

phys_addr_t robo_page_to_phys(size_t page)
{
    if (page > (size_t)(ROBO_INVALID_PHYS_ADDR >> ROBO_PAGE_SHIFT))
        return ROBO_INVALID_PHYS_ADDR;
    return (phys_addr_t)page << ROBO_PAGE_SHIFT;
}

size_t robo_bitmap_bytes(size_t page_count)
{
    if (page_count == 0)
        return 0;
    if (page_count > ((size_t)-1) - 7u)
        return 0;
    return (page_count + 7u) / 8u;
}

size_t robo_bitmap_pages(size_t page_count)
{
    size_t bytes = robo_bitmap_bytes(page_count);
    phys_addr_t up;
    if (!bytes)
        return 0;
    if (bytes > (size_t)(ROBO_INVALID_PHYS_ADDR - (ROBO_PAGE_SIZE - 1ull)))
        return 0;
    up = robo_page_align_up((phys_addr_t)bytes);
    if (up == ROBO_INVALID_PHYS_ADDR)
        return 0;
    return (size_t)(up / ROBO_PAGE_SIZE);
}
