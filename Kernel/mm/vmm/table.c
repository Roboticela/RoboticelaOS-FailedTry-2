#include "mm/vmm/table.h"
#include "mm/pmm.h"

Pte *TablePtr(PhysAddr phys)
{
    if (phys == RoboInvalidPhysAddr || !PageAligned(phys))
        return 0;
    /* Page-table pages live in identity-mapped RAM for this phase. */
    return (Pte *)(uintptr_t)phys;
}

void TableZero(PhysAddr phys)
{
    Pte *t = TablePtr(phys);
    unsigned i;
    if (!t)
        return;
    for (i = 0; i < RoboPtEntries; i++)
        t[i] = 0;
}

PhysAddr TableAlloc(void)
{
    PhysAddr p = PageAlloc();
    if (p == RoboInvalidPhysAddr)
        return p;
    TableZero(p);
    return p;
}

Pte TableGet(const Pte *table, unsigned index)
{
    if (!table || index >= RoboPtEntries)
        return 0;
    return table[index];
}

void TableSet(Pte *table, unsigned index, Pte entry)
{
    if (!table || index >= RoboPtEntries)
        return;
    table[index] = entry;
}

int PtePresent(Pte e)
{
    return (e & RoboPteP) != 0;
}

int PteHuge(Pte e)
{
    return (e & RoboPtePs) != 0;
}
