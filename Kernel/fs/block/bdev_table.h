#ifndef ROBO_BDEV_TABLE_H
#define ROBO_BDEV_TABLE_H

#include "fs/block/block.h"

#define RoboBdevNameMax 32u
#define RoboBdevTableMax 64u

#define RoboBdevKindDisk 1u
#define RoboBdevKindPart 2u

typedef struct RoboBdevEntry {
    char name[RoboBdevNameMax];
    uint32_t kind;
    uint32_t index;
    RoboBlockDevice *dev;
    RoboBlockDevice *parent;
} RoboBdevEntry;

int BdevTableInit(void);
int BdevTableAdd(const char *name, uint32_t kind, RoboBlockDevice *dev,
                 RoboBlockDevice *parent);
RoboBlockDevice *BdevTableFind(const char *name);
unsigned BdevTableCount(void);
RoboBdevEntry *BdevTableAt(unsigned i);
unsigned BdevTableDiskCount(void);
int BdevTableNextDiskName(char *out, unsigned outLen);

#endif
