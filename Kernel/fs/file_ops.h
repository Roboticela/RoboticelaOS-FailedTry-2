#ifndef ROBO_FILE_OPS_H
#define ROBO_FILE_OPS_H

#include <stddef.h>
#include <stdint.h>
#include "fs/file_types.h"

struct RoboFile;

typedef struct RoboFileOps {
    int64_t (*read)(struct RoboFile *file, void *buffer, size_t count);
    int64_t (*write)(struct RoboFile *file, const void *buffer, size_t count);
    int (*close)(struct RoboFile *file);
    int (*seek)(struct RoboFile *file, int64_t offset, int whence);
    int (*ioctl)(struct RoboFile *file, uint64_t request, void *arg);
    int (*poll)(struct RoboFile *file, uint32_t events);
} RoboFileOps;

int64_t FileRead(struct RoboFile *file, void *buffer, size_t count);
int64_t FileWrite(struct RoboFile *file, const void *buffer, size_t count);
int FileSeek(struct RoboFile *file, int64_t offset, int whence);
int FileIoctl(struct RoboFile *file, uint64_t request, void *arg);
int FilePoll(struct RoboFile *file, uint32_t events);

#endif
