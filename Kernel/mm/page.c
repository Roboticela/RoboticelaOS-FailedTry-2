#include "mm/page.h"

bool PageAligned(PhysAddr address)
{
    return (address & RoboPageMask) == 0;
}

PhysAddr PageAlignDown(PhysAddr address)
{
    return address & ~RoboPageMask;
}

PhysAddr PageAlignUp(PhysAddr address)
{
    if (PageAligned(address))
        return address;
    if (address > RoboInvalidPhysAddr - RoboPageMask)
        return RoboInvalidPhysAddr;
    return (address + RoboPageMask) & ~RoboPageMask;
}

bool PhysEnd(PhysAddr base, uint64_t length, PhysAddr *end)
{
    if (!end)
        return false;
    if (length == 0) {
        *end = base;
        return true;
    }
    if (base > RoboInvalidPhysAddr - length)
        return false;
    *end = base + length;
    return true;
}

bool PhysRangesOverlap(PhysAddr a0, uint64_t aLen, PhysAddr b0, uint64_t bLen)
{
    PhysAddr a1, b1;
    if (aLen == 0 || bLen == 0)
        return false;
    if (!PhysEnd(a0, aLen, &a1) || !PhysEnd(b0, bLen, &b1))
        return true; /* overflow: treat as overlapping / unsafe */
    return a0 < b1 && b0 < a1;
}

size_t PhysToPage(PhysAddr address)
{
    return (size_t)(address >> RoboPageShift);
}

PhysAddr PageToPhys(size_t page)
{
    if (page > (size_t)(RoboInvalidPhysAddr >> RoboPageShift))
        return RoboInvalidPhysAddr;
    return (PhysAddr)page << RoboPageShift;
}

size_t BitmapBytes(size_t pageCount)
{
    if (pageCount == 0)
        return 0;
    if (pageCount > ((size_t)-1) - 7u)
        return 0;
    return (pageCount + 7u) / 8u;
}

size_t BitmapPages(size_t pageCount)
{
    size_t bytes = BitmapBytes(pageCount);
    PhysAddr up;
    if (!bytes)
        return 0;
    if (bytes > (size_t)(RoboInvalidPhysAddr - (RoboPageSize - 1ull)))
        return 0;
    up = PageAlignUp((PhysAddr)bytes);
    if (up == RoboInvalidPhysAddr)
        return 0;
    return (size_t)(up / RoboPageSize);
}
