#ifndef ROBO_VFS_MOUNT_H
#define ROBO_VFS_MOUNT_H

#include "fs/vfs/vfs_types.h"
#include "fs/block/block.h"

struct RoboFilesystemOps;

typedef struct RoboFilesystem {
    const char *name;
    uint32_t id;
    const struct RoboFilesystemOps *ops;
} RoboFilesystem;

typedef struct RoboFilesystemOps {
    int (*mount)(RoboBlockDevice *dev, uint32_t flags, struct RoboMount **out);
    int (*unmount)(struct RoboMount *mount);
} RoboFilesystemOps;

typedef struct RoboMount {
    const RoboFilesystem *fs;
    struct RoboVnode *root;
    char path[ROBO_PATH_MAX + 1];
    uint32_t flags;
    uint32_t refcount;
    void *fs_instance;
    struct RoboMount *next;
} RoboMount;

RoboMount *robo_mount_create(const RoboFilesystem *fs, struct RoboVnode *root,
                             void *instance);
void robo_mount_get(RoboMount *m);
void robo_mount_put(RoboMount *m);

#endif
