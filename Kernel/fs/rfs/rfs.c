#include "fs/rfs/rfs.h"
#include "fs/rfs/rfs_disk.h"
#include "syscall/syscall_errors.h"

#if defined(PROCESS_HOST_TEST) || defined(RFS_HOST_TEST)
#include <stdlib.h>
#define kmalloc malloc
#define kfree free
static uint64_t now(void) { return 0; }
static uint64_t RfsLock(void) { return 0; }
static void RfsUnlock(uint64_t f) { (void)f; }
#else
#include "mm/heap/heap.h"
#include "time/time.h"
static uint64_t now(void) { return TimeNow(); }
static uint64_t RfsLock(void)
{
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
/* RfsLock: bitmap, object table, directory and file metadata.
 * Do not nest with VfsLock. */
static void RfsUnlock(uint64_t flags)
{
    __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
}
#endif

typedef struct RoboRfs {
    RoboBlockDevice *dev;
    uint32_t bs;
    uint32_t total;
    uint32_t bitmapStart;
    uint32_t bitmapBlocks;
    uint32_t objStart;
    uint32_t objBlocks;
    uint32_t objCount;
    uint32_t dataStart;
    uint32_t rootId;
    uint32_t freeBlocks;
    uint32_t freeObjects;
    uint8_t *bitmap;
    uint8_t *blk;
} RoboRfs;

typedef struct {
    RoboRfs *fs;
    uint32_t id;
} RfsPriv;

typedef struct {
    uint32_t type;
    uint32_t mode;
    uint64_t size;
    uint64_t ctime;
    uint64_t mtime;
    uint64_t atime;
    uint32_t nlink;
    uint32_t flags;
    uint32_t direct[ROBOFS_DIRECT];
    uint32_t indirect;
} RfsObj;

static RoboVnode *MakeVnode(RoboRfs *fs, uint32_t id, const RfsObj *o,
                             RoboMount *mnt);
static const RoboVnodeOps gRfsVops;
static RoboFilesystem gRfsType;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void wr64(uint8_t *p, uint64_t v)
{
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}

static void zmem(void *dst, unsigned n)
{
    unsigned char *p = (unsigned char *)dst;
    while (n--)
        *p++ = 0;
}

static void cmem(void *dst, const void *src, unsigned n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
        *d++ = *s++;
}

static uint32_t SbCsum(const uint8_t *sb)
{
    uint32_t s = 0, i;
    for (i = 0; i < 68; i += 4)
        s ^= rd32(sb + i);
    return s;
}

static int MulOk(uint32_t a, uint32_t b, uint32_t *out)
{
    if (b && a > 0xFFFFFFFFu / b)
        return 0;
    *out = a * b;
    return 1;
}

static int ReadBlk(RoboRfs *fs, uint32_t lba, void *buf)
{
    return BdevRead(fs->dev, lba, buf);
}

static int WriteBlk(RoboRfs *fs, uint32_t lba, const void *buf)
{
    return BdevWrite(fs->dev, lba, buf);
}

static int FlushBitmap(RoboRfs *fs)
{
    uint32_t i;
    int r;
    for (i = 0; i < fs->bitmapBlocks; i++) {
        r = WriteBlk(fs, fs->bitmapStart + i, fs->bitmap + i * fs->bs);
        if (r != 0)
            return r;
    }
    return 0;
}

static int FlushSb(RoboRfs *fs)
{
    zmem(fs->blk, fs->bs);
    wr32(fs->blk + 0, ROBOFS_DISK_MAGIC);
    wr32(fs->blk + 4, (ROBOFS_DISK_MINOR << 16) | ROBOFS_DISK_MAJOR);
    wr32(fs->blk + 8, fs->bs);
    wr32(fs->blk + 12, fs->total);
    wr32(fs->blk + 16, fs->freeBlocks);
    wr32(fs->blk + 20, fs->objCount);
    wr32(fs->blk + 24, fs->freeObjects);
    wr32(fs->blk + 28, fs->bitmapStart);
    wr32(fs->blk + 32, fs->bitmapBlocks);
    wr32(fs->blk + 36, fs->objStart);
    wr32(fs->blk + 40, fs->objBlocks);
    wr32(fs->blk + 44, fs->dataStart);
    wr32(fs->blk + 48, fs->rootId);
    wr32(fs->blk + 52, 0);
    wr32(fs->blk + 68, SbCsum(fs->blk));
    return WriteBlk(fs, 0, fs->blk);
}

static int BitGet(RoboRfs *fs, uint32_t blk)
{
    return (fs->bitmap[blk / 8] >> (blk % 8)) & 1;
}

static void BitSet(RoboRfs *fs, uint32_t blk, int on)
{
    uint32_t byte = blk / 8;
    uint32_t bit = blk % 8;
    if (on)
        fs->bitmap[byte] = (uint8_t)(fs->bitmap[byte] | (1u << bit));
    else
        fs->bitmap[byte] = (uint8_t)(fs->bitmap[byte] & ~(1u << bit));
}

static int AllocBlock(RoboRfs *fs, uint32_t *out)
{
    uint32_t i;
    for (i = fs->dataStart; i < fs->total; i++) {
        if (!BitGet(fs, i)) {
            BitSet(fs, i, 1);
            fs->freeBlocks--;
            *out = i;
            return FlushBitmap(fs);
        }
    }
    return -RoboEnospc;
}

static int FreeBlock(RoboRfs *fs, uint32_t blk)
{
    if (blk < fs->dataStart || blk >= fs->total)
        return -RoboEio;
    if (!BitGet(fs, blk))
        return -RoboEio;
    BitSet(fs, blk, 0);
    fs->freeBlocks++;
    return FlushBitmap(fs);
}

static int LoadObj(RoboRfs *fs, uint32_t id, RfsObj *o)
{
    uint32_t byte, lba, off, i;
    int r;

    if (!id || id >= fs->objCount)
        return -RoboEinval;
    if (!MulOk(id, ROBOFS_OBJ_SIZE, &byte))
        return -RoboEio;
    lba = fs->objStart + byte / fs->bs;
    off = byte % fs->bs;
    r = ReadBlk(fs, lba, fs->blk);
    if (r != 0)
        return r;
    o->type = rd32(fs->blk + off + 0);
    o->mode = rd32(fs->blk + off + 4);
    o->size = rd64(fs->blk + off + 8);
    o->ctime = rd64(fs->blk + off + 16);
    o->mtime = rd64(fs->blk + off + 24);
    o->atime = rd64(fs->blk + off + 32);
    o->nlink = rd32(fs->blk + off + 40);
    o->flags = rd32(fs->blk + off + 44);
    for (i = 0; i < ROBOFS_DIRECT; i++)
        o->direct[i] = rd32(fs->blk + off + 48 + i * 4);
    o->indirect = rd32(fs->blk + off + 80);
    return 0;
}

static int StoreObj(RoboRfs *fs, uint32_t id, const RfsObj *o)
{
    uint32_t byte, lba, off, i;
    int r;

    if (!id || id >= fs->objCount)
        return -RoboEinval;
    if (!MulOk(id, ROBOFS_OBJ_SIZE, &byte))
        return -RoboEio;
    lba = fs->objStart + byte / fs->bs;
    off = byte % fs->bs;
    r = ReadBlk(fs, lba, fs->blk);
    if (r != 0)
        return r;
    wr32(fs->blk + off + 0, o->type);
    wr32(fs->blk + off + 4, o->mode);
    wr64(fs->blk + off + 8, o->size);
    wr64(fs->blk + off + 16, o->ctime);
    wr64(fs->blk + off + 24, o->mtime);
    wr64(fs->blk + off + 32, o->atime);
    wr32(fs->blk + off + 40, o->nlink);
    wr32(fs->blk + off + 44, o->flags);
    for (i = 0; i < ROBOFS_DIRECT; i++)
        wr32(fs->blk + off + 48 + i * 4, o->direct[i]);
    wr32(fs->blk + off + 80, o->indirect);
    return WriteBlk(fs, lba, fs->blk);
}

static int AllocObj(RoboRfs *fs, uint32_t *out)
{
    uint32_t id;
    RfsObj o;
    int r;

    for (id = 1; id < fs->objCount; id++) {
        r = LoadObj(fs, id, &o);
        if (r != 0)
            return r;
        if (o.type == ROBOFS_OBJ_UNUSED) {
            zmem(&o, (unsigned)sizeof(o));
            o.nlink = 1;
            o.ctime = o.mtime = o.atime = now();
            *out = id;
            fs->freeObjects--;
            r = StoreObj(fs, id, &o);
            if (r != 0)
                return r;
            return FlushSb(fs);
        }
    }
    return -RoboEnospc;
}

static int FreeFileBlocks(RoboRfs *fs, RfsObj *o)
{
    uint32_t i, nptr, p;
    int r;

    for (i = 0; i < ROBOFS_DIRECT; i++) {
        if (o->direct[i]) {
            r = FreeBlock(fs, o->direct[i]);
            if (r != 0)
                return r;
            o->direct[i] = 0;
        }
    }
    if (o->indirect) {
        r = ReadBlk(fs, o->indirect, fs->blk);
        if (r != 0)
            return r;
        nptr = fs->bs / 4;
        for (i = 0; i < nptr; i++) {
            p = rd32(fs->blk + i * 4);
            if (p) {
                r = FreeBlock(fs, p);
                if (r != 0)
                    return r;
            }
        }
        r = FreeBlock(fs, o->indirect);
        if (r != 0)
            return r;
        o->indirect = 0;
    }
    o->size = 0;
    return 0;
}

static int MapBlock(RoboRfs *fs, RfsObj *o, uint32_t lb, int alloc, uint32_t *out)
{
    uint32_t blk = 0, nptr, idx;
    int r, dirty = 0;

    if (lb < ROBOFS_DIRECT) {
        blk = o->direct[lb];
        if (!blk && alloc) {
            r = AllocBlock(fs, &blk);
            if (r != 0)
                return r;
            zmem(fs->blk, fs->bs);
            r = WriteBlk(fs, blk, fs->blk);
            if (r != 0)
                return r;
            o->direct[lb] = blk;
            dirty = 1;
        }
        *out = blk;
        return dirty ? 1 : 0;
    }
    idx = lb - ROBOFS_DIRECT;
    nptr = fs->bs / 4;
    if (idx >= nptr)
        return -RoboEio;
    if (!o->indirect) {
        if (!alloc) {
            *out = 0;
            return 0;
        }
        r = AllocBlock(fs, &o->indirect);
        if (r != 0)
            return r;
        zmem(fs->blk, fs->bs);
        r = WriteBlk(fs, o->indirect, fs->blk);
        if (r != 0)
            return r;
        dirty = 1;
    }
    r = ReadBlk(fs, o->indirect, fs->blk);
    if (r != 0)
        return r;
    blk = rd32(fs->blk + idx * 4);
    if (!blk && alloc) {
        r = AllocBlock(fs, &blk);
        if (r != 0)
            return r;
        {
            uint8_t *zero = (uint8_t *)kmalloc(fs->bs);
            if (!zero)
                return -RoboEnomem;
            zmem(zero, fs->bs);
            r = WriteBlk(fs, blk, zero);
            kfree(zero);
            if (r != 0)
                return r;
        }
        wr32(fs->blk + idx * 4, blk);
        r = WriteBlk(fs, o->indirect, fs->blk);
        if (r != 0)
            return r;
        dirty = 1;
    }
    *out = blk;
    return dirty ? 1 : 0;
}

static int NameEq(const char *a, const char *b)
{
    unsigned i;
    for (i = 0; i <= ROBOFS_NAME_MAX; i++) {
        if (a[i] != b[i])
            return 0;
        if (!a[i])
            return 1;
    }
    return 1;
}

static uint32_t DirSlots(const RfsObj *o, uint32_t bs)
{
    return (uint32_t)(o->size / ROBOFS_DIRENT_SIZE);
}

static int DirGet(RoboRfs *fs, RfsObj *dir, uint32_t slot,
                   uint32_t *id, uint32_t *type, char *name)
{
    uint32_t lb, off, blk, i;
    uint8_t *buf;
    int r;

    lb = (slot * ROBOFS_DIRENT_SIZE) / fs->bs;
    off = (slot * ROBOFS_DIRENT_SIZE) % fs->bs;
    r = MapBlock(fs, dir, lb, 0, &blk);
    if (r < 0)
        return r;
    if (!blk)
        return -RoboEnoent;
    buf = fs->blk;
    r = ReadBlk(fs, blk, buf);
    if (r != 0)
        return r;
    *id = rd32(buf + off);
    *type = rd32(buf + off + 4);
    for (i = 0; i < ROBOFS_NAME_MAX; i++)
        name[i] = (char)buf[off + 8 + i];
    name[ROBOFS_NAME_MAX] = 0;
    return 0;
}

static int DirSet(RoboRfs *fs, RfsObj *dir, uint32_t slot,
                   uint32_t id, uint32_t type, const char *name)
{
    uint32_t lb, off, blk, i;
    int r, dirty;

    lb = (slot * ROBOFS_DIRENT_SIZE) / fs->bs;
    off = (slot * ROBOFS_DIRENT_SIZE) % fs->bs;
    dirty = MapBlock(fs, dir, lb, 1, &blk);
    if (dirty < 0)
        return dirty;
    r = ReadBlk(fs, blk, fs->blk);
    if (r != 0)
        return r;
    wr32(fs->blk + off, id);
    wr32(fs->blk + off + 4, type);
    for (i = 0; i < ROBOFS_NAME_MAX; i++)
        fs->blk[off + 8 + i] = name && name[i] ? (uint8_t)name[i] : 0;
    return WriteBlk(fs, blk, fs->blk);
}

static int DirLookup(RoboRfs *fs, RfsObj *dir, const char *name,
                      uint32_t *id, uint32_t *type, uint32_t *slot)
{
    uint32_t i, n, eid, et;
    char en[ROBOFS_NAME_MAX + 1];
    int r;

    n = DirSlots(dir, fs->bs);
    for (i = 0; i < n; i++) {
        r = DirGet(fs, dir, i, &eid, &et, en);
        if (r != 0)
            return r;
        if (!eid)
            continue;
        if (NameEq(en, name)) {
            *id = eid;
            *type = et;
            if (slot)
                *slot = i;
            return 0;
        }
    }
    return -RoboEnoent;
}

static int DirInsert(RoboRfs *fs, uint32_t dirId, const char *name,
                      uint32_t child, uint32_t type)
{
    RfsObj dir;
    uint32_t i, n, eid, et, slot;
    char en[ROBOFS_NAME_MAX + 1];
    int r;

    r = LoadObj(fs, dirId, &dir);
    if (r != 0)
        return r;
    if (dir.type != ROBOFS_OBJ_DIR)
        return -RoboEnotdir;
    if (DirLookup(fs, &dir, name, &eid, &et, &slot) == 0)
        return -RoboEexist;
    n = DirSlots(&dir, fs->bs);
    for (i = 0; i < n; i++) {
        r = DirGet(fs, &dir, i, &eid, &et, en);
        if (r != 0)
            return r;
        if (!eid) {
            r = DirSet(fs, &dir, i, child, type, name);
            if (r != 0)
                return r;
            dir.mtime = now();
            return StoreObj(fs, dirId, &dir);
        }
    }
    r = DirSet(fs, &dir, n, child, type, name);
    if (r != 0)
        return r;
    dir.size += ROBOFS_DIRENT_SIZE;
    dir.mtime = now();
    return StoreObj(fs, dirId, &dir);
}

static int DirRemove(RoboRfs *fs, uint32_t dirId, const char *name,
                      uint32_t expectType)
{
    RfsObj dir;
    uint32_t id, type, slot;
    int r;

    r = LoadObj(fs, dirId, &dir);
    if (r != 0)
        return r;
    if (dir.type != ROBOFS_OBJ_DIR)
        return -RoboEnotdir;
    r = DirLookup(fs, &dir, name, &id, &type, &slot);
    if (r != 0)
        return r;
    if (type != expectType)
        return expectType == ROBOFS_OBJ_DIR ? -RoboEnotdir : -RoboEisdir;
    r = DirSet(fs, &dir, slot, 0, 0, "");
    if (r != 0)
        return r;
    dir.mtime = now();
    r = StoreObj(fs, dirId, &dir);
    if (r != 0)
        return r;
    return (int)id;
}

static RoboVnode *MakeVnode(RoboRfs *fs, uint32_t id, const RfsObj *o,
                             RoboMount *mnt)
{
    RfsPriv *p;
    RoboVnode *n;

    p = (RfsPriv *)kmalloc(sizeof(*p));
    if (!p)
        return 0;
    p->fs = fs;
    p->id = id;
    n = VnodeCreate(o->type == ROBOFS_OBJ_DIR ? RoboVnodeDirectory
                                                    : RoboVnodeFile,
                          &gRfsVops, p);
    if (!n) {
        kfree(p);
        return 0;
    }
    n->id = id;
    n->size = o->size;
    n->mount = mnt;
    return n;
}

static void RfsRelease(RoboVnode *node)
{
    if (node && node->fsPrivate) {
        kfree(node->fsPrivate);
        node->fsPrivate = 0;
    }
}

static int RfsLookup(RoboVnode *dir, const char *name, RoboVnode **out)
{
    RfsPriv *p = (RfsPriv *)dir->fsPrivate;
    RfsObj o;
    uint32_t id, type;
    int r;

    r = LoadObj(p->fs, p->id, &o);
    if (r != 0)
        return r;
    r = DirLookup(p->fs, &o, name, &id, &type, 0);
    if (r != 0)
        return r;
    r = LoadObj(p->fs, id, &o);
    if (r != 0)
        return r;
    *out = MakeVnode(p->fs, id, &o, dir->mount);
    return *out ? 0 : -RoboEnomem;
}

static int RfsOpen(RoboVnode *node, RoboFile *file)
{
    RfsPriv *p = (RfsPriv *)node->fsPrivate;
    RfsObj o;
    int r;

    r = LoadObj(p->fs, p->id, &o);
    if (r != 0)
        return r;
    if ((file->flags & RoboFflagTrunc) && (file->flags & RoboFflagWrite) &&
        o.type == ROBOFS_OBJ_FILE) {
        r = FreeFileBlocks(p->fs, &o);
        if (r != 0)
            return r;
        o.mtime = now();
        r = StoreObj(p->fs, p->id, &o);
        if (r != 0)
            return r;
        r = FlushSb(p->fs);
        node->size = 0;
    }
    return r;
}

static int64_t RfsRead(RoboVnode *node, RoboFile *file, void *buffer,
                        size_t count, uint64_t offset)
{
    RfsPriv *p = (RfsPriv *)node->fsPrivate;
    RfsObj o;
    uint8_t *dst = (uint8_t *)buffer;
    size_t done = 0;
    int r;

    (void)file;
    r = LoadObj(p->fs, p->id, &o);
    if (r != 0)
        return r;
    if (o.type != ROBOFS_OBJ_FILE)
        return -(int64_t)RoboEisdir;
    if (offset >= o.size)
        return 0;
    if (count > o.size - offset)
        count = (size_t)(o.size - offset);
    while (done < count) {
        uint32_t lb = (uint32_t)((offset + done) / p->fs->bs);
        uint32_t off = (uint32_t)((offset + done) % p->fs->bs);
        uint32_t chunk = p->fs->bs - off;
        uint32_t blk;
        if (chunk > count - done)
            chunk = (uint32_t)(count - done);
        r = MapBlock(p->fs, &o, lb, 0, &blk);
        if (r < 0)
            return r;
        if (!blk)
            zmem(dst + done, chunk);
        else {
            r = ReadBlk(p->fs, blk, p->fs->blk);
            if (r != 0)
                return r;
            cmem(dst + done, p->fs->blk + off, chunk);
        }
        done += chunk;
    }
    return (int64_t)done;
}

static int64_t RfsWrite(RoboVnode *node, RoboFile *file, const void *buffer,
                         size_t count, uint64_t offset)
{
    RfsPriv *p = (RfsPriv *)node->fsPrivate;
    RfsObj o;
    const uint8_t *src = (const uint8_t *)buffer;
    size_t done = 0;
    int r;
    uint64_t lockf;

    (void)file;
    lockf = RfsLock();
    r = LoadObj(p->fs, p->id, &o);
    if (r != 0) {
        RfsUnlock(lockf);
        return r;
    }
    if (o.type != ROBOFS_OBJ_FILE) {
        RfsUnlock(lockf);
        return -(int64_t)RoboEisdir;
    }
    if (count && offset > ~0ull - (uint64_t)count) {
        RfsUnlock(lockf);
        return -(int64_t)RoboEinval;
    }
    while (done < count) {
        uint32_t lb = (uint32_t)((offset + done) / p->fs->bs);
        uint32_t off = (uint32_t)((offset + done) % p->fs->bs);
        uint32_t chunk = p->fs->bs - off;
        uint32_t blk;
        int dirty;
        if (chunk > count - done)
            chunk = (uint32_t)(count - done);
        dirty = MapBlock(p->fs, &o, lb, 1, &blk);
        if (dirty < 0) {
            RfsUnlock(lockf);
            return dirty;
        }
        r = ReadBlk(p->fs, blk, p->fs->blk);
        if (r != 0) {
            RfsUnlock(lockf);
            return r;
        }
        cmem(p->fs->blk + off, src + done, chunk);
        r = WriteBlk(p->fs, blk, p->fs->blk);
        if (r != 0) {
            RfsUnlock(lockf);
            return r;
        }
        done += chunk;
    }
    if (offset + done > o.size)
        o.size = offset + done;
    o.mtime = now();
    r = StoreObj(p->fs, p->id, &o);
    if (r == 0)
        r = FlushSb(p->fs);
    node->size = o.size;
    RfsUnlock(lockf);
    if (r != 0)
        return r;
    return (int64_t)done;
}

static int RfsCreate(RoboVnode *dir, const char *name, RoboVnodeType type,
                      uint32_t mode, RoboVnode **out)
{
    RfsPriv *p = (RfsPriv *)dir->fsPrivate;
    RfsObj o;
    uint32_t id;
    int r, kind;
    uint64_t lockf;

    kind = (type == RoboVnodeDirectory) ? ROBOFS_OBJ_DIR : ROBOFS_OBJ_FILE;
    if (!name || !name[0] || name[0] == '/' )
        return -RoboEinval;
    if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2])))
        return -RoboEinval;
    {
        unsigned ni;
        for (ni = 0; name[ni]; ni++) {
            if (name[ni] == '/' || ni >= ROBOFS_NAME_MAX)
                return -RoboEinval;
        }
    }
    lockf = RfsLock();
    r = AllocObj(p->fs, &id);
    if (r != 0) {
        RfsUnlock(lockf);
        return r;
    }
    r = LoadObj(p->fs, id, &o);
    if (r != 0) {
        RfsUnlock(lockf);
        return r;
    }
    o.type = (uint32_t)kind;
    o.mode = mode;
    r = StoreObj(p->fs, id, &o);
    if (r != 0) {
        RfsUnlock(lockf);
        return r;
    }
    r = DirInsert(p->fs, p->id, name, id, (uint32_t)kind);
    if (r != 0) {
        o.type = ROBOFS_OBJ_UNUSED;
        StoreObj(p->fs, id, &o);
        p->fs->freeObjects++;
        FlushSb(p->fs);
        RfsUnlock(lockf);
        return r;
    }
    r = FlushSb(p->fs);
    RfsUnlock(lockf);
    if (r != 0)
        return r;
    *out = MakeVnode(p->fs, id, &o, dir->mount);
    return *out ? 0 : -RoboEnomem;
}

static int RfsUnlink(RoboVnode *dir, const char *name)
{
    RfsPriv *p = (RfsPriv *)dir->fsPrivate;
    RfsObj o;
    int id, r;
    uint64_t lockf;

    lockf = RfsLock();
    id = DirRemove(p->fs, p->id, name, ROBOFS_OBJ_FILE);
    if (id < 0) {
        RfsUnlock(lockf);
        return id;
    }
    r = LoadObj(p->fs, (uint32_t)id, &o);
    if (r != 0) {
        RfsUnlock(lockf);
        return r;
    }
    r = FreeFileBlocks(p->fs, &o);
    o.type = ROBOFS_OBJ_UNUSED;
    o.nlink = 0;
    if (r == 0)
        r = StoreObj(p->fs, (uint32_t)id, &o);
    if (r == 0) {
        p->fs->freeObjects++;
        r = FlushSb(p->fs);
    }
    RfsUnlock(lockf);
    return r;
}

static int RfsMkdir(RoboVnode *dir, const char *name, uint32_t mode,
                     RoboVnode **out)
{
    return RfsCreate(dir, name, RoboVnodeDirectory, mode, out);
}

static int RfsRmdir(RoboVnode *dir, const char *name)
{
    RfsPriv *p = (RfsPriv *)dir->fsPrivate;
    RfsObj parent, o;
    uint32_t i, n, eid, et, id, type, slot;
    char en[ROBOFS_NAME_MAX + 1];
    int r;
    uint64_t lockf;

    lockf = RfsLock();
    r = LoadObj(p->fs, p->id, &parent);
    if (r != 0) {
        RfsUnlock(lockf);
        return r;
    }
    r = DirLookup(p->fs, &parent, name, &id, &type, &slot);
    if (r != 0) {
        RfsUnlock(lockf);
        return r;
    }
    if (type != ROBOFS_OBJ_DIR) {
        RfsUnlock(lockf);
        return -RoboEnotdir;
    }
    r = LoadObj(p->fs, id, &o);
    if (r != 0) {
        RfsUnlock(lockf);
        return r;
    }
    n = DirSlots(&o, p->fs->bs);
    for (i = 0; i < n; i++) {
        r = DirGet(p->fs, &o, i, &eid, &et, en);
        if (r != 0) {
            RfsUnlock(lockf);
            return r;
        }
        if (eid) {
            RfsUnlock(lockf);
            return -RoboEnotempty;
        }
    }
    r = DirSet(p->fs, &parent, slot, 0, 0, "");
    if (r == 0) {
        parent.mtime = now();
        r = StoreObj(p->fs, p->id, &parent);
    }
    if (r == 0)
        r = FreeFileBlocks(p->fs, &o);
    o.type = ROBOFS_OBJ_UNUSED;
    o.nlink = 0;
    if (r == 0)
        r = StoreObj(p->fs, id, &o);
    if (r == 0) {
        p->fs->freeObjects++;
        r = FlushSb(p->fs);
    }
    RfsUnlock(lockf);
    return r;
}

static int RfsRename(RoboVnode *oldDir, const char *oldName,
                      RoboVnode *newDir, const char *newName)
{
    RfsPriv *op = (RfsPriv *)oldDir->fsPrivate;
    RfsPriv *np = (RfsPriv *)newDir->fsPrivate;
    RfsObj od;
    uint32_t id, type, slot;
    int r;
    uint64_t lockf;

    if (op->fs != np->fs)
        return -RoboExdev;
    lockf = RfsLock();
    r = LoadObj(op->fs, op->id, &od);
    if (r != 0) {
        RfsUnlock(lockf);
        return r;
    }
    r = DirLookup(op->fs, &od, oldName, &id, &type, &slot);
    if (r != 0) {
        RfsUnlock(lockf);
        return r;
    }
    if (op->id == np->id) {
        uint32_t eid, et;
        if (DirLookup(op->fs, &od, newName, &eid, &et, 0) == 0) {
            RfsUnlock(lockf);
            return -RoboEexist;
        }
        r = DirSet(op->fs, &od, slot, id, type, newName);
        if (r == 0) {
            od.mtime = now();
            r = StoreObj(op->fs, op->id, &od);
        }
        if (r == 0)
            r = FlushSb(op->fs);
        RfsUnlock(lockf);
        return r;
    }
    r = DirInsert(op->fs, np->id, newName, id, type);
    if (r != 0) {
        RfsUnlock(lockf);
        return r;
    }
    r = LoadObj(op->fs, op->id, &od);
    if (r != 0) {
        RfsUnlock(lockf);
        return r;
    }
    r = DirLookup(op->fs, &od, oldName, &id, &type, &slot);
    if (r != 0) {
        RfsUnlock(lockf);
        return r;
    }
    r = DirSet(op->fs, &od, slot, 0, 0, "");
    if (r == 0) {
        od.mtime = now();
        r = StoreObj(op->fs, op->id, &od);
    }
    if (r == 0)
        r = FlushSb(op->fs);
    RfsUnlock(lockf);
    return r;
}

static int RfsStat(RoboVnode *node, RoboStat *st)
{
    RfsPriv *p = (RfsPriv *)node->fsPrivate;
    RfsObj o;
    int r;

    r = LoadObj(p->fs, p->id, &o);
    if (r != 0)
        return r;
    zmem(st, (unsigned)sizeof(*st));
    st->size = o.size;
    st->mode = o.mode;
    st->inode = p->id;
    st->created = o.ctime;
    st->modified = o.mtime;
    st->accessed = o.atime;
    st->nlink = o.nlink;
    st->type = (o.type == ROBOFS_OBJ_DIR) ? RoboVnodeDirectory
                                          : RoboVnodeFile;
    return 0;
}

static const RoboVnodeOps gRfsVops = {
    .lookup = RfsLookup,
    .open = RfsOpen,
    .close = 0,
    .read = RfsRead,
    .write = RfsWrite,
    .create = RfsCreate,
    .unlink = RfsUnlink,
    .mkdir = RfsMkdir,
    .rmdir = RfsRmdir,
    .rename = RfsRename,
    .stat = RfsStat,
    .readdir = 0,
    .release = RfsRelease,
};

static void RfsFree(RoboRfs *fs)
{
    if (!fs)
        return;
    kfree(fs->bitmap);
    kfree(fs->blk);
    kfree(fs);
}

int RobofsFormat(RoboBlockDevice *dev)
{
    RoboRfs tmp;
    uint32_t i, bits, bmb, objs, ob, data, reserved;
    RfsObj root;
    int r;

    if (!dev || dev->blockSize != ROBOFS_BLOCK_SIZE || dev->blockCount < 16)
        return -RoboEinval;
    zmem(&tmp, (unsigned)sizeof(tmp));
    tmp.dev = dev;
    tmp.bs = dev->blockSize;
    tmp.total = (uint32_t)dev->blockCount;
    tmp.bitmapStart = 1;
    bits = (tmp.total + 7) / 8;
    bmb = (bits + tmp.bs - 1) / tmp.bs;
    if (!bmb)
        bmb = 1;
    tmp.bitmapBlocks = bmb;
    tmp.objStart = 1 + bmb;
    objs = 256;
    if (objs < 32)
        objs = 32;
    tmp.objCount = objs;
    ob = (objs * ROBOFS_OBJ_SIZE + tmp.bs - 1) / tmp.bs;
    tmp.objBlocks = ob;
    tmp.dataStart = tmp.objStart + ob;
    if (tmp.dataStart >= tmp.total) {
        objs = 64;
        tmp.objCount = objs;
        ob = (objs * ROBOFS_OBJ_SIZE + tmp.bs - 1) / tmp.bs;
        tmp.objBlocks = ob;
        tmp.dataStart = tmp.objStart + ob;
    }
    if (tmp.dataStart >= tmp.total)
        return -RoboEnospc;
    tmp.rootId = 1;
    reserved = tmp.dataStart;
    tmp.freeBlocks = tmp.total - reserved;
    tmp.freeObjects = objs - 2;
    tmp.blk = (uint8_t *)kmalloc(tmp.bs);
    tmp.bitmap = (uint8_t *)kmalloc(bmb * tmp.bs);
    if (!tmp.blk || !tmp.bitmap) {
        kfree(tmp.blk);
        kfree(tmp.bitmap);
        return -RoboEnomem;
    }
    zmem(tmp.bitmap, bmb * tmp.bs);
    zmem(tmp.blk, tmp.bs);
    for (i = 0; i < reserved; i++)
        BitSet(&tmp, i, 1);
    r = FlushBitmap(&tmp);
    if (r == 0) {
        for (i = tmp.objStart; i < tmp.dataStart; i++) {
            zmem(tmp.blk, tmp.bs);
            r = WriteBlk(&tmp, i, tmp.blk);
            if (r != 0)
                break;
        }
    }
    if (r == 0) {
        zmem(&root, (unsigned)sizeof(root));
        root.type = ROBOFS_OBJ_DIR;
        root.nlink = 1;
        root.ctime = root.mtime = root.atime = now();
        r = StoreObj(&tmp, 1, &root);
    }
    if (r == 0)
        r = FlushSb(&tmp);
    kfree(tmp.bitmap);
    kfree(tmp.blk);
    return r;
}

int RobofsMount(RoboBlockDevice *dev, uint32_t flags, RoboMount **out)
{
    RoboRfs *fs;
    uint8_t *sb;
    uint32_t magic, ver, i;
    RfsObj root;
    RoboVnode *vn;
    RoboMount *m;
    int r;

    (void)flags;
    if (!dev || !out || dev->blockSize != ROBOFS_BLOCK_SIZE)
        return -RoboEinval;
    *out = 0;
    sb = (uint8_t *)kmalloc(dev->blockSize);
    if (!sb)
        return -RoboEnomem;
    r = BdevRead(dev, 0, sb);
    if (r != 0) {
        kfree(sb);
        return r;
    }
    magic = rd32(sb);
    ver = rd32(sb + 4);
    if (magic != ROBOFS_DISK_MAGIC || (ver & 0xFFFFu) != ROBOFS_DISK_MAJOR) {
        kfree(sb);
        return -RoboEinval;
    }
    if (rd32(sb + 68) != SbCsum(sb)) {
        kfree(sb);
        return -RoboEio;
    }
    if (rd32(sb + 8) != dev->blockSize) {
        kfree(sb);
        return -RoboEinval;
    }
    fs = (RoboRfs *)kmalloc(sizeof(*fs));
    if (!fs) {
        kfree(sb);
        return -RoboEnomem;
    }
    zmem(fs, (unsigned)sizeof(*fs));
    fs->dev = dev;
    fs->bs = rd32(sb + 8);
    fs->total = rd32(sb + 12);
    fs->freeBlocks = rd32(sb + 16);
    fs->objCount = rd32(sb + 20);
    fs->freeObjects = rd32(sb + 24);
    fs->bitmapStart = rd32(sb + 28);
    fs->bitmapBlocks = rd32(sb + 32);
    fs->objStart = rd32(sb + 36);
    fs->objBlocks = rd32(sb + 40);
    fs->dataStart = rd32(sb + 44);
    fs->rootId = rd32(sb + 48);
    kfree(sb);
    if (!fs->total || fs->total > dev->blockCount ||
        fs->dataStart >= fs->total || !fs->rootId ||
        fs->rootId >= fs->objCount) {
        kfree(fs);
        return -RoboEio;
    }
    fs->blk = (uint8_t *)kmalloc(fs->bs);
    fs->bitmap = (uint8_t *)kmalloc(fs->bitmapBlocks * fs->bs);
    if (!fs->blk || !fs->bitmap) {
        RfsFree(fs);
        return -RoboEnomem;
    }
    for (i = 0; i < fs->bitmapBlocks; i++) {
        r = ReadBlk(fs, fs->bitmapStart + i, fs->bitmap + i * fs->bs);
        if (r != 0) {
            RfsFree(fs);
            return r;
        }
    }
    r = LoadObj(fs, fs->rootId, &root);
    if (r != 0 || root.type != ROBOFS_OBJ_DIR) {
        RfsFree(fs);
        return -RoboEio;
    }
    vn = MakeVnode(fs, fs->rootId, &root, 0);
    if (!vn) {
        RfsFree(fs);
        return -RoboEnomem;
    }
    m = MountCreate(&gRfsType, vn, fs);
    VnodePut(vn);
    if (!m) {
        RfsFree(fs);
        return -RoboEnomem;
    }
    *out = m;
    return 0;
}

int RobofsUnmount(RoboMount *mount)
{
    RoboRfs *fs;

    if (!mount)
        return -RoboEinval;
    fs = (RoboRfs *)mount->fsInstance;
    mount->fsInstance = 0;
    MountPut(mount);
    RfsFree(fs);
    return 0;
}

static int RfsFsMount(RoboBlockDevice *dev, uint32_t flags, RoboMount **out)
{
    return RobofsMount(dev, flags, out);
}

static int RfsFsUnmount(RoboMount *m)
{
    return RobofsUnmount(m);
}

static const RoboFilesystemOps gRfsFsops = {
    .mount = RfsFsMount,
    .unmount = RfsFsUnmount,
};

static RoboFilesystem gRfsType = {
    .name = "robofs",
    .id = 0,
    .ops = &gRfsFsops,
};

int RobofsRegister(void)
{
    return VfsRegister(&gRfsType);
}
