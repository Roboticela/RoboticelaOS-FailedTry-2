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
    int (*rename)(struct RoboVnode *old_dir, const char *old_name,
                  struct RoboVnode *new_dir, const char *new_name);
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
    void *fs_private;
} RoboVnode;

RoboVnode *robo_vnode_create(RoboVnodeType type, const RoboVnodeOps *ops,
                             void *fs_private);
void robo_vnode_get(RoboVnode *node);
void robo_vnode_put(RoboVnode *node);
uint32_t robo_vnode_refcount(const RoboVnode *node);
uint64_t robo_vnode_stat_live(void);

int robo_vnode_lookup(RoboVnode *dir, const char *name, RoboVnode **out);
int64_t robo_vnode_read(RoboVnode *node, struct RoboFile *file,
                        void *buffer, size_t count, uint64_t offset);
int64_t robo_vnode_write(RoboVnode *node, struct RoboFile *file,
                         const void *buffer, size_t count, uint64_t offset);
int robo_vnode_stat(RoboVnode *node, RoboStat *st);

#endif
