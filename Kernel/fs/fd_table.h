#ifndef ROBO_FD_TABLE_H
#define ROBO_FD_TABLE_H

#include "fs/file.h"

typedef struct RoboFDEntry {
    RoboFile *file;
    uint32_t flags;
} RoboFDEntry;

typedef struct RoboFDTable {
    RoboFDEntry ent[RoboMaxFds];
    unsigned openCount;
} RoboFDTable;

RoboFDTable *FdTableCreate(void);
void FdTableDestroy(RoboFDTable *table);
int FdTableClone(const RoboFDTable *source, RoboFDTable **destination);
unsigned FdTableOpenCount(const RoboFDTable *table);

Fd FdTableAlloc(RoboFDTable *table, RoboFile *file,
                              uint32_t flags);
int FdTableClose(RoboFDTable *table, Fd fd);
RoboFile *FdTableGet(RoboFDTable *table, Fd fd);

#endif
