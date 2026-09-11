#include "fs/vfs/vfs.h"
#include "syscall/syscall_errors.h"
#include "syscall/syscall_numbers.h"
#include <stdint.h>

#if defined(PROCESS_HOST_TEST) || defined(RFS_HOST_TEST)
#include <stdlib.h>
#define kmalloc malloc
#define kfree free
static uint64_t vfs_lock(void) { return 0; }
static void vfs_unlock(uint64_t f) { (void)f; }
#else
#include "mm/heap/heap.h"
#include "kio.h"
#include "fs/robofs.h"
static uint64_t vfs_lock(void)
{
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
static void vfs_unlock(uint64_t flags)
{
    __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
}
#endif

static RoboFilesystem *g_fs[ROBO_VFS_MAX_FS];
static unsigned g_nfs;
static RoboMount *g_root_mount;
static uint32_t g_next_fs_id = 1;

/*
 * Lock hierarchy (single CPU, interrupt flag):
 *   vfs_lock  — filesystem registry and root-mount pointer
 *   rfs_lock  — RoboFS metadata mutations
 * Never acquire vfs_lock while holding rfs_lock. Path walk is currently
 * unsynchronized; backends serialize their own mutations.
 */

static uint32_t open_to_fflags(uint32_t flags)
{
    uint32_t acc = flags & 3u;
    uint32_t f = 0;

    if (acc == ROBO_O_RDONLY)
        f = ROBO_FFLAG_READ;
    else if (acc == ROBO_O_WRONLY)
        f = ROBO_FFLAG_WRITE;
    else     if (acc == ROBO_O_RDWR)
        f = ROBO_FFLAG_READ | ROBO_FFLAG_WRITE;
    if (flags & ROBO_O_TRUNC)
        f |= ROBO_FFLAG_TRUNC;
    return f;
}

static RoboFileType vnode_file_type(RoboVnodeType t)
{
    switch (t) {
    case ROBO_VNODE_DIRECTORY: return ROBO_FILE_DIRECTORY;
    case ROBO_VNODE_PIPE: return ROBO_FILE_PIPE;
    case ROBO_VNODE_SOCKET: return ROBO_FILE_SOCKET;
    case ROBO_VNODE_DEVICE: return ROBO_FILE_DEVICE;
    default: return ROBO_FILE_REGULAR;
    }
}

static void cross_mount(RoboVnode **node)
{
    RoboVnode *n = *node;
    if (n && n->cover && n->cover->root) {
        robo_vnode_get(n->cover->root);
        robo_vnode_put(n);
        *node = n->cover->root;
    }
}

int robo_vfs_init(void)
{
    unsigned i;
    g_nfs = 0;
    g_root_mount = 0;
    g_next_fs_id = 1;
    for (i = 0; i < ROBO_VFS_MAX_FS; i++)
        g_fs[i] = 0;
    return 0;
}

int robo_vfs_register(RoboFilesystem *fs)
{
    unsigned i;
    uint64_t lockf;

    if (!fs || !fs->name || !fs->ops)
        return -ROBO_EINVAL;
    lockf = vfs_lock();
    for (i = 0; i < g_nfs; i++) {
        const char *a = g_fs[i]->name;
        const char *b = fs->name;
        unsigned k;
        int same = 1;
        for (k = 0; a[k] || b[k]; k++) {
            if (a[k] != b[k]) {
                same = 0;
                break;
            }
        }
        if (same) {
            vfs_unlock(lockf);
            return -ROBO_EEXIST;
        }
    }
    if (g_nfs >= ROBO_VFS_MAX_FS) {
        vfs_unlock(lockf);
        return -ROBO_ENOSPC;
    }
    if (!fs->id)
        fs->id = g_next_fs_id++;
    g_fs[g_nfs++] = fs;
    vfs_unlock(lockf);
    return 0;
}

const RoboFilesystem *robo_vfs_find_fs(const char *name)
{
    unsigned i, k;

    if (!name)
        return 0;
    for (i = 0; i < g_nfs; i++) {
        const char *a = g_fs[i]->name;
        int same = 1;
        for (k = 0; a[k] || name[k]; k++) {
            if (a[k] != name[k]) {
                same = 0;
                break;
            }
        }
        if (same)
            return g_fs[i];
    }
    return 0;
}

int robo_vfs_mount_root(const char *fs_name, RoboBlockDevice *dev, uint32_t flags)
{
    const RoboFilesystem *fs;
    RoboMount *m;
    int r;

    fs = robo_vfs_find_fs(fs_name);
    if (!fs || !fs->ops || !fs->ops->mount)
        return -ROBO_EINVAL;
    if (g_root_mount)
        return -ROBO_EBUSY;
    r = fs->ops->mount(dev, flags, &m);
    if (r != 0)
        return r;
    m->fs = fs;
    m->path[0] = '/';
    m->path[1] = 0;
    g_root_mount = m;
    return 0;
}

int robo_vfs_unmount_root(void)
{
    RoboMount *m = g_root_mount;
    int r = 0;

    if (!m)
        return -ROBO_EINVAL;
    g_root_mount = 0;
    if (m->fs && m->fs->ops && m->fs->ops->unmount)
        r = m->fs->ops->unmount(m);
    else
        robo_mount_put(m);
    return r;
}

RoboVnode *robo_vfs_root(void)
{
    if (!g_root_mount || !g_root_mount->root)
        return 0;
    robo_vnode_get(g_root_mount->root);
    return g_root_mount->root;
}

int robo_vfs_lookup_at(RoboVnode *start, const char *path, uint32_t flags,
                       RoboVnode **out, char *last, unsigned last_max)
{
    RoboVnode *cur, *next;
    const char *p;
    char name[ROBO_NAME_MAX + 1];
    int r, got;
    int want_parent = (flags & ROBO_VFS_LOOKUP_PARENT) != 0;

    if (!out)
        return -ROBO_EINVAL;
    *out = 0;
    if (last && last_max)
        last[0] = 0;
    if (!robo_path_valid(path))
        return -ROBO_EINVAL;

    if (path[0] == '/') {
        cur = robo_vfs_root();
        if (start)
            (void)start;
    } else {
        cur = start ? start : robo_vfs_root();
        if (start)
            robo_vnode_get(start);
    }
    if (!cur)
        return -ROBO_ENOENT;

    p = path;
    if (*p == '/')
        p++;

    if (!*p) {
        if (want_parent) {
            robo_vnode_put(cur);
            return -ROBO_EINVAL;
        }
        *out = cur;
        return 0;
    }

    for (;;) {
        int trailing = 0;
        int is_last;

        got = robo_path_next(&p, name, sizeof(name));
        if (got < 0) {
            robo_vnode_put(cur);
            return got;
        }
        if (!got || !name[0]) {
            if (cur->type != ROBO_VNODE_DIRECTORY) {
                robo_vnode_put(cur);
                return -ROBO_ENOTDIR;
            }
            *out = cur;
            return 0;
        }

        while (*p == '/') {
            trailing = 1;
            p++;
        }
        is_last = !*p;

        if (want_parent && is_last) {
            if (last && last_max) {
                unsigned i;
                for (i = 0; name[i] && i + 1 < last_max; i++)
                    last[i] = name[i];
                last[i] = 0;
            }
            *out = cur;
            return 0;
        }

        if (name[0] == '.' && !name[1]) {
            if (is_last) {
                if (trailing && cur->type != ROBO_VNODE_DIRECTORY) {
                    robo_vnode_put(cur);
                    return -ROBO_ENOTDIR;
                }
                *out = cur;
                return 0;
            }
            continue;
        }
        if (name[0] == '.' && name[1] == '.' && !name[2]) {
            next = cur->parent ? cur->parent : cur;
            robo_vnode_get(next);
            robo_vnode_put(cur);
            cur = next;
            cross_mount(&cur);
            if (is_last) {
                if (trailing && cur->type != ROBO_VNODE_DIRECTORY) {
                    robo_vnode_put(cur);
                    return -ROBO_ENOTDIR;
                }
                *out = cur;
                return 0;
            }
            continue;
        }

        cross_mount(&cur);
        if (cur->type != ROBO_VNODE_DIRECTORY) {
            robo_vnode_put(cur);
            return -ROBO_ENOTDIR;
        }
        r = robo_vnode_lookup(cur, name, &next);
        if (r != 0) {
            robo_vnode_put(cur);
            return r;
        }
        if (next->parent != cur) {
            if (next->parent)
                robo_vnode_put(next->parent);
            robo_vnode_get(cur);
            next->parent = cur;
        }
        robo_vnode_put(cur);
        cur = next;
        cross_mount(&cur);
        if (is_last) {
            if (trailing && cur->type != ROBO_VNODE_DIRECTORY) {
                robo_vnode_put(cur);
                return -ROBO_ENOTDIR;
            }
            *out = cur;
            return 0;
        }
    }
}

int robo_vfs_lookup(const char *path, RoboVnode **out)
{
    return robo_vfs_lookup_at(0, path, 0, out, 0, 0);
}

static int64_t vfs_file_read(RoboFile *file, void *buffer, size_t count)
{
    int64_t n;

    if (!file || !file->vnode)
        return -(int64_t)ROBO_EBADF;
    n = robo_vnode_read(file->vnode, file, buffer, count, file->offset);
    if (n > 0)
        file->offset += (uint64_t)n;
    return n;
}

static int64_t vfs_file_write(RoboFile *file, const void *buffer, size_t count)
{
    int64_t n;

    if (!file || !file->vnode)
        return -(int64_t)ROBO_EBADF;
    n = robo_vnode_write(file->vnode, file, buffer, count, file->offset);
    if (n > 0)
        file->offset += (uint64_t)n;
    return n;
}

static int vfs_file_close(RoboFile *file)
{
    if (!file)
        return 0;
    if (file->vnode) {
        if (file->vnode->ops && file->vnode->ops->close)
            file->vnode->ops->close(file->vnode, file);
        robo_vnode_put(file->vnode);
        file->vnode = 0;
    }
    return 0;
}

static int vfs_file_seek(RoboFile *file, int64_t offset, int whence)
{
    int64_t base, next;
    uint64_t size;

    if (!file || !file->vnode)
        return -ROBO_EBADF;
    size = file->vnode->size;
    if (whence == ROBO_SEEK_SET)
        base = 0;
    else if (whence == ROBO_SEEK_CUR)
        base = (int64_t)file->offset;
    else if (whence == ROBO_SEEK_END)
        base = (int64_t)size;
    else
        return -ROBO_EINVAL;
    if (offset > 0 && base > (int64_t)INT64_MAX - offset)
        return -ROBO_EINVAL;
    if (offset < 0 && base < (int64_t)INT64_MIN - offset)
        return -ROBO_EINVAL;
    next = base + offset;
    if (next < 0)
        return -ROBO_EINVAL;
    file->offset = (uint64_t)next;
    return 0;
}

static const RoboFileOps g_vfs_file_ops = {
    .read = vfs_file_read,
    .write = vfs_file_write,
    .close = vfs_file_close,
    .seek = vfs_file_seek,
    .ioctl = 0,
    .poll = 0,
};

const RoboFileOps *robo_vfs_file_ops(void)
{
    return &g_vfs_file_ops;
}

static int file_from_vnode(RoboVnode *node, uint32_t flags, RoboFile **out)
{
    RoboFile *file;
    uint32_t ff = open_to_fflags(flags);
    int r;

    if (!ff)
        return -ROBO_EINVAL;
    file = robo_file_create(vnode_file_type(node->type), &g_vfs_file_ops, 0, ff);
    if (!file)
        return -ROBO_ENOMEM;
    file->vnode = node;
    robo_vnode_get(node);
    if (node->ops && node->ops->open) {
        r = node->ops->open(node, file);
        if (r != 0) {
            file->vnode = 0;
            robo_vnode_put(node);
            robo_file_put(file);
            return r;
        }
    }
    if ((flags & ROBO_O_TRUNC) && (ff & ROBO_FFLAG_WRITE) &&
        node->type == ROBO_VNODE_FILE && node->ops && node->ops->write) {
        /* backend truncate via write of zero length at 0 is not enough;
           leave size to backend open/trunc if it honors flags */
        (void)flags;
    }
    *out = file;
    return 0;
}

int robo_vfs_open(const char *path, uint32_t flags, uint32_t mode, RoboFile **out)
{
    RoboVnode *node = 0;
    RoboVnode *parent = 0;
    char last[ROBO_NAME_MAX + 1];
    uint32_t acc = flags & 3u;
    uint32_t known = 3u | ROBO_O_CREAT | ROBO_O_TRUNC;
    int r;

    if (!out)
        return -ROBO_EINVAL;
    *out = 0;
    if (flags & ~known)
        return -ROBO_EINVAL;
    if (acc > ROBO_O_RDWR)
        return -ROBO_EINVAL;

    r = robo_vfs_lookup(path, &node);
    if (r == 0) {
        if (node->type == ROBO_VNODE_DIRECTORY && (acc == ROBO_O_WRONLY)) {
            robo_vnode_put(node);
            return -ROBO_EISDIR;
        }
        r = file_from_vnode(node, flags, out);
        robo_vnode_put(node);
        return r;
    }
    if (r != -ROBO_ENOENT || !(flags & ROBO_O_CREAT))
        return r;

    r = robo_vfs_lookup_at(0, path, ROBO_VFS_LOOKUP_PARENT, &parent, last,
                           sizeof(last));
    if (r != 0)
        return r;
    if (!robo_name_valid(last)) {
        robo_vnode_put(parent);
        return -ROBO_EINVAL;
    }
    if (!parent->ops || !parent->ops->create) {
        robo_vnode_put(parent);
        return -ROBO_ENOTSUP;
    }
    r = parent->ops->create(parent, last, ROBO_VNODE_FILE, mode, &node);
    robo_vnode_put(parent);
    if (r != 0)
        return r;
    r = file_from_vnode(node, flags, out);
    robo_vnode_put(node);
    return r;
}

int64_t robo_vfs_read(RoboFile *file, void *buffer, size_t count)
{
    return robo_file_read(file, buffer, count);
}

int64_t robo_vfs_write(RoboFile *file, const void *buffer, size_t count)
{
    return robo_file_write(file, buffer, count);
}

int robo_vfs_seek(RoboFile *file, int64_t offset, int whence)
{
    return robo_file_seek(file, offset, whence);
}

int robo_vfs_stat_path(const char *path, RoboStat *st)
{
    RoboVnode *n;
    int r;

    r = robo_vfs_lookup(path, &n);
    if (r != 0)
        return r;
    r = robo_vnode_stat(n, st);
    robo_vnode_put(n);
    return r;
}

int robo_vfs_mkdir(const char *path, uint32_t mode)
{
    RoboVnode *parent = 0;
    RoboVnode *node = 0;
    char last[ROBO_NAME_MAX + 1];
    int r;

    r = robo_vfs_lookup(path, &node);
    if (r == 0) {
        robo_vnode_put(node);
        return -ROBO_EEXIST;
    }
    r = robo_vfs_lookup_at(0, path, ROBO_VFS_LOOKUP_PARENT, &parent, last,
                           sizeof(last));
    if (r != 0)
        return r;
    if (!robo_name_valid(last)) {
        robo_vnode_put(parent);
        return -ROBO_EINVAL;
    }
    if (!parent->ops || !parent->ops->mkdir) {
        robo_vnode_put(parent);
        return -ROBO_ENOTSUP;
    }
    r = parent->ops->mkdir(parent, last, mode, &node);
    if (node)
        robo_vnode_put(node);
    robo_vnode_put(parent);
    return r;
}

int robo_vfs_rmdir(const char *path)
{
    RoboVnode *parent = 0;
    char last[ROBO_NAME_MAX + 1];
    int r;

    r = robo_vfs_lookup_at(0, path, ROBO_VFS_LOOKUP_PARENT, &parent, last,
                           sizeof(last));
    if (r != 0)
        return r;
    if (!robo_name_valid(last)) {
        robo_vnode_put(parent);
        return -ROBO_EINVAL;
    }
    if (!parent->ops || !parent->ops->rmdir) {
        robo_vnode_put(parent);
        return -ROBO_ENOTSUP;
    }
    r = parent->ops->rmdir(parent, last);
    robo_vnode_put(parent);
    return r;
}

int robo_vfs_unlink(const char *path)
{
    RoboVnode *parent = 0;
    char last[ROBO_NAME_MAX + 1];
    int r;

    r = robo_vfs_lookup_at(0, path, ROBO_VFS_LOOKUP_PARENT, &parent, last,
                           sizeof(last));
    if (r != 0)
        return r;
    if (!robo_name_valid(last)) {
        robo_vnode_put(parent);
        return -ROBO_EINVAL;
    }
    if (!parent->ops || !parent->ops->unlink) {
        robo_vnode_put(parent);
        return -ROBO_ENOTSUP;
    }
    r = parent->ops->unlink(parent, last);
    robo_vnode_put(parent);
    return r;
}

int robo_vfs_rename(const char *old_path, const char *new_path)
{
    RoboVnode *od = 0, *nd = 0;
    char on[ROBO_NAME_MAX + 1], nn[ROBO_NAME_MAX + 1];
    int r;

    r = robo_vfs_lookup_at(0, old_path, ROBO_VFS_LOOKUP_PARENT, &od, on,
                           sizeof(on));
    if (r != 0)
        return r;
    r = robo_vfs_lookup_at(0, new_path, ROBO_VFS_LOOKUP_PARENT, &nd, nn,
                           sizeof(nn));
    if (r != 0) {
        robo_vnode_put(od);
        return r;
    }
    if (!robo_name_valid(on) || !robo_name_valid(nn)) {
        robo_vnode_put(od);
        robo_vnode_put(nd);
        return -ROBO_EINVAL;
    }
    if (od->mount != nd->mount) {
        robo_vnode_put(od);
        robo_vnode_put(nd);
        return -ROBO_EXDEV;
    }
    if (!od->ops || !od->ops->rename) {
        robo_vnode_put(od);
        robo_vnode_put(nd);
        return -ROBO_ENOTSUP;
    }
    r = od->ops->rename(od, on, nd, nn);
    robo_vnode_put(od);
    robo_vnode_put(nd);
    return r;
}

int robo_vfs_create(const char *path, RoboVnodeType type, uint32_t mode)
{
    RoboFile *f;
    int r;

    if (type == ROBO_VNODE_DIRECTORY)
        return robo_vfs_mkdir(path, mode);
    r = robo_vfs_open(path, ROBO_O_CREAT | ROBO_O_WRONLY, mode, &f);
    if (r != 0)
        return r;
    robo_file_put(f);
    return 0;
}

int robo_vfs_readdir(RoboFile *directory, RoboDirEntry *entry)
{
    if (!directory || !directory->vnode || !entry)
        return -ROBO_EINVAL;
    if (directory->vnode->type != ROBO_VNODE_DIRECTORY)
        return -ROBO_ENOTDIR;
    if (!directory->vnode->ops || !directory->vnode->ops->readdir)
        return -ROBO_ENOTSUP;
    return directory->vnode->ops->readdir(directory->vnode, directory, entry);
}

#if !defined(PROCESS_HOST_TEST) && !defined(RFS_HOST_TEST)
static int mkdir_p(const char *path)
{
    char buf[ROBO_PATH_MAX + 1];
    unsigned i, n;
    int r;

    if (!path || path[0] != '/')
        return -ROBO_EINVAL;
    n = 0;
    while (path[n] && n < ROBO_PATH_MAX) {
        buf[n] = path[n];
        n++;
    }
    buf[n] = 0;
    for (i = 1; i < n; i++) {
        if (buf[i] != '/')
            continue;
        buf[i] = 0;
        r = robo_vfs_mkdir(buf, 0);
        buf[i] = '/';
        if (r != 0 && r != -ROBO_EEXIST)
            return r;
    }
    return 0;
}

int robo_vfs_import_initrd(void)
{
    unsigned i, n;
    char name[ROBFS_NAME];
    const void *data;
    uint32_t size;
    RoboFile *f;
    int r;

    if (!robo_fs_ready())
        return -ROBO_ENOENT;
    n = robo_fs_file_count();
    for (i = 0; i < n; i++) {
        int64_t wr;
        if (!robo_fs_file_at(i, name, ROBFS_NAME, &data, &size))
            continue;
        r = mkdir_p(name);
        if (r != 0 && r != -ROBO_EEXIST)
            return r;
        r = robo_vfs_open(name, ROBO_O_CREAT | ROBO_O_RDWR | ROBO_O_TRUNC, 0, &f);
        if (r != 0)
            return r;
        if (size) {
            wr = robo_vfs_write(f, data, size);
            if (wr < 0 || (uint32_t)wr != size) {
                robo_file_put(f);
                return wr < 0 ? (int)wr : -ROBO_EIO;
            }
        }
        robo_file_put(f);
    }
    return 0;
}
#endif

#if defined(PROCESS_HOST_TEST) || defined(RFS_HOST_TEST)
int robo_vfs_import_initrd(void)
{
    return -ROBO_ENOTSUP;
}
#endif
