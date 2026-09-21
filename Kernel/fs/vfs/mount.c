#include "fs/vfs/mount.h"
#include "fs/vfs/vnode.h"

#if defined(PROCESS_HOST_TEST) || defined(RFS_HOST_TEST)
#include <stdlib.h>
#define kmalloc malloc
#define kfree free
#else
#include "mm/heap/heap.h"
#endif

static void zmem(void *dst, unsigned n)
{
    unsigned char *p = (unsigned char *)dst;
    while (n--)
        *p++ = 0;
}

RoboMount *MountCreate(const RoboFilesystem *fs, RoboVnode *root,
                             void *instance)
{
    RoboMount *m;

    if (!fs || !root)
        return 0;
    m = (RoboMount *)kmalloc(sizeof(*m));
    if (!m)
        return 0;
    zmem(m, (unsigned)sizeof(*m));
    m->fs = fs;
    m->root = root;
    m->fsInstance = instance;
    m->refcount = 1;
    VnodeGet(root);
    root->mount = m;
    return m;
}

void MountGet(RoboMount *m)
{
    if (m)
        m->refcount++;
}

void MountPut(RoboMount *m)
{
    if (!m || !m->refcount)
        return;
    m->refcount--;
    if (m->refcount)
        return;
    if (m->root) {
        m->root->mount = 0;
        VnodePut(m->root);
        m->root = 0;
    }
    kfree(m);
}
