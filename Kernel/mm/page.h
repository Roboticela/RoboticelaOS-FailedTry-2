#ifndef ROBO_MM_PAGE_H
#define ROBO_MM_PAGE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef uint64_t PhysAddr;
typedef uint64_t VirtAddr;

#define RoboPageShift 12u
#define RoboPageSize  (1ull << RoboPageShift)
#define RoboPageMask  (RoboPageSize - 1ull)
#define RoboInvalidPhysAddr (~(PhysAddr)0)
#define RoboInvalidVirtAddr (~(VirtAddr)0)

bool PageAligned(PhysAddr address);
PhysAddr PageAlignDown(PhysAddr address);
PhysAddr PageAlignUp(PhysAddr address);

bool PhysEnd(PhysAddr base, uint64_t length, PhysAddr *end);
bool PhysRangesOverlap(PhysAddr a0, uint64_t aLen, PhysAddr b0, uint64_t bLen);

size_t PhysToPage(PhysAddr address);
PhysAddr PageToPhys(size_t page);

size_t BitmapBytes(size_t pageCount);
size_t BitmapPages(size_t pageCount);

#endif
