#ifndef ROBO_FILE_H
#define ROBO_FILE_H

#include "fs/file_types.h"
#include "fs/file_ops.h"

typedef struct RoboFile {
    RoboFileType type;
    uint32_t flags;
    uint64_t offset;
    const RoboFileOps *ops;
    void *private_data;
    uint32_t refcount;
} RoboFile;

RoboFile *robo_file_create(RoboFileType type, const RoboFileOps *ops,
                           void *private_data, uint32_t flags);
void robo_file_get(RoboFile *file);
void robo_file_put(RoboFile *file);
uint32_t robo_file_refcount(const RoboFile *file);
uint64_t robo_file_stat_live(void);
const char *robo_file_type_name(RoboFileType type);

#endif
