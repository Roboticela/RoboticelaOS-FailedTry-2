#ifndef ROBO_VNODE_H
#define ROBO_VNODE_H

#include <stddef.h>
#include "fs/vfs/vfs_types.h"

struct RoboFile;

typedef struct RoboVnodeOps {
    int (*lookup)(struct RoboVnode *dir, const char *name,
                  struct RoboVnode **out);
    int (*open)(struct RoboVnode *node, struct RoboFile *file);
    int (*close)(struct RoboVnode *node, struct RoboFile *file);
    int64_t (*read)(struct RoboVnode *node, struct RoboFile *file,
                    void *buffer, size_t count, uint64_t offset);
    int64_t (*write)(struct RoboVnode *node, struct RoboFile *file,
                     const void *buffer, size_t count, uint64_t offset);
    int (*create)(struct RoboVnode *dir, const char *name,
                  RoboVnodeType type, uint32_t mode, struct RoboVnode **out);
    int (*unlink)(struct RoboVnode *dir, const char *name);
    int (*mkdir)(struct RoboVnode *dir, const char *name, uint32_t mode,
                 struct RoboVnode **out);
    int (*rmdir)(struct RoboVnode *dir, const char *name);
    int (*rename)(struct RoboVnode *oldDir, const char *oldName,
                  struct RoboVnode *newDir, const char *newName);
    int (*stat)(struct RoboVnode *node, RoboStat *st);
    int (*readdir)(struct RoboVnode *node, struct RoboFile *file,
                   RoboDirEntry *entry);
    void (*release)(struct RoboVnode *node);
} RoboVnodeOps;

typedef struct RoboVnode {
    RoboVnodeType type;
    uint64_t size;
    uint64_t id;
    uint32_t flags;
    uint32_t refcount;
    struct RoboMount *mount;
    struct RoboVnode *parent;
    struct RoboMount *cover;
    const RoboVnodeOps *ops;
    void *fsPrivate;
} RoboVnode;

RoboVnode *VnodeCreate(RoboVnodeType type, const RoboVnodeOps *ops,
                             void *fsPrivate);
void VnodeGet(RoboVnode *node);
void VnodePut(RoboVnode *node);
uint32_t VnodeRefcount(const RoboVnode *node);
uint64_t VnodeStatLive(void);

int VnodeLookup(RoboVnode *dir, const char *name, RoboVnode **out);
int64_t VnodeRead(RoboVnode *node, struct RoboFile *file,
                        void *buffer, size_t count, uint64_t offset);
int64_t VnodeWrite(RoboVnode *node, struct RoboFile *file,
                         const void *buffer, size_t count, uint64_t offset);
int VnodeStat(RoboVnode *node, RoboStat *st);

#endif
