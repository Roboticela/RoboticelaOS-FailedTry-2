#ifndef ROBO_PROCESS_TABLE_H
#define ROBO_PROCESS_TABLE_H

#include "process/process.h"

void PtableInit(void);
int PtableInsert(RoboProcess *process);
void PtableRemove(RoboProcess *process);
RoboProcess *PtableLookup(ProcessId pid);
uint64_t PtableCount(void);
RoboProcess *PtableHead(void);
ProcessId PtableAllocId(void);

#endif
