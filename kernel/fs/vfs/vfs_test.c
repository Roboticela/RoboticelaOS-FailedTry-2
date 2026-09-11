#include "fs/vfs/vfs.h"
#include "syscall/syscall_errors.h"
#include "syscall/syscall_numbers.h"

#if defined(PROCESS_HOST_TEST) || defined(RFS_HOST_TEST)
#include <stdlib.h>
#include <stdio.h>
#define kmalloc malloc
#define kfree free
#else
#include "mm/heap/heap.h"
#include "kio.h"
#endif

typedef struct TestNode {
    char name[32];
    uint32_t type;
    uint8_t *data;
    uint32_t size;
    uint32_t cap;
    struct TestNode *parent;
    struct TestNode *child;
    struct TestNode *sibling;
} TestNode;

static const RoboVnodeOps g_test_vops;
static RoboFilesystem g_test_fs;
static uint32_t g_test_ids = 1;

static void zmem(void *dst, unsigned n)
{
    unsigned char *p = (unsigned char *)dst;
    while (n--)
        *p++ = 0;
}

static void cmem(void *d, const void *s, unsigned n)
{
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    while (n--)
        *dd++ = *ss++;
}

static int ncmp(const char *a, const char *b)
{
    unsigned i;
    for (i = 0; a[i] || b[i]; i++) {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

static TestNode *tn_new(const char *name, uint32_t type)
{
    TestNode *t = (TestNode *)kmalloc(sizeof(*t));
    unsigned i;
    if (!t)
        return 0;
    zmem(t, (unsigned)sizeof(*t));
    for (i = 0; name && name[i] && i < 31; i++)
        t->name[i] = name[i];
    t->type = type;
    return t;
}

static void tn_free_tree(TestNode *t)
{
    TestNode *c, *n;
    if (!t)
        return;
    c = t->child;
    while (c) {
        n = c->sibling;
        tn_free_tree(c);
        c = n;
    }
    kfree(t->data);
    kfree(t);
}

static RoboVnode *vn_wrap(TestNode *t, RoboMount *m)
{
    RoboVnode *v = robo_vnode_create(t->type == ROBO_VNODE_DIRECTORY
                                         ? ROBO_VNODE_DIRECTORY
                                         : ROBO_VNODE_FILE,
                                     &g_test_vops, t);
    if (!v)
        return 0;
    v->id = g_test_ids++;
    v->size = t->size;
    v->mount = m;
    return v;
}

static int test_lookup(RoboVnode *dir, const char *name, RoboVnode **out)
{
    TestNode *t = (TestNode *)dir->fs_private;
    TestNode *c;
    for (c = t->child; c; c = c->sibling) {
        if (ncmp(c->name, name)) {
            *out = vn_wrap(c, dir->mount);
            return *out ? 0 : -ROBO_ENOMEM;
        }
    }
    return -ROBO_ENOENT;
}

static int64_t test_read(RoboVnode *node, RoboFile *file, void *buffer,
                         size_t count, uint64_t offset)
{
    TestNode *t = (TestNode *)node->fs_private;
    size_t n;
    (void)file;
    if (t->type != ROBO_VNODE_FILE)
        return -(int64_t)ROBO_EISDIR;
    if (offset >= t->size)
        return 0;
    n = t->size - (uint32_t)offset;
    if (n > count)
        n = count;
    cmem(buffer, t->data + offset, (unsigned)n);
    return (int64_t)n;
}

static int64_t test_write(RoboVnode *node, RoboFile *file, const void *buffer,
                          size_t count, uint64_t offset)
{
    TestNode *t = (TestNode *)node->fs_private;
    uint32_t need;
    uint8_t *p;
    unsigned i;
    (void)file;
    if (t->type != ROBO_VNODE_FILE)
        return -(int64_t)ROBO_EISDIR;
    need = (uint32_t)offset + (uint32_t)count;
    if (need > t->cap) {
        p = (uint8_t *)kmalloc(need + 16);
        if (!p)
            return -(int64_t)ROBO_ENOMEM;
        for (i = 0; i < t->size; i++)
            p[i] = t->data[i];
        kfree(t->data);
        t->data = p;
        t->cap = need + 16;
    }
    cmem(t->data + offset, buffer, (unsigned)count);
    if (need > t->size)
        t->size = need;
    node->size = t->size;
    return (int64_t)count;
}

static int test_create(RoboVnode *dir, const char *name, RoboVnodeType type,
                       uint32_t mode, RoboVnode **out)
{
    TestNode *t = (TestNode *)dir->fs_private;
    TestNode *c;
    (void)mode;
    for (c = t->child; c; c = c->sibling) {
        if (ncmp(c->name, name))
            return -ROBO_EEXIST;
    }
    c = tn_new(name, type);
    if (!c)
        return -ROBO_ENOMEM;
    c->parent = t;
    c->sibling = t->child;
    t->child = c;
    *out = vn_wrap(c, dir->mount);
    return *out ? 0 : -ROBO_ENOMEM;
}

static int test_unlink(RoboVnode *dir, const char *name)
{
    TestNode *t = (TestNode *)dir->fs_private;
    TestNode **pp, *c;
    for (pp = &t->child; *pp; pp = &(*pp)->sibling) {
        c = *pp;
        if (ncmp(c->name, name)) {
            if (c->type == ROBO_VNODE_DIRECTORY)
                return -ROBO_EISDIR;
            *pp = c->sibling;
            kfree(c->data);
            kfree(c);
            return 0;
        }
    }
    return -ROBO_ENOENT;
}

static int test_mkdir(RoboVnode *dir, const char *name, uint32_t mode,
                      RoboVnode **out)
{
    return test_create(dir, name, ROBO_VNODE_DIRECTORY, mode, out);
}

static int test_rmdir(RoboVnode *dir, const char *name)
{
    TestNode *t = (TestNode *)dir->fs_private;
    TestNode **pp, *c;
    for (pp = &t->child; *pp; pp = &(*pp)->sibling) {
        c = *pp;
        if (ncmp(c->name, name)) {
            if (c->type != ROBO_VNODE_DIRECTORY)
                return -ROBO_ENOTDIR;
            if (c->child)
                return -ROBO_ENOTEMPTY;
            *pp = c->sibling;
            kfree(c);
            return 0;
        }
    }
    return -ROBO_ENOENT;
}

static int test_stat(RoboVnode *node, RoboStat *st)
{
    TestNode *t = (TestNode *)node->fs_private;
    zmem(st, (unsigned)sizeof(*st));
    st->size = t->size;
    st->inode = node->id;
    st->type = t->type;
    return 0;
}

static int test_rename(RoboVnode *od, const char *on, RoboVnode *nd,
                       const char *nn)
{
    TestNode *os = (TestNode *)od->fs_private;
    TestNode *ns = (TestNode *)nd->fs_private;
    TestNode **pp, *c, *x;
    unsigned i;

    for (x = ns->child; x; x = x->sibling) {
        if (ncmp(x->name, nn))
            return -ROBO_EEXIST;
    }
    for (pp = &os->child; *pp; pp = &(*pp)->sibling) {
        c = *pp;
        if (!ncmp(c->name, on))
            continue;
        *pp = c->sibling;
        zmem(c->name, 32);
        for (i = 0; nn[i] && i < 31; i++)
            c->name[i] = nn[i];
        c->parent = ns;
        c->sibling = ns->child;
        ns->child = c;
        return 0;
    }
    return -ROBO_ENOENT;
}

static const RoboVnodeOps g_test_vops = {
    .lookup = test_lookup,
    .read = test_read,
    .write = test_write,
    .create = test_create,
    .unlink = test_unlink,
    .mkdir = test_mkdir,
    .rmdir = test_rmdir,
    .rename = test_rename,
    .stat = test_stat,
};

static int test_mount(RoboBlockDevice *dev, uint32_t flags, RoboMount **out)
{
    TestNode *root, *hello, *d, *nested;
    RoboVnode *vn;
    RoboMount *m;
    const char *msg = "hello vfs";
    unsigned i;
    (void)dev;
    (void)flags;
    root = tn_new("", ROBO_VNODE_DIRECTORY);
    hello = tn_new("hello.txt", ROBO_VNODE_FILE);
    d = tn_new("directory", ROBO_VNODE_DIRECTORY);
    nested = tn_new("nested.txt", ROBO_VNODE_FILE);
    if (!root || !hello || !d || !nested)
        return -ROBO_ENOMEM;
    hello->size = 9;
    hello->cap = 9;
    hello->data = (uint8_t *)kmalloc(9);
    for (i = 0; i < 9; i++)
        hello->data[i] = (uint8_t)msg[i];
    nested->size = 6;
    nested->cap = 6;
    nested->data = (uint8_t *)kmalloc(6);
    nested->data[0] = 'n';
    nested->data[1] = 'e';
    nested->data[2] = 's';
    nested->data[3] = 't';
    nested->data[4] = 'e';
    nested->data[5] = 'd';
    hello->parent = root;
    hello->sibling = d;
    d->parent = root;
    d->child = nested;
    nested->parent = d;
    root->child = hello;
    vn = vn_wrap(root, 0);
    if (!vn)
        return -ROBO_ENOMEM;
    m = robo_mount_create(&g_test_fs, vn, root);
    robo_vnode_put(vn);
    if (!m)
        return -ROBO_ENOMEM;
    *out = m;
    return 0;
}

static int test_unmount(RoboMount *m)
{
    TestNode *root;
    if (!m)
        return -ROBO_EINVAL;
    root = (TestNode *)m->fs_instance;
    m->fs_instance = 0;
    robo_mount_put(m);
    tn_free_tree(root);
    return 0;
}

static const RoboFilesystemOps g_test_fsops = {
    .mount = test_mount,
    .unmount = test_unmount,
};

static RoboFilesystem g_test_fs = {
    .name = "testfs",
    .id = 0,
    .ops = &g_test_fsops,
};

static int check(int cond, int *ok, const char *what)
{
    if (cond)
        return 1;
    *ok = 0;
#ifdef RFS_HOST_TEST
    printf("VFS check fail: %s\n", what);
#else
    (void)what;
#endif
    return 0;
}

int robo_vfs_selftest(void)
{
    int ok = 1;
    RoboFile *f;
    RoboVnode *n;
    char buf[16];
    int64_t r;
    RoboStat st;

    if (robo_vfs_register(&g_test_fs) != 0 && robo_vfs_find_fs("testfs") == 0) {
#ifdef RFS_HOST_TEST
        printf("VFS check fail: register\n");
#endif
        return 0;
    }
    if (robo_vfs_mount_root("testfs", 0, 0) != 0) {
#ifdef RFS_HOST_TEST
        printf("VFS check fail: mount_root\n");
#endif
        return 0;
    }

    if (!check(robo_vfs_lookup("/hello.txt", &n) == 0, &ok, "lookup hello"))
        goto done;
    robo_vnode_put(n);
    if (!check(robo_vfs_lookup("/nope", &n) == -ROBO_ENOENT, &ok, "lookup missing"))
        goto done;
    if (!check(robo_vfs_lookup("/directory/nested.txt", &n) == 0, &ok, "lookup nested"))
        goto done;
    robo_vnode_put(n);
    if (!check(robo_vfs_lookup("/directory/.", &n) == 0, &ok, "lookup dot"))
        goto done;
    robo_vnode_put(n);
    if (!check(robo_vfs_lookup("/directory/../hello.txt", &n) == 0, &ok, "lookup dotdot"))
        goto done;
    robo_vnode_put(n);
    if (!check(robo_vfs_lookup("hello.txt", &n) == 0, &ok, "lookup relative"))
        goto done;
    robo_vnode_put(n);
    if (!check(robo_vfs_open("/hello.txt", ROBO_O_RDONLY, 0, &f) == 0, &ok, "open"))
        goto done;
    r = robo_vfs_read(f, buf, 9);
    if (!check(r == 9 && buf[0] == 'h', &ok, "read")) {
        robo_file_put(f);
        goto done;
    }
    robo_file_put(f);
    if (!check(robo_vfs_stat_path("/hello.txt", &st) == 0 && st.size == 9, &ok, "stat"))
        goto done;
    if (!check(robo_vfs_mkdir("/newdir", 0) == 0, &ok, "mkdir"))
        goto done;
    if (!check(robo_vfs_create("/newdir/a.txt", ROBO_VNODE_FILE, 0) == 0, &ok, "create"))
        goto done;
    if (!check(robo_vfs_rename("/newdir/a.txt", "/newdir/b.txt") == 0, &ok, "rename"))
        goto done;
    if (!check(robo_vfs_unlink("/newdir/b.txt") == 0, &ok, "unlink"))
        goto done;
    if (!check(robo_vfs_rmdir("/newdir") == 0, &ok, "rmdir"))
        goto done;
    if (!check(robo_vfs_lookup("", &n) == -ROBO_EINVAL, &ok, "empty path"))
        goto done;

done:
    robo_vfs_unmount_root();
#if !defined(PROCESS_HOST_TEST) && !defined(RFS_HOST_TEST)
    kprintf("VFS_SELFTEST  %s\n", ok ? "ok" : "fail");
    kprintf("\n========================================\n");
    kprintf("         ROBOTICELA OS VFS\n");
    kprintf("========================================\n");
    kprintf("Generic VFS            : %s\n", ok ? "PASS" : "FAIL");
    kprintf("RoboVnode              : %s\n", ok ? "PASS" : "FAIL");
    kprintf("Filesystem registration: %s\n", ok ? "PASS" : "FAIL");
    kprintf("Mount abstraction      : %s\n", ok ? "PASS" : "FAIL");
    kprintf("Path resolution        : %s\n", ok ? "PASS" : "FAIL");
    kprintf("Mock filesystem        : %s\n", ok ? "PASS" : "FAIL");
    kprintf("========================================\n");
    if (ok)
        kprintf("[PASS] ROBOTICELA OS VFS\n");
    else
        kprintf("[FAIL] ROBOTICELA OS VFS\n");
#endif
    return ok;
}

#if !defined(PROCESS_HOST_TEST) && !defined(FD_HOST_TEST) && !defined(RFS_HOST_TEST)
void robo_vfs_fb_report(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    kfb_puts("VFS  vnodes ", mute);
    kfb_putu((unsigned long long)robo_vnode_stat_live(), white);
    kfb_puts("\n", mute);
}
#endif
