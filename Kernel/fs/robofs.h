#ifndef ROBO_FS_H
#define ROBO_FS_H

#include <stddef.h>
#include <stdint.h>
#include "roboticela/bootinfo.h"

#define ROBFS_MAGIC   0x31534652u /* 'RFS1' */
#define ROBFS_VERSION 1u
#define ROBFS_NAME    112u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t nfiles;
    uint32_t size;
} __attribute__((packed)) RoboFsHeader;

typedef struct {
    char name[ROBFS_NAME];
    uint32_t off;
    uint32_t len;
} __attribute__((packed)) RoboFsEntry;

int FsInit(const RoboBootInfo *info);
int FsReady(void);
uint64_t FsPayloadBytes(void);
uint32_t FsRootBlocks(uint32_t blockSize);
int FsOpen(const char *path, const void **data, uint32_t *size);
unsigned FsFileCount(void);
int FsFileAt(unsigned index, char *name, unsigned nameMax,
                    const void **data, uint32_t *size);
void FsReport(void);

#endif
