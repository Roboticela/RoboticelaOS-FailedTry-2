#include "fs/rfs/rfs.h"
#include "fs/rfs/rfs_disk.h"
#include "syscall/syscall_errors.h"

#if defined(PROCESS_HOST_TEST) || defined(RFS_HOST_TEST)
#include <stdlib.h>
#define kmalloc malloc
#define kfree free
static uint64_t now(void) { return 0; }
static uint64_t rfs_lock(void) { return 0; }
static void rfs_unlock(uint64_t f) { (void)f; }
#else
#include "mm/heap/heap.h"
#include "time/time.h"
static uint64_t now(void) { return robo_time_now(); }
static uint64_t rfs_lock(void)
{
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
/* rfs_lock: bitmap, object table, directory and file metadata.
 * Do not nest with vfs_lock. */
static void rfs_unlock(uint64_t flags)
{
    __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
}
#endif

typedef struct RoboRfs {
    RoboBlockDevice *dev;
    uint32_t bs;
    uint32_t total;
    uint32_t bitmap_start;
    uint32_t bitmap_blocks;
    uint32_t obj_start;
    uint32_t obj_blocks;
    uint32_t obj_count;
    uint32_t data_start;
    uint32_t root_id;
    uint32_t free_blocks;
    uint32_t free_objects;
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

static RoboVnode *make_vnode(RoboRfs *fs, uint32_t id, const RfsObj *o,
                             RoboMount *mnt);
static const RoboVnodeOps g_rfs_vops;
static RoboFilesystem g_rfs_type;

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

static uint32_t sb_csum(const uint8_t *sb)
{
    uint32_t s = 0, i;
    for (i = 0; i < 68; i += 4)
        s ^= rd32(sb + i);
    return s;
}

static int mul_ok(uint32_t a, uint32_t b, uint32_t *out)
{
    if (b && a > 0xFFFFFFFFu / b)
        return 0;
    *out = a * b;
    return 1;
}

static int read_blk(RoboRfs *fs, uint32_t lba, void *buf)
{
    return robo_bdev_read(fs->dev, lba, buf);
}

static int write_blk(RoboRfs *fs, uint32_t lba, const void *buf)
{
    return robo_bdev_write(fs->dev, lba, buf);
}

static int flush_bitmap(RoboRfs *fs)
{
    uint32_t i;
    int r;
    for (i = 0; i < fs->bitmap_blocks; i++) {
        r = write_blk(fs, fs->bitmap_start + i, fs->bitmap + i * fs->bs);
        if (r != 0)
            return r;
    }
    return 0;
}

static int flush_sb(RoboRfs *fs)
{
    zmem(fs->blk, fs->bs);
    wr32(fs->blk + 0, ROBOFS_DISK_MAGIC);
    wr32(fs->blk + 4, (ROBOFS_DISK_MINOR << 16) | ROBOFS_DISK_MAJOR);
    wr32(fs->blk + 8, fs->bs);
    wr32(fs->blk + 12, fs->total);
    wr32(fs->blk + 16, fs->free_blocks);
    wr32(fs->blk + 20, fs->obj_count);
    wr32(fs->blk + 24, fs->free_objects);
    wr32(fs->blk + 28, fs->bitmap_start);
    wr32(fs->blk + 32, fs->bitmap_blocks);
    wr32(fs->blk + 36, fs->obj_start);
    wr32(fs->blk + 40, fs->obj_blocks);
    wr32(fs->blk + 44, fs->data_start);
    wr32(fs->blk + 48, fs->root_id);
    wr32(fs->blk + 52, 0);
    wr32(fs->blk + 68, sb_csum(fs->blk));
    return write_blk(fs, 0, fs->blk);
}

static int bit_get(RoboRfs *fs, uint32_t blk)
{
    return (fs->bitmap[blk / 8] >> (blk % 8)) & 1;
}

static void bit_set(RoboRfs *fs, uint32_t blk, int on)
{
    uint32_t byte = blk / 8;
    uint32_t bit = blk % 8;
    if (on)
        fs->bitmap[byte] = (uint8_t)(fs->bitmap[byte] | (1u << bit));
    else
        fs->bitmap[byte] = (uint8_t)(fs->bitmap[byte] & ~(1u << bit));
}

static int alloc_block(RoboRfs *fs, uint32_t *out)
{
    uint32_t i;
    for (i = fs->data_start; i < fs->total; i++) {
        if (!bit_get(fs, i)) {
            bit_set(fs, i, 1);
            fs->free_blocks--;
            *out = i;
            return flush_bitmap(fs);
        }
    }
    return -ROBO_ENOSPC;
}

static int free_block(RoboRfs *fs, uint32_t blk)
{
    if (blk < fs->data_start || blk >= fs->total)
        return -ROBO_EIO;
    if (!bit_get(fs, blk))
        return -ROBO_EIO;
    bit_set(fs, blk, 0);
    fs->free_blocks++;
    return flush_bitmap(fs);
}

static int load_obj(RoboRfs *fs, uint32_t id, RfsObj *o)
{
    uint32_t byte, lba, off, i;
    int r;

    if (!id || id >= fs->obj_count)
        return -ROBO_EINVAL;
    if (!mul_ok(id, ROBOFS_OBJ_SIZE, &byte))
        return -ROBO_EIO;
    lba = fs->obj_start + byte / fs->bs;
    off = byte % fs->bs;
    r = read_blk(fs, lba, fs->blk);
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

static int store_obj(RoboRfs *fs, uint32_t id, const RfsObj *o)
{
    uint32_t byte, lba, off, i;
    int r;

    if (!id || id >= fs->obj_count)
        return -ROBO_EINVAL;
    if (!mul_ok(id, ROBOFS_OBJ_SIZE, &byte))
        return -ROBO_EIO;
    lba = fs->obj_start + byte / fs->bs;
    off = byte % fs->bs;
    r = read_blk(fs, lba, fs->blk);
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
    return write_blk(fs, lba, fs->blk);
}

static int alloc_obj(RoboRfs *fs, uint32_t *out)
{
    uint32_t id;
    RfsObj o;
    int r;

    for (id = 1; id < fs->obj_count; id++) {
        r = load_obj(fs, id, &o);
        if (r != 0)
            return r;
        if (o.type == ROBOFS_OBJ_UNUSED) {
            zmem(&o, (unsigned)sizeof(o));
            o.nlink = 1;
            o.ctime = o.mtime = o.atime = now();
            *out = id;
            fs->free_objects--;
            r = store_obj(fs, id, &o);
            if (r != 0)
                return r;
            return flush_sb(fs);
        }
    }
    return -ROBO_ENOSPC;
}

static int free_file_blocks(RoboRfs *fs, RfsObj *o)
{
    uint32_t i, nptr, p;
    int r;

    for (i = 0; i < ROBOFS_DIRECT; i++) {
        if (o->direct[i]) {
            r = free_block(fs, o->direct[i]);
            if (r != 0)
                return r;
            o->direct[i] = 0;
        }
    }
    if (o->indirect) {
        r = read_blk(fs, o->indirect, fs->blk);
        if (r != 0)
            return r;
        nptr = fs->bs / 4;
        for (i = 0; i < nptr; i++) {
            p = rd32(fs->blk + i * 4);
            if (p) {
                r = free_block(fs, p);
                if (r != 0)
                    return r;
            }
        }
        r = free_block(fs, o->indirect);
        if (r != 0)
            return r;
        o->indirect = 0;
    }
    o->size = 0;
    return 0;
}

static int map_block(RoboRfs *fs, RfsObj *o, uint32_t lb, int alloc, uint32_t *out)
{
    uint32_t blk = 0, nptr, idx;
    int r, dirty = 0;

    if (lb < ROBOFS_DIRECT) {
        blk = o->direct[lb];
        if (!blk && alloc) {
            r = alloc_block(fs, &blk);
            if (r != 0)
                return r;
            zmem(fs->blk, fs->bs);
            r = write_blk(fs, blk, fs->blk);
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
        return -ROBO_EIO;
    if (!o->indirect) {
        if (!alloc) {
            *out = 0;
            return 0;
        }
        r = alloc_block(fs, &o->indirect);
        if (r != 0)
            return r;
        zmem(fs->blk, fs->bs);
        r = write_blk(fs, o->indirect, fs->blk);
        if (r != 0)
            return r;
        dirty = 1;
    }
    r = read_blk(fs, o->indirect, fs->blk);
    if (r != 0)
        return r;
    blk = rd32(fs->blk + idx * 4);
    if (!blk && alloc) {
        r = alloc_block(fs, &blk);
        if (r != 0)
            return r;
        {
            uint8_t *zero = (uint8_t *)kmalloc(fs->bs);
            if (!zero)
                return -ROBO_ENOMEM;
            zmem(zero, fs->bs);
            r = write_blk(fs, blk, zero);
            kfree(zero);
            if (r != 0)
                return r;
        }
        wr32(fs->blk + idx * 4, blk);
        r = write_blk(fs, o->indirect, fs->blk);
        if (r != 0)
            return r;
        dirty = 1;
    }
    *out = blk;
    return dirty ? 1 : 0;
}

static int name_eq(const char *a, const char *b)
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

static uint32_t dir_slots(const RfsObj *o, uint32_t bs)
{
    return (uint32_t)(o->size / ROBOFS_DIRENT_SIZE);
}

static int dir_get(RoboRfs *fs, RfsObj *dir, uint32_t slot,
                   uint32_t *id, uint32_t *type, char *name)
{
    uint32_t lb, off, blk, i;
    uint8_t *buf;
    int r;

    lb = (slot * ROBOFS_DIRENT_SIZE) / fs->bs;
    off = (slot * ROBOFS_DIRENT_SIZE) % fs->bs;
    r = map_block(fs, dir, lb, 0, &blk);
    if (r < 0)
        return r;
    if (!blk)
        return -ROBO_ENOENT;
    buf = fs->blk;
    r = read_blk(fs, blk, buf);
    if (r != 0)
        return r;
    *id = rd32(buf + off);
    *type = rd32(buf + off + 4);
    for (i = 0; i < ROBOFS_NAME_MAX; i++)
        name[i] = (char)buf[off + 8 + i];
    name[ROBOFS_NAME_MAX] = 0;
    return 0;
}

static int dir_set(RoboRfs *fs, RfsObj *dir, uint32_t slot,
                   uint32_t id, uint32_t type, const char *name)
{
    uint32_t lb, off, blk, i;
    int r, dirty;

    lb = (slot * ROBOFS_DIRENT_SIZE) / fs->bs;
    off = (slot * ROBOFS_DIRENT_SIZE) % fs->bs;
    dirty = map_block(fs, dir, lb, 1, &blk);
    if (dirty < 0)
        return dirty;
    r = read_blk(fs, blk, fs->blk);
    if (r != 0)
        return r;
    wr32(fs->blk + off, id);
    wr32(fs->blk + off + 4, type);
    for (i = 0; i < ROBOFS_NAME_MAX; i++)
        fs->blk[off + 8 + i] = name && name[i] ? (uint8_t)name[i] : 0;
    return write_blk(fs, blk, fs->blk);
}

static int dir_lookup(RoboRfs *fs, RfsObj *dir, const char *name,
                      uint32_t *id, uint32_t *type, uint32_t *slot)
{
    uint32_t i, n, eid, et;
    char en[ROBOFS_NAME_MAX + 1];
    int r;

    n = dir_slots(dir, fs->bs);
    for (i = 0; i < n; i++) {
        r = dir_get(fs, dir, i, &eid, &et, en);
        if (r != 0)
            return r;
        if (!eid)
            continue;
        if (name_eq(en, name)) {
            *id = eid;
            *type = et;
            if (slot)
                *slot = i;
            return 0;
        }
    }
    return -ROBO_ENOENT;
}

static int dir_insert(RoboRfs *fs, uint32_t dir_id, const char *name,
                      uint32_t child, uint32_t type)
{
    RfsObj dir;
    uint32_t i, n, eid, et, slot;
    char en[ROBOFS_NAME_MAX + 1];
    int r;

    r = load_obj(fs, dir_id, &dir);
    if (r != 0)
        return r;
    if (dir.type != ROBOFS_OBJ_DIR)
        return -ROBO_ENOTDIR;
    if (dir_lookup(fs, &dir, name, &eid, &et, &slot) == 0)
        return -ROBO_EEXIST;
    n = dir_slots(&dir, fs->bs);
    for (i = 0; i < n; i++) {
        r = dir_get(fs, &dir, i, &eid, &et, en);
        if (r != 0)
            return r;
        if (!eid) {
            r = dir_set(fs, &dir, i, child, type, name);
            if (r != 0)
                return r;
            dir.mtime = now();
            return store_obj(fs, dir_id, &dir);
        }
    }
    r = dir_set(fs, &dir, n, child, type, name);
    if (r != 0)
        return r;
    dir.size += ROBOFS_DIRENT_SIZE;
    dir.mtime = now();
    return store_obj(fs, dir_id, &dir);
}

static int dir_remove(RoboRfs *fs, uint32_t dir_id, const char *name,
                      uint32_t expect_type)
{
    RfsObj dir;
    uint32_t id, type, slot;
    int r;

    r = load_obj(fs, dir_id, &dir);
    if (r != 0)
        return r;
    if (dir.type != ROBOFS_OBJ_DIR)
        return -ROBO_ENOTDIR;
    r = dir_lookup(fs, &dir, name, &id, &type, &slot);
    if (r != 0)
        return r;
    if (type != expect_type)
        return expect_type == ROBOFS_OBJ_DIR ? -ROBO_ENOTDIR : -ROBO_EISDIR;
    r = dir_set(fs, &dir, slot, 0, 0, "");
    if (r != 0)
        return r;
    dir.mtime = now();
    r = store_obj(fs, dir_id, &dir);
    if (r != 0)
        return r;
    return (int)id;
}

static RoboVnode *make_vnode(RoboRfs *fs, uint32_t id, const RfsObj *o,
                             RoboMount *mnt)
{
    RfsPriv *p;
    RoboVnode *n;

    p = (RfsPriv *)kmalloc(sizeof(*p));
    if (!p)
        return 0;
    p->fs = fs;
    p->id = id;
    n = robo_vnode_create(o->type == ROBOFS_OBJ_DIR ? ROBO_VNODE_DIRECTORY
                                                    : ROBO_VNODE_FILE,
                          &g_rfs_vops, p);
    if (!n) {
        kfree(p);
        return 0;
    }
    n->id = id;
    n->size = o->size;
    n->mount = mnt;
    return n;
}

static void rfs_release(RoboVnode *node)
{
    if (node && node->fs_private) {
        kfree(node->fs_private);
        node->fs_private = 0;
    }
}

static int rfs_lookup(RoboVnode *dir, const char *name, RoboVnode **out)
{
    RfsPriv *p = (RfsPriv *)dir->fs_private;
    RfsObj o;
    uint32_t id, type;
    int r;

    r = load_obj(p->fs, p->id, &o);
    if (r != 0)
        return r;
    r = dir_lookup(p->fs, &o, name, &id, &type, 0);
    if (r != 0)
        return r;
    r = load_obj(p->fs, id, &o);
    if (r != 0)
        return r;
    *out = make_vnode(p->fs, id, &o, dir->mount);
    return *out ? 0 : -ROBO_ENOMEM;
}

static int rfs_open(RoboVnode *node, RoboFile *file)
{
    RfsPriv *p = (RfsPriv *)node->fs_private;
    RfsObj o;
    int r;

    r = load_obj(p->fs, p->id, &o);
    if (r != 0)
        return r;
    if ((file->flags & ROBO_FFLAG_TRUNC) && (file->flags & ROBO_FFLAG_WRITE) &&
        o.type == ROBOFS_OBJ_FILE) {
        r = free_file_blocks(p->fs, &o);
        if (r != 0)
            return r;
        o.mtime = now();
        r = store_obj(p->fs, p->id, &o);
        if (r != 0)
            return r;
        r = flush_sb(p->fs);
        node->size = 0;
    }
    return r;
}

static int64_t rfs_read(RoboVnode *node, RoboFile *file, void *buffer,
                        size_t count, uint64_t offset)
{
    RfsPriv *p = (RfsPriv *)node->fs_private;
    RfsObj o;
    uint8_t *dst = (uint8_t *)buffer;
    size_t done = 0;
    int r;

    (void)file;
    r = load_obj(p->fs, p->id, &o);
    if (r != 0)
        return r;
    if (o.type != ROBOFS_OBJ_FILE)
        return -(int64_t)ROBO_EISDIR;
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
        r = map_block(p->fs, &o, lb, 0, &blk);
        if (r < 0)
            return r;
        if (!blk)
            zmem(dst + done, chunk);
        else {
            r = read_blk(p->fs, blk, p->fs->blk);
            if (r != 0)
                return r;
            cmem(dst + done, p->fs->blk + off, chunk);
        }
        done += chunk;
    }
    return (int64_t)done;
}

static int64_t rfs_write(RoboVnode *node, RoboFile *file, const void *buffer,
                         size_t count, uint64_t offset)
{
    RfsPriv *p = (RfsPriv *)node->fs_private;
    RfsObj o;
    const uint8_t *src = (const uint8_t *)buffer;
    size_t done = 0;
    int r;
    uint64_t lockf;

    (void)file;
    lockf = rfs_lock();
    r = load_obj(p->fs, p->id, &o);
    if (r != 0) {
        rfs_unlock(lockf);
        return r;
    }
    if (o.type != ROBOFS_OBJ_FILE) {
        rfs_unlock(lockf);
        return -(int64_t)ROBO_EISDIR;
    }
    if (count && offset > ~0ull - (uint64_t)count) {
        rfs_unlock(lockf);
        return -(int64_t)ROBO_EINVAL;
    }
    while (done < count) {
        uint32_t lb = (uint32_t)((offset + done) / p->fs->bs);
        uint32_t off = (uint32_t)((offset + done) % p->fs->bs);
        uint32_t chunk = p->fs->bs - off;
        uint32_t blk;
        int dirty;
        if (chunk > count - done)
            chunk = (uint32_t)(count - done);
        dirty = map_block(p->fs, &o, lb, 1, &blk);
        if (dirty < 0) {
            rfs_unlock(lockf);
            return dirty;
        }
        r = read_blk(p->fs, blk, p->fs->blk);
        if (r != 0) {
            rfs_unlock(lockf);
            return r;
        }
        cmem(p->fs->blk + off, src + done, chunk);
        r = write_blk(p->fs, blk, p->fs->blk);
        if (r != 0) {
            rfs_unlock(lockf);
            return r;
        }
        done += chunk;
    }
    if (offset + done > o.size)
        o.size = offset + done;
    o.mtime = now();
    r = store_obj(p->fs, p->id, &o);
    if (r == 0)
        r = flush_sb(p->fs);
    node->size = o.size;
    rfs_unlock(lockf);
    if (r != 0)
        return r;
    return (int64_t)done;
}

static int rfs_create(RoboVnode *dir, const char *name, RoboVnodeType type,
                      uint32_t mode, RoboVnode **out)
{
    RfsPriv *p = (RfsPriv *)dir->fs_private;
    RfsObj o;
    uint32_t id;
    int r, kind;
    uint64_t lockf;

    kind = (type == ROBO_VNODE_DIRECTORY) ? ROBOFS_OBJ_DIR : ROBOFS_OBJ_FILE;
    if (!name || !name[0] || name[0] == '/' )
        return -ROBO_EINVAL;
    if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2])))
        return -ROBO_EINVAL;
    {
        unsigned ni;
        for (ni = 0; name[ni]; ni++) {
            if (name[ni] == '/' || ni >= ROBOFS_NAME_MAX)
                return -ROBO_EINVAL;
        }
    }
    lockf = rfs_lock();
    r = alloc_obj(p->fs, &id);
    if (r != 0) {
        rfs_unlock(lockf);
        return r;
    }
    r = load_obj(p->fs, id, &o);
    if (r != 0) {
        rfs_unlock(lockf);
        return r;
    }
    o.type = (uint32_t)kind;
    o.mode = mode;
    r = store_obj(p->fs, id, &o);
    if (r != 0) {
        rfs_unlock(lockf);
        return r;
    }
    r = dir_insert(p->fs, p->id, name, id, (uint32_t)kind);
    if (r != 0) {
        o.type = ROBOFS_OBJ_UNUSED;
        store_obj(p->fs, id, &o);
        p->fs->free_objects++;
        flush_sb(p->fs);
        rfs_unlock(lockf);
        return r;
    }
    r = flush_sb(p->fs);
    rfs_unlock(lockf);
    if (r != 0)
        return r;
    *out = make_vnode(p->fs, id, &o, dir->mount);
    return *out ? 0 : -ROBO_ENOMEM;
}

static int rfs_unlink(RoboVnode *dir, const char *name)
{
    RfsPriv *p = (RfsPriv *)dir->fs_private;
    RfsObj o;
    int id, r;
    uint64_t lockf;

    lockf = rfs_lock();
    id = dir_remove(p->fs, p->id, name, ROBOFS_OBJ_FILE);
    if (id < 0) {
        rfs_unlock(lockf);
        return id;
    }
    r = load_obj(p->fs, (uint32_t)id, &o);
    if (r != 0) {
        rfs_unlock(lockf);
        return r;
    }
    r = free_file_blocks(p->fs, &o);
    o.type = ROBOFS_OBJ_UNUSED;
    o.nlink = 0;
    if (r == 0)
        r = store_obj(p->fs, (uint32_t)id, &o);
    if (r == 0) {
        p->fs->free_objects++;
        r = flush_sb(p->fs);
    }
    rfs_unlock(lockf);
    return r;
}

static int rfs_mkdir(RoboVnode *dir, const char *name, uint32_t mode,
                     RoboVnode **out)
{
    return rfs_create(dir, name, ROBO_VNODE_DIRECTORY, mode, out);
}

static int rfs_rmdir(RoboVnode *dir, const char *name)
{
    RfsPriv *p = (RfsPriv *)dir->fs_private;
    RfsObj parent, o;
    uint32_t i, n, eid, et, id, type, slot;
    char en[ROBOFS_NAME_MAX + 1];
    int r;
    uint64_t lockf;

    lockf = rfs_lock();
    r = load_obj(p->fs, p->id, &parent);
    if (r != 0) {
        rfs_unlock(lockf);
        return r;
    }
    r = dir_lookup(p->fs, &parent, name, &id, &type, &slot);
    if (r != 0) {
        rfs_unlock(lockf);
        return r;
    }
    if (type != ROBOFS_OBJ_DIR) {
        rfs_unlock(lockf);
        return -ROBO_ENOTDIR;
    }
    r = load_obj(p->fs, id, &o);
    if (r != 0) {
        rfs_unlock(lockf);
        return r;
    }
    n = dir_slots(&o, p->fs->bs);
    for (i = 0; i < n; i++) {
        r = dir_get(p->fs, &o, i, &eid, &et, en);
        if (r != 0) {
            rfs_unlock(lockf);
            return r;
        }
        if (eid) {
            rfs_unlock(lockf);
            return -ROBO_ENOTEMPTY;
        }
    }
    r = dir_set(p->fs, &parent, slot, 0, 0, "");
    if (r == 0) {
        parent.mtime = now();
        r = store_obj(p->fs, p->id, &parent);
    }
    if (r == 0)
        r = free_file_blocks(p->fs, &o);
    o.type = ROBOFS_OBJ_UNUSED;
    o.nlink = 0;
    if (r == 0)
        r = store_obj(p->fs, id, &o);
    if (r == 0) {
        p->fs->free_objects++;
        r = flush_sb(p->fs);
    }
    rfs_unlock(lockf);
    return r;
}

static int rfs_rename(RoboVnode *old_dir, const char *old_name,
                      RoboVnode *new_dir, const char *new_name)
{
    RfsPriv *op = (RfsPriv *)old_dir->fs_private;
    RfsPriv *np = (RfsPriv *)new_dir->fs_private;
    RfsObj od;
    uint32_t id, type, slot;
    int r;
    uint64_t lockf;

    if (op->fs != np->fs)
        return -ROBO_EXDEV;
    lockf = rfs_lock();
    r = load_obj(op->fs, op->id, &od);
    if (r != 0) {
        rfs_unlock(lockf);
        return r;
    }
    r = dir_lookup(op->fs, &od, old_name, &id, &type, &slot);
    if (r != 0) {
        rfs_unlock(lockf);
        return r;
    }
    if (op->id == np->id) {
        uint32_t eid, et;
        if (dir_lookup(op->fs, &od, new_name, &eid, &et, 0) == 0) {
            rfs_unlock(lockf);
            return -ROBO_EEXIST;
        }
        r = dir_set(op->fs, &od, slot, id, type, new_name);
        if (r == 0) {
            od.mtime = now();
            r = store_obj(op->fs, op->id, &od);
        }
        if (r == 0)
            r = flush_sb(op->fs);
        rfs_unlock(lockf);
        return r;
    }
    r = dir_insert(op->fs, np->id, new_name, id, type);
    if (r != 0) {
        rfs_unlock(lockf);
        return r;
    }
    r = load_obj(op->fs, op->id, &od);
    if (r != 0) {
        rfs_unlock(lockf);
        return r;
    }
    r = dir_lookup(op->fs, &od, old_name, &id, &type, &slot);
    if (r != 0) {
        rfs_unlock(lockf);
        return r;
    }
    r = dir_set(op->fs, &od, slot, 0, 0, "");
    if (r == 0) {
        od.mtime = now();
        r = store_obj(op->fs, op->id, &od);
    }
    if (r == 0)
        r = flush_sb(op->fs);
    rfs_unlock(lockf);
    return r;
}

static int rfs_stat(RoboVnode *node, RoboStat *st)
{
    RfsPriv *p = (RfsPriv *)node->fs_private;
    RfsObj o;
    int r;

    r = load_obj(p->fs, p->id, &o);
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
    st->type = (o.type == ROBOFS_OBJ_DIR) ? ROBO_VNODE_DIRECTORY
                                          : ROBO_VNODE_FILE;
    return 0;
}

static const RoboVnodeOps g_rfs_vops = {
    .lookup = rfs_lookup,
    .open = rfs_open,
    .close = 0,
    .read = rfs_read,
    .write = rfs_write,
    .create = rfs_create,
    .unlink = rfs_unlink,
    .mkdir = rfs_mkdir,
    .rmdir = rfs_rmdir,
    .rename = rfs_rename,
    .stat = rfs_stat,
    .readdir = 0,
    .release = rfs_release,
};

static void rfs_free(RoboRfs *fs)
{
    if (!fs)
        return;
    kfree(fs->bitmap);
    kfree(fs->blk);
    kfree(fs);
}

int robofs_format(RoboBlockDevice *dev)
{
    RoboRfs tmp;
    uint32_t i, bits, bmb, objs, ob, data, reserved;
    RfsObj root;
    int r;

    if (!dev || dev->block_size != ROBOFS_BLOCK_SIZE || dev->block_count < 16)
        return -ROBO_EINVAL;
    zmem(&tmp, (unsigned)sizeof(tmp));
    tmp.dev = dev;
    tmp.bs = dev->block_size;
    tmp.total = (uint32_t)dev->block_count;
    tmp.bitmap_start = 1;
    bits = (tmp.total + 7) / 8;
    bmb = (bits + tmp.bs - 1) / tmp.bs;
    if (!bmb)
        bmb = 1;
    tmp.bitmap_blocks = bmb;
    tmp.obj_start = 1 + bmb;
    objs = 256;
    if (objs < 32)
        objs = 32;
    tmp.obj_count = objs;
    ob = (objs * ROBOFS_OBJ_SIZE + tmp.bs - 1) / tmp.bs;
    tmp.obj_blocks = ob;
    tmp.data_start = tmp.obj_start + ob;
    if (tmp.data_start >= tmp.total) {
        objs = 64;
        tmp.obj_count = objs;
        ob = (objs * ROBOFS_OBJ_SIZE + tmp.bs - 1) / tmp.bs;
        tmp.obj_blocks = ob;
        tmp.data_start = tmp.obj_start + ob;
    }
    if (tmp.data_start >= tmp.total)
        return -ROBO_ENOSPC;
    tmp.root_id = 1;
    reserved = tmp.data_start;
    tmp.free_blocks = tmp.total - reserved;
    tmp.free_objects = objs - 2;
    tmp.blk = (uint8_t *)kmalloc(tmp.bs);
    tmp.bitmap = (uint8_t *)kmalloc(bmb * tmp.bs);
    if (!tmp.blk || !tmp.bitmap) {
        kfree(tmp.blk);
        kfree(tmp.bitmap);
        return -ROBO_ENOMEM;
    }
    zmem(tmp.bitmap, bmb * tmp.bs);
    zmem(tmp.blk, tmp.bs);
    for (i = 0; i < reserved; i++)
        bit_set(&tmp, i, 1);
    r = flush_bitmap(&tmp);
    if (r == 0) {
        for (i = tmp.obj_start; i < tmp.data_start; i++) {
            zmem(tmp.blk, tmp.bs);
            r = write_blk(&tmp, i, tmp.blk);
            if (r != 0)
                break;
        }
    }
    if (r == 0) {
        zmem(&root, (unsigned)sizeof(root));
        root.type = ROBOFS_OBJ_DIR;
        root.nlink = 1;
        root.ctime = root.mtime = root.atime = now();
        r = store_obj(&tmp, 1, &root);
    }
    if (r == 0)
        r = flush_sb(&tmp);
    kfree(tmp.bitmap);
    kfree(tmp.blk);
    return r;
}

int robofs_mount(RoboBlockDevice *dev, uint32_t flags, RoboMount **out)
{
    RoboRfs *fs;
    uint8_t *sb;
    uint32_t magic, ver, i;
    RfsObj root;
    RoboVnode *vn;
    RoboMount *m;
    int r;

    (void)flags;
    if (!dev || !out || dev->block_size != ROBOFS_BLOCK_SIZE)
        return -ROBO_EINVAL;
    *out = 0;
    sb = (uint8_t *)kmalloc(dev->block_size);
    if (!sb)
        return -ROBO_ENOMEM;
    r = robo_bdev_read(dev, 0, sb);
    if (r != 0) {
        kfree(sb);
        return r;
    }
    magic = rd32(sb);
    ver = rd32(sb + 4);
    if (magic != ROBOFS_DISK_MAGIC || (ver & 0xFFFFu) != ROBOFS_DISK_MAJOR) {
        kfree(sb);
        return -ROBO_EINVAL;
    }
    if (rd32(sb + 68) != sb_csum(sb)) {
        kfree(sb);
        return -ROBO_EIO;
    }
    if (rd32(sb + 8) != dev->block_size) {
        kfree(sb);
        return -ROBO_EINVAL;
    }
    fs = (RoboRfs *)kmalloc(sizeof(*fs));
    if (!fs) {
        kfree(sb);
        return -ROBO_ENOMEM;
    }
    zmem(fs, (unsigned)sizeof(*fs));
    fs->dev = dev;
    fs->bs = rd32(sb + 8);
    fs->total = rd32(sb + 12);
    fs->free_blocks = rd32(sb + 16);
    fs->obj_count = rd32(sb + 20);
    fs->free_objects = rd32(sb + 24);
    fs->bitmap_start = rd32(sb + 28);
    fs->bitmap_blocks = rd32(sb + 32);
    fs->obj_start = rd32(sb + 36);
    fs->obj_blocks = rd32(sb + 40);
    fs->data_start = rd32(sb + 44);
    fs->root_id = rd32(sb + 48);
    kfree(sb);
    if (!fs->total || fs->total > dev->block_count ||
        fs->data_start >= fs->total || !fs->root_id ||
        fs->root_id >= fs->obj_count) {
        kfree(fs);
        return -ROBO_EIO;
    }
    fs->blk = (uint8_t *)kmalloc(fs->bs);
    fs->bitmap = (uint8_t *)kmalloc(fs->bitmap_blocks * fs->bs);
    if (!fs->blk || !fs->bitmap) {
        rfs_free(fs);
        return -ROBO_ENOMEM;
    }
    for (i = 0; i < fs->bitmap_blocks; i++) {
        r = read_blk(fs, fs->bitmap_start + i, fs->bitmap + i * fs->bs);
        if (r != 0) {
            rfs_free(fs);
            return r;
        }
    }
    r = load_obj(fs, fs->root_id, &root);
    if (r != 0 || root.type != ROBOFS_OBJ_DIR) {
        rfs_free(fs);
        return -ROBO_EIO;
    }
    vn = make_vnode(fs, fs->root_id, &root, 0);
    if (!vn) {
        rfs_free(fs);
        return -ROBO_ENOMEM;
    }
    m = robo_mount_create(&g_rfs_type, vn, fs);
    robo_vnode_put(vn);
    if (!m) {
        rfs_free(fs);
        return -ROBO_ENOMEM;
    }
    *out = m;
    return 0;
}

int robofs_unmount(RoboMount *mount)
{
    RoboRfs *fs;

    if (!mount)
        return -ROBO_EINVAL;
    fs = (RoboRfs *)mount->fs_instance;
    mount->fs_instance = 0;
    robo_mount_put(mount);
    rfs_free(fs);
    return 0;
}

static int rfs_fs_mount(RoboBlockDevice *dev, uint32_t flags, RoboMount **out)
{
    return robofs_mount(dev, flags, out);
}

static int rfs_fs_unmount(RoboMount *m)
{
    return robofs_unmount(m);
}

static const RoboFilesystemOps g_rfs_fsops = {
    .mount = rfs_fs_mount,
    .unmount = rfs_fs_unmount,
};

static RoboFilesystem g_rfs_type = {
    .name = "robofs",
    .id = 0,
    .ops = &g_rfs_fsops,
};

int robofs_register(void)
{
    return robo_vfs_register(&g_rfs_type);
}
