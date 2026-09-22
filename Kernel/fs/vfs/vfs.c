#include "fs/vfs/vfs.h"
#include "syscall/syscall_errors.h"
#include "syscall/syscall_numbers.h"
#include <stdint.h>

#if defined(PROCESS_HOST_TEST) || defined(RFS_HOST_TEST)
#include <stdlib.h>
#define kmalloc malloc
#define kfree free
static uint64_t VfsLock(void) { return 0; }
static void VfsUnlock(uint64_t f) { (void)f; }
#else
#include "mm/heap/heap.h"
#include "kio.h"
#include "fs/robofs.h"
static uint64_t VfsLock(void)
{
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
static void VfsUnlock(uint64_t flags)
{
    __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
}
#endif

static RoboFilesystem *gFs[RoboVfsMaxFs];
static unsigned gNfs;
static RoboMount *gRootMount;
static uint32_t gNextFsId = 1;

/*
 * Lock hierarchy (single CPU, interrupt flag):
 *   VfsLock  — filesystem registry and root-mount pointer
 *   RfsLock  — RoboFS metadata mutations
 * Never acquire VfsLock while holding RfsLock. Path walk is currently
 * unsynchronized; backends serialize their own mutations.
 */

static uint32_t OpenToFflags(uint32_t flags)
{
    uint32_t acc = flags & 3u;
    uint32_t f = 0;

    if (acc == RoboORdonly)
        f = RoboFflagRead;
    else if (acc == RoboOWronly)
        f = RoboFflagWrite;
    else     if (acc == RoboORdwr)
        f = RoboFflagRead | RoboFflagWrite;
    if (flags & RoboOTrunc)
        f |= RoboFflagTrunc;
    return f;
}

static RoboFileType VnodeFileType(RoboVnodeType t)
{
    switch (t) {
    case RoboVnodeDirectory: return RoboFileDirectory;
    case RoboVnodePipe: return RoboFilePipe;
    case RoboVnodeSocket: return RoboFileSocket;
    case RoboVnodeDevice: return RoboFileDevice;
    default: return RoboFileRegular;
    }
}

static void CrossMount(RoboVnode **node)
{
    RoboVnode *n = *node;
    if (n && n->cover && n->cover->root) {
        VnodeGet(n->cover->root);
        VnodePut(n);
        *node = n->cover->root;
    }
}

int VfsInit(void)
{
    unsigned i;
    gNfs = 0;
    gRootMount = 0;
    gNextFsId = 1;
    for (i = 0; i < RoboVfsMaxFs; i++)
        gFs[i] = 0;
    return 0;
}

int VfsRegister(RoboFilesystem *fs)
{
    unsigned i;
    uint64_t lockf;

    if (!fs || !fs->name || !fs->ops)
        return -RoboEinval;
    lockf = VfsLock();
    for (i = 0; i < gNfs; i++) {
        const char *a = gFs[i]->name;
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
            VfsUnlock(lockf);
            return -RoboEexist;
        }
    }
    if (gNfs >= RoboVfsMaxFs) {
        VfsUnlock(lockf);
        return -RoboEnospc;
    }
    if (!fs->id)
        fs->id = gNextFsId++;
    gFs[gNfs++] = fs;
    VfsUnlock(lockf);
    return 0;
}

const RoboFilesystem *VfsFindFs(const char *name)
{
    unsigned i, k;

    if (!name)
        return 0;
    for (i = 0; i < gNfs; i++) {
        const char *a = gFs[i]->name;
        int same = 1;
        for (k = 0; a[k] || name[k]; k++) {
            if (a[k] != name[k]) {
                same = 0;
                break;
            }
        }
        if (same)
            return gFs[i];
    }
    return 0;
}

int VfsMountRoot(const char *fsName, RoboBlockDevice *dev, uint32_t flags)
{
    const RoboFilesystem *fs;
    RoboMount *m;
    int r;

    fs = VfsFindFs(fsName);
    if (!fs || !fs->ops || !fs->ops->mount)
        return -RoboEinval;
    if (gRootMount)
        return -RoboEbusy;
    r = fs->ops->mount(dev, flags, &m);
    if (r != 0)
        return r;
    m->fs = fs;
    m->path[0] = '/';
    m->path[1] = 0;
    gRootMount = m;
    return 0;
}

int VfsUnmountRoot(void)
{
    RoboMount *m = gRootMount;
    int r = 0;

    if (!m)
        return -RoboEinval;
    gRootMount = 0;
    if (m->fs && m->fs->ops && m->fs->ops->unmount)
        r = m->fs->ops->unmount(m);
    else
        MountPut(m);
    return r;
}

RoboVnode *VfsRoot(void)
{
    if (!gRootMount || !gRootMount->root)
        return 0;
    VnodeGet(gRootMount->root);
    return gRootMount->root;
}

int VfsLookupAt(RoboVnode *start, const char *path, uint32_t flags,
                       RoboVnode **out, char *last, unsigned lastMax)
{
    RoboVnode *cur, *next;
    const char *p;
    char name[RoboNameMax + 1];
    int r, got;
    int wantParent = (flags & RoboVfsLookupParent) != 0;

    if (!out)
        return -RoboEinval;
    *out = 0;
    if (last && lastMax)
        last[0] = 0;
    if (!PathValid(path))
        return -RoboEinval;

    if (path[0] == '/') {
        cur = VfsRoot();
        if (start)
            (void)start;
    } else {
        cur = start ? start : VfsRoot();
        if (start)
            VnodeGet(start);
    }
    if (!cur)
        return -RoboEnoent;

    p = path;
    if (*p == '/')
        p++;

    if (!*p) {
        if (wantParent) {
            VnodePut(cur);
            return -RoboEinval;
        }
        *out = cur;
        return 0;
    }

    for (;;) {
        int trailing = 0;
        int isLast;

        got = PathNext(&p, name, sizeof(name));
        if (got < 0) {
            VnodePut(cur);
            return got;
        }
        if (!got || !name[0]) {
            if (cur->type != RoboVnodeDirectory) {
                VnodePut(cur);
                return -RoboEnotdir;
            }
            *out = cur;
            return 0;
        }

        while (*p == '/') {
            trailing = 1;
            p++;
        }
        isLast = !*p;

        if (wantParent && isLast) {
            if (last && lastMax) {
                unsigned i;
                for (i = 0; name[i] && i + 1 < lastMax; i++)
                    last[i] = name[i];
                last[i] = 0;
            }
            *out = cur;
            return 0;
        }

        if (name[0] == '.' && !name[1]) {
            if (isLast) {
                if (trailing && cur->type != RoboVnodeDirectory) {
                    VnodePut(cur);
                    return -RoboEnotdir;
                }
                *out = cur;
                return 0;
            }
            continue;
        }
        if (name[0] == '.' && name[1] == '.' && !name[2]) {
            next = cur->parent ? cur->parent : cur;
            VnodeGet(next);
            VnodePut(cur);
            cur = next;
            CrossMount(&cur);
            if (isLast) {
                if (trailing && cur->type != RoboVnodeDirectory) {
                    VnodePut(cur);
                    return -RoboEnotdir;
                }
                *out = cur;
                return 0;
            }
            continue;
        }

        CrossMount(&cur);
        if (cur->type != RoboVnodeDirectory) {
            VnodePut(cur);
            return -RoboEnotdir;
        }
        r = VnodeLookup(cur, name, &next);
        if (r != 0) {
            VnodePut(cur);
            return r;
        }
        if (next->parent != cur) {
            if (next->parent)
                VnodePut(next->parent);
            VnodeGet(cur);
            next->parent = cur;
        }
        VnodePut(cur);
        cur = next;
        CrossMount(&cur);
        if (isLast) {
            if (trailing && cur->type != RoboVnodeDirectory) {
                VnodePut(cur);
                return -RoboEnotdir;
            }
            *out = cur;
            return 0;
        }
    }
}

int VfsLookup(const char *path, RoboVnode **out)
{
    return VfsLookupAt(0, path, 0, out, 0, 0);
}

static int64_t VfsFileRead(RoboFile *file, void *buffer, size_t count)
{
    int64_t n;

    if (!file || !file->vnode)
        return -(int64_t)RoboEbadf;
    n = VnodeRead(file->vnode, file, buffer, count, file->offset);
    if (n > 0)
        file->offset += (uint64_t)n;
    return n;
}

static int64_t VfsFileWrite(RoboFile *file, const void *buffer, size_t count)
{
    int64_t n;

    if (!file || !file->vnode)
        return -(int64_t)RoboEbadf;
    n = VnodeWrite(file->vnode, file, buffer, count, file->offset);
    if (n > 0)
        file->offset += (uint64_t)n;
    return n;
}

static int VfsFileClose(RoboFile *file)
{
    if (!file)
        return 0;
    if (file->vnode) {
        if (file->vnode->ops && file->vnode->ops->close)
            file->vnode->ops->close(file->vnode, file);
        VnodePut(file->vnode);
        file->vnode = 0;
    }
    return 0;
}

static int VfsFileSeek(RoboFile *file, int64_t offset, int whence)
{
    int64_t base, next;
    uint64_t size;

    if (!file || !file->vnode)
        return -RoboEbadf;
    size = file->vnode->size;
    if (whence == RoboSeekSet)
        base = 0;
    else if (whence == RoboSeekCur)
        base = (int64_t)file->offset;
    else if (whence == RoboSeekEnd)
        base = (int64_t)size;
    else
        return -RoboEinval;
    if (offset > 0 && base > (int64_t)INT64_MAX - offset)
        return -RoboEinval;
    if (offset < 0 && base < (int64_t)INT64_MIN - offset)
        return -RoboEinval;
    next = base + offset;
    if (next < 0)
        return -RoboEinval;
    file->offset = (uint64_t)next;
    return 0;
}

static const RoboFileOps gVfsFileOps = {
    .read = VfsFileRead,
    .write = VfsFileWrite,
    .close = VfsFileClose,
    .seek = VfsFileSeek,
    .ioctl = 0,
    .poll = 0,
};

const RoboFileOps *VfsFileOps(void)
{
    return &gVfsFileOps;
}

static int FileFromVnode(RoboVnode *node, uint32_t flags, RoboFile **out)
{
    RoboFile *file;
    uint32_t ff = OpenToFflags(flags);
    int r;

    if (!ff)
        return -RoboEinval;
    file = FileCreate(VnodeFileType(node->type), &gVfsFileOps, 0, ff);
    if (!file)
        return -RoboEnomem;
    file->vnode = node;
    VnodeGet(node);
    if (node->ops && node->ops->open) {
        r = node->ops->open(node, file);
        if (r != 0) {
            file->vnode = 0;
            VnodePut(node);
            FilePut(file);
            return r;
        }
    }
    if ((flags & RoboOTrunc) && (ff & RoboFflagWrite) &&
        node->type == RoboVnodeFile && node->ops && node->ops->write) {
        /* backend truncate via write of zero length at 0 is not enough;
           leave size to backend open/trunc if it honors flags */
        (void)flags;
    }
    *out = file;
    return 0;
}

int VfsOpen(const char *path, uint32_t flags, uint32_t mode, RoboFile **out)
{
    RoboVnode *node = 0;
    RoboVnode *parent = 0;
    char last[RoboNameMax + 1];
    uint32_t acc = flags & 3u;
    uint32_t known = 3u | RoboOCreat | RoboOTrunc;
    int r;

    if (!out)
        return -RoboEinval;
    *out = 0;
    if (flags & ~known)
        return -RoboEinval;
    if (acc > RoboORdwr)
        return -RoboEinval;

    r = VfsLookup(path, &node);
    if (r == 0) {
        if (node->type == RoboVnodeDirectory && (acc == RoboOWronly)) {
            VnodePut(node);
            return -RoboEisdir;
        }
        r = FileFromVnode(node, flags, out);
        VnodePut(node);
        return r;
    }
    if (r != -RoboEnoent || !(flags & RoboOCreat))
        return r;

    r = VfsLookupAt(0, path, RoboVfsLookupParent, &parent, last,
                           sizeof(last));
    if (r != 0)
        return r;
    if (!NameValid(last)) {
        VnodePut(parent);
        return -RoboEinval;
    }
    if (!parent->ops || !parent->ops->create) {
        VnodePut(parent);
        return -RoboEnotsup;
    }
    r = parent->ops->create(parent, last, RoboVnodeFile, mode, &node);
    VnodePut(parent);
    if (r != 0)
        return r;
    r = FileFromVnode(node, flags, out);
    VnodePut(node);
    return r;
}

int64_t VfsRead(RoboFile *file, void *buffer, size_t count)
{
    return FileRead(file, buffer, count);
}

int64_t VfsWrite(RoboFile *file, const void *buffer, size_t count)
{
    return FileWrite(file, buffer, count);
}

int VfsSeek(RoboFile *file, int64_t offset, int whence)
{
    return FileSeek(file, offset, whence);
}

int VfsStatPath(const char *path, RoboStat *st)
{
    RoboVnode *n;
    int r;

    r = VfsLookup(path, &n);
    if (r != 0)
        return r;
    r = VnodeStat(n, st);
    VnodePut(n);
    return r;
}

int VfsMkdir(const char *path, uint32_t mode)
{
    RoboVnode *parent = 0;
    RoboVnode *node = 0;
    char last[RoboNameMax + 1];
    int r;

    r = VfsLookup(path, &node);
    if (r == 0) {
        VnodePut(node);
        return -RoboEexist;
    }
    r = VfsLookupAt(0, path, RoboVfsLookupParent, &parent, last,
                           sizeof(last));
    if (r != 0)
        return r;
    if (!NameValid(last)) {
        VnodePut(parent);
        return -RoboEinval;
    }
    if (!parent->ops || !parent->ops->mkdir) {
        VnodePut(parent);
        return -RoboEnotsup;
    }
    r = parent->ops->mkdir(parent, last, mode, &node);
    if (node)
        VnodePut(node);
    VnodePut(parent);
    return r;
}

int VfsRmdir(const char *path)
{
    RoboVnode *parent = 0;
    char last[RoboNameMax + 1];
    int r;

    r = VfsLookupAt(0, path, RoboVfsLookupParent, &parent, last,
                           sizeof(last));
    if (r != 0)
        return r;
    if (!NameValid(last)) {
        VnodePut(parent);
        return -RoboEinval;
    }
    if (!parent->ops || !parent->ops->rmdir) {
        VnodePut(parent);
        return -RoboEnotsup;
    }
    r = parent->ops->rmdir(parent, last);
    VnodePut(parent);
    return r;
}

int VfsUnlink(const char *path)
{
    RoboVnode *parent = 0;
    char last[RoboNameMax + 1];
    int r;

    r = VfsLookupAt(0, path, RoboVfsLookupParent, &parent, last,
                           sizeof(last));
    if (r != 0)
        return r;
    if (!NameValid(last)) {
        VnodePut(parent);
        return -RoboEinval;
    }
    if (!parent->ops || !parent->ops->unlink) {
        VnodePut(parent);
        return -RoboEnotsup;
    }
    r = parent->ops->unlink(parent, last);
    VnodePut(parent);
    return r;
}

int VfsRename(const char *oldPath, const char *newPath)
{
    RoboVnode *od = 0, *nd = 0;
    char on[RoboNameMax + 1], nn[RoboNameMax + 1];
    int r;

    r = VfsLookupAt(0, oldPath, RoboVfsLookupParent, &od, on,
                           sizeof(on));
    if (r != 0)
        return r;
    r = VfsLookupAt(0, newPath, RoboVfsLookupParent, &nd, nn,
                           sizeof(nn));
    if (r != 0) {
        VnodePut(od);
        return r;
    }
    if (!NameValid(on) || !NameValid(nn)) {
        VnodePut(od);
        VnodePut(nd);
        return -RoboEinval;
    }
    if (od->mount != nd->mount) {
        VnodePut(od);
        VnodePut(nd);
        return -RoboExdev;
    }
    if (!od->ops || !od->ops->rename) {
        VnodePut(od);
        VnodePut(nd);
        return -RoboEnotsup;
    }
    r = od->ops->rename(od, on, nd, nn);
    VnodePut(od);
    VnodePut(nd);
    return r;
}

int VfsCreate(const char *path, RoboVnodeType type, uint32_t mode)
{
    RoboFile *f;
    int r;

    if (type == RoboVnodeDirectory)
        return VfsMkdir(path, mode);
    r = VfsOpen(path, RoboOCreat | RoboOWronly, mode, &f);
    if (r != 0)
        return r;
    FilePut(f);
    return 0;
}

int VfsReaddir(RoboFile *directory, RoboDirEntry *entry)
{
    if (!directory || !directory->vnode || !entry)
        return -RoboEinval;
    if (directory->vnode->type != RoboVnodeDirectory)
        return -RoboEnotdir;
    if (!directory->vnode->ops || !directory->vnode->ops->readdir)
        return -RoboEnotsup;
    return directory->vnode->ops->readdir(directory->vnode, directory, entry);
}

#if !defined(PROCESS_HOST_TEST) && !defined(RFS_HOST_TEST)
static int MkdirP(const char *path)
{
    char buf[RoboPathMax + 1];
    unsigned i, n;
    int r;

    if (!path || path[0] != '/')
        return -RoboEinval;
    n = 0;
    while (path[n] && n < RoboPathMax) {
        buf[n] = path[n];
        n++;
    }
    buf[n] = 0;
    for (i = 1; i < n; i++) {
        if (buf[i] != '/')
            continue;
        buf[i] = 0;
        r = VfsMkdir(buf, 0);
        buf[i] = '/';
        if (r != 0 && r != -RoboEexist)
            return r;
    }
    return 0;
}

int VfsImportInitrd(void)
{
    unsigned i, n;
    unsigned skipped = 0;
    char name[ROBFS_NAME];
    const void *data;
    uint32_t size;
    RoboFile *f;
    int r;

    if (!FsReady())
        return -RoboEnoent;
    n = FsFileCount();
    for (i = 0; i < n; i++) {
        int64_t wr;
        if (!FsFileAt(i, name, ROBFS_NAME, &data, &size))
            continue;
        r = MkdirP(name);
        if (r != 0 && r != -RoboEexist)
            return r;
        r = VfsOpen(name, RoboOCreat | RoboORdwr | RoboOTrunc, 0, &f);
        if (r != 0)
            return r;
        if (size) {
            wr = VfsWrite(f, data, size);
            if (wr < 0 || (uint32_t)wr != size) {
                /*
                 * A file this filesystem cannot hold: too large for its block map, or the volume is out
                 * of room. Abandoning the walk here would leave *every later entry* of the image missing --
                 * one large binary would silently take the fonts, the manifest and everything after it with
                 * it -- so the part that was written is removed, the entry is named in the log, and the walk
                 * continues. The file is still on the image, and `Exec` reads an image file it cannot
                 * find in the VFS, so a program that hit this is runnable; it is only *not* reachable by path
                 * through the filesystem.
                 */
                FilePut(f);
                (void)VfsUnlink(name);
                kinfo("VFS_IMPORT_SKIP  %s\n", name);
                skipped++;
                continue;
            }
        }
        FilePut(f);
    }
    return (int)skipped;
}
#endif

#if defined(PROCESS_HOST_TEST) || defined(RFS_HOST_TEST)
int VfsImportInitrd(void)
{
    return -RoboEnotsup;
}
#endif
