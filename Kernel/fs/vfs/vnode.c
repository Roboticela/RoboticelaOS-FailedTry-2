#include "fs/vfs/vnode.h"

#if defined(PROCESS_HOST_TEST) || defined(RFS_HOST_TEST)
#include <stdlib.h>
#define kmalloc malloc
#define kfree free
#else
#include "mm/heap/heap.h"
#endif

static uint64_t gVnodeLive;

static void zmem(void *dst, unsigned n)
{
    unsigned char *p = (unsigned char *)dst;
    while (n--)
        *p++ = 0;
}

RoboVnode *VnodeCreate(RoboVnodeType type, const RoboVnodeOps *ops,
                             void *fsPrivate)
{
    RoboVnode *n;

    n = (RoboVnode *)kmalloc(sizeof(*n));
    if (!n)
        return 0;
    zmem(n, (unsigned)sizeof(*n));
    n->type = type;
    n->ops = ops;
    n->fsPrivate = fsPrivate;
    n->refcount = 1;
    gVnodeLive++;
    return n;
}

void VnodeGet(RoboVnode *node)
{
    if (node)
        node->refcount++;
}

void VnodePut(RoboVnode *node)
{
    if (!node || !node->refcount)
        return;
    node->refcount--;
    if (node->refcount)
        return;
    if (node->ops && node->ops->release)
        node->ops->release(node);
    if (node->parent) {
        VnodePut(node->parent);
        node->parent = 0;
    }
    if (gVnodeLive)
        gVnodeLive--;
    kfree(node);
}

uint32_t VnodeRefcount(const RoboVnode *node)
{
    return node ? node->refcount : 0;
}

uint64_t VnodeStatLive(void)
{
    return gVnodeLive;
}

int VnodeLookup(RoboVnode *dir, const char *name, RoboVnode **out)
{
    if (!dir || !name || !out)
        return -RoboEinval;
    if (dir->type != RoboVnodeDirectory)
        return -RoboEnotdir;
    if (!dir->ops || !dir->ops->lookup)
        return -RoboEnotsup;
    return dir->ops->lookup(dir, name, out);
}

int64_t VnodeRead(RoboVnode *node, struct RoboFile *file,
                        void *buffer, size_t count, uint64_t offset)
{
    if (!node)
        return -(int64_t)RoboEbadf;
    if (!node->ops || !node->ops->read)
        return -(int64_t)RoboEnotsup;
    return node->ops->read(node, file, buffer, count, offset);
}

int64_t VnodeWrite(RoboVnode *node, struct RoboFile *file,
                         const void *buffer, size_t count, uint64_t offset)
{
    if (!node)
        return -(int64_t)RoboEbadf;
    if (!node->ops || !node->ops->write)
        return -(int64_t)RoboEnotsup;
    return node->ops->write(node, file, buffer, count, offset);
}

int VnodeStat(RoboVnode *node, RoboStat *st)
{
    if (!node || !st)
        return -RoboEinval;
    if (!node->ops || !node->ops->stat)
        return -RoboEnotsup;
    return node->ops->stat(node, st);
}
