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
    char path[RoboPathMax + 1];
    uint32_t flags;
    uint32_t refcount;
    void *fsInstance;
    struct RoboMount *next;
} RoboMount;

RoboMount *MountCreate(const RoboFilesystem *fs, struct RoboVnode *root,
                             void *instance);
void MountGet(RoboMount *m);
void MountPut(RoboMount *m);

#endif
