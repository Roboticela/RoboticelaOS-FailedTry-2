#ifndef ROBO_FILE_H
#define ROBO_FILE_H

#include "fs/file_types.h"
#include "fs/file_ops.h"

struct RoboVnode;

typedef struct RoboFile {
    RoboFileType type;
    uint32_t flags;
    uint64_t offset;
    const RoboFileOps *ops;
    void *privateData;
    uint32_t refcount;
    struct RoboVnode *vnode;
} RoboFile;

RoboFile *FileCreate(RoboFileType type, const RoboFileOps *ops,
                           void *privateData, uint32_t flags);
void FileGet(RoboFile *file);
void FilePut(RoboFile *file);
uint32_t FileRefcount(const RoboFile *file);
uint64_t FileStatLive(void);
const char *FileTypeName(RoboFileType type);

#endif
