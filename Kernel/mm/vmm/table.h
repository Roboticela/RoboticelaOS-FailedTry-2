#ifndef ROBO_MM_VMM_TABLE_H
#define ROBO_MM_VMM_TABLE_H

#include "mm/vmm/paging.h"

Pte *TablePtr(PhysAddr phys);
PhysAddr TableAlloc(void);
void TableZero(PhysAddr phys);

Pte TableGet(const Pte *table, unsigned index);
void TableSet(Pte *table, unsigned index, Pte entry);

int PtePresent(Pte e);
int PteHuge(Pte e);

#endif
