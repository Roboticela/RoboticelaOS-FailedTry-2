#ifndef ROBO_PARTITION_H
#define ROBO_PARTITION_H

#include "fs/block/block.h"

#define RoboPartMax 16u

#define RoboPartSchemeNone 0u
#define RoboPartSchemeMbr  1u
#define RoboPartSchemeGpt  2u

typedef struct RoboPartition {
    uint32_t index;
    uint64_t firstLba;
    uint64_t blockCount;
    uint8_t typeGuid[16];
    uint8_t mbrType;
    RoboBlockDevice bdev;
} RoboPartition;

typedef struct RoboPartitionTable {
    RoboBlockDevice *disk;
    uint32_t scheme;
    unsigned count;
    RoboPartition parts[RoboPartMax];
} RoboPartitionTable;

/*
 * Probe GPT (preferred) or MBR on @disk. Registers partition block devices
 * as "<diskName>p1" … in the block table. Returns the number of partitions
 * found, or a negative syscall error.
 */
int PartitionProbe(RoboBlockDevice *disk, const char *diskName);

unsigned PartitionCount(const RoboBlockDevice *disk);
RoboBlockDevice *PartitionAt(const RoboBlockDevice *disk, unsigned i);

#endif
