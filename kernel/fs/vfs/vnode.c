#include "fs/vfs/vnode.h"

#if defined(PROCESS_HOST_TEST) || defined(RFS_HOST_TEST)
#include <stdlib.h>
#define kmalloc malloc
#define kfree free
#else
#include "mm/heap/heap.h"
#endif

static uint64_t g_vnode_live;

static void zmem(void *dst, unsigned n)
{
    unsigned char *p = (unsigned char *)dst;
    while (n--)
        *p++ = 0;
}

RoboVnode *robo_vnode_create(RoboVnodeType type, const RoboVnodeOps *ops,
                             void *fs_private)
{
    RoboVnode *n;

    n = (RoboVnode *)kmalloc(sizeof(*n));
    if (!n)
        return 0;
    zmem(n, (unsigned)sizeof(*n));
    n->type = type;
    n->ops = ops;
    n->fs_private = fs_private;
    n->refcount = 1;
    g_vnode_live++;
    return n;
}

void robo_vnode_get(RoboVnode *node)
{
    if (node)
        node->refcount++;
}

void robo_vnode_put(RoboVnode *node)
{
    if (!node || !node->refcount)
        return;
    node->refcount--;
    if (node->refcount)
        return;
    if (node->ops && node->ops->release)
        node->ops->release(node);
    if (node->parent) {
        robo_vnode_put(node->parent);
        node->parent = 0;
    }
    if (g_vnode_live)
        g_vnode_live--;
    kfree(node);
}

uint32_t robo_vnode_refcount(const RoboVnode *node)
{
    return node ? node->refcount : 0;
}

uint64_t robo_vnode_stat_live(void)
{
    return g_vnode_live;
}

int robo_vnode_lookup(RoboVnode *dir, const char *name, RoboVnode **out)
{
    if (!dir || !name || !out)
        return -ROBO_EINVAL;
    if (dir->type != ROBO_VNODE_DIRECTORY)
        return -ROBO_ENOTDIR;
    if (!dir->ops || !dir->ops->lookup)
        return -ROBO_ENOTSUP;
    return dir->ops->lookup(dir, name, out);
}

int64_t robo_vnode_read(RoboVnode *node, struct RoboFile *file,
                        void *buffer, size_t count, uint64_t offset)
{
    if (!node)
        return -(int64_t)ROBO_EBADF;
    if (!node->ops || !node->ops->read)
        return -(int64_t)ROBO_ENOTSUP;
    return node->ops->read(node, file, buffer, count, offset);
}

int64_t robo_vnode_write(RoboVnode *node, struct RoboFile *file,
                         const void *buffer, size_t count, uint64_t offset)
{
    if (!node)
        return -(int64_t)ROBO_EBADF;
    if (!node->ops || !node->ops->write)
        return -(int64_t)ROBO_ENOTSUP;
    return node->ops->write(node, file, buffer, count, offset);
}

int robo_vnode_stat(RoboVnode *node, RoboStat *st)
{
    if (!node || !st)
        return -ROBO_EINVAL;
    if (!node->ops || !node->ops->stat)
        return -ROBO_ENOTSUP;
    return node->ops->stat(node, st);
}
