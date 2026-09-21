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

static const RoboVnodeOps gTestVops;
static RoboFilesystem gTestFs;
static uint32_t gTestIds = 1;

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

static TestNode *TnNew(const char *name, uint32_t type)
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

static void TnFreeTree(TestNode *t)
{
    TestNode *c, *n;
    if (!t)
        return;
    c = t->child;
    while (c) {
        n = c->sibling;
        TnFreeTree(c);
        c = n;
    }
    kfree(t->data);
    kfree(t);
}

static RoboVnode *VnWrap(TestNode *t, RoboMount *m)
{
    RoboVnode *v = VnodeCreate(t->type == RoboVnodeDirectory
                                         ? RoboVnodeDirectory
                                         : RoboVnodeFile,
                                     &gTestVops, t);
    if (!v)
        return 0;
    v->id = gTestIds++;
    v->size = t->size;
    v->mount = m;
    return v;
}

static int TestLookup(RoboVnode *dir, const char *name, RoboVnode **out)
{
    TestNode *t = (TestNode *)dir->fsPrivate;
    TestNode *c;
    for (c = t->child; c; c = c->sibling) {
        if (ncmp(c->name, name)) {
            *out = VnWrap(c, dir->mount);
            return *out ? 0 : -RoboEnomem;
        }
    }
    return -RoboEnoent;
}

static int64_t TestRead(RoboVnode *node, RoboFile *file, void *buffer,
                         size_t count, uint64_t offset)
{
    TestNode *t = (TestNode *)node->fsPrivate;
    size_t n;
    (void)file;
    if (t->type != RoboVnodeFile)
        return -(int64_t)RoboEisdir;
    if (offset >= t->size)
        return 0;
    n = t->size - (uint32_t)offset;
    if (n > count)
        n = count;
    cmem(buffer, t->data + offset, (unsigned)n);
    return (int64_t)n;
}

static int64_t TestWrite(RoboVnode *node, RoboFile *file, const void *buffer,
                          size_t count, uint64_t offset)
{
    TestNode *t = (TestNode *)node->fsPrivate;
    uint32_t need;
    uint8_t *p;
    unsigned i;
    (void)file;
    if (t->type != RoboVnodeFile)
        return -(int64_t)RoboEisdir;
    need = (uint32_t)offset + (uint32_t)count;
    if (need > t->cap) {
        p = (uint8_t *)kmalloc(need + 16);
        if (!p)
            return -(int64_t)RoboEnomem;
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

static int TestCreate(RoboVnode *dir, const char *name, RoboVnodeType type,
                       uint32_t mode, RoboVnode **out)
{
    TestNode *t = (TestNode *)dir->fsPrivate;
    TestNode *c;
    (void)mode;
    for (c = t->child; c; c = c->sibling) {
        if (ncmp(c->name, name))
            return -RoboEexist;
    }
    c = TnNew(name, type);
    if (!c)
        return -RoboEnomem;
    c->parent = t;
    c->sibling = t->child;
    t->child = c;
    *out = VnWrap(c, dir->mount);
    return *out ? 0 : -RoboEnomem;
}

static int TestUnlink(RoboVnode *dir, const char *name)
{
    TestNode *t = (TestNode *)dir->fsPrivate;
    TestNode **pp, *c;
    for (pp = &t->child; *pp; pp = &(*pp)->sibling) {
        c = *pp;
        if (ncmp(c->name, name)) {
            if (c->type == RoboVnodeDirectory)
                return -RoboEisdir;
            *pp = c->sibling;
            kfree(c->data);
            kfree(c);
            return 0;
        }
    }
    return -RoboEnoent;
}

static int TestMkdir(RoboVnode *dir, const char *name, uint32_t mode,
                      RoboVnode **out)
{
    return TestCreate(dir, name, RoboVnodeDirectory, mode, out);
}

static int TestRmdir(RoboVnode *dir, const char *name)
{
    TestNode *t = (TestNode *)dir->fsPrivate;
    TestNode **pp, *c;
    for (pp = &t->child; *pp; pp = &(*pp)->sibling) {
        c = *pp;
        if (ncmp(c->name, name)) {
            if (c->type != RoboVnodeDirectory)
                return -RoboEnotdir;
            if (c->child)
                return -RoboEnotempty;
            *pp = c->sibling;
            kfree(c);
            return 0;
        }
    }
    return -RoboEnoent;
}

static int TestStat(RoboVnode *node, RoboStat *st)
{
    TestNode *t = (TestNode *)node->fsPrivate;
    zmem(st, (unsigned)sizeof(*st));
    st->size = t->size;
    st->inode = node->id;
    st->type = t->type;
    return 0;
}

static int TestRename(RoboVnode *od, const char *on, RoboVnode *nd,
                       const char *nn)
{
    TestNode *os = (TestNode *)od->fsPrivate;
    TestNode *ns = (TestNode *)nd->fsPrivate;
    TestNode **pp, *c, *x;
    unsigned i;

    for (x = ns->child; x; x = x->sibling) {
        if (ncmp(x->name, nn))
            return -RoboEexist;
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
    return -RoboEnoent;
}

static const RoboVnodeOps gTestVops = {
    .lookup = TestLookup,
    .read = TestRead,
    .write = TestWrite,
    .create = TestCreate,
    .unlink = TestUnlink,
    .mkdir = TestMkdir,
    .rmdir = TestRmdir,
    .rename = TestRename,
    .stat = TestStat,
};

static int TestMount(RoboBlockDevice *dev, uint32_t flags, RoboMount **out)
{
    TestNode *root, *hello, *d, *nested;
    RoboVnode *vn;
    RoboMount *m;
    const char *msg = "hello vfs";
    unsigned i;
    (void)dev;
    (void)flags;
    root = TnNew("", RoboVnodeDirectory);
    hello = TnNew("hello.txt", RoboVnodeFile);
    d = TnNew("directory", RoboVnodeDirectory);
    nested = TnNew("nested.txt", RoboVnodeFile);
    if (!root || !hello || !d || !nested)
        return -RoboEnomem;
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
    vn = VnWrap(root, 0);
    if (!vn)
        return -RoboEnomem;
    m = MountCreate(&gTestFs, vn, root);
    VnodePut(vn);
    if (!m)
        return -RoboEnomem;
    *out = m;
    return 0;
}

static int TestUnmount(RoboMount *m)
{
    TestNode *root;
    if (!m)
        return -RoboEinval;
    root = (TestNode *)m->fsInstance;
    m->fsInstance = 0;
    MountPut(m);
    TnFreeTree(root);
    return 0;
}

static const RoboFilesystemOps gTestFsops = {
    .mount = TestMount,
    .unmount = TestUnmount,
};

static RoboFilesystem gTestFs = {
    .name = "testfs",
    .id = 0,
    .ops = &gTestFsops,
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

int VfsSelftest(void)
{
    int ok = 1;
    RoboFile *f;
    RoboVnode *n;
    char buf[16];
    int64_t r;
    RoboStat st;

    if (VfsRegister(&gTestFs) != 0 && VfsFindFs("testfs") == 0) {
#ifdef RFS_HOST_TEST
        printf("VFS check fail: register\n");
#endif
        return 0;
    }
    if (VfsMountRoot("testfs", 0, 0) != 0) {
#ifdef RFS_HOST_TEST
        printf("VFS check fail: mount_root\n");
#endif
        return 0;
    }

    if (!check(VfsLookup("/hello.txt", &n) == 0, &ok, "lookup hello"))
        goto done;
    VnodePut(n);
    if (!check(VfsLookup("/nope", &n) == -RoboEnoent, &ok, "lookup missing"))
        goto done;
    if (!check(VfsLookup("/directory/nested.txt", &n) == 0, &ok, "lookup nested"))
        goto done;
    VnodePut(n);
    if (!check(VfsLookup("/directory/.", &n) == 0, &ok, "lookup dot"))
        goto done;
    VnodePut(n);
    if (!check(VfsLookup("/directory/../hello.txt", &n) == 0, &ok, "lookup dotdot"))
        goto done;
    VnodePut(n);
    if (!check(VfsLookup("hello.txt", &n) == 0, &ok, "lookup relative"))
        goto done;
    VnodePut(n);
    if (!check(VfsOpen("/hello.txt", RoboORdonly, 0, &f) == 0, &ok, "open"))
        goto done;
    r = VfsRead(f, buf, 9);
    if (!check(r == 9 && buf[0] == 'h', &ok, "read")) {
        FilePut(f);
        goto done;
    }
    FilePut(f);
    if (!check(VfsStatPath("/hello.txt", &st) == 0 && st.size == 9, &ok, "stat"))
        goto done;
    if (!check(VfsMkdir("/newdir", 0) == 0, &ok, "mkdir"))
        goto done;
    if (!check(VfsCreate("/newdir/a.txt", RoboVnodeFile, 0) == 0, &ok, "create"))
        goto done;
    if (!check(VfsRename("/newdir/a.txt", "/newdir/b.txt") == 0, &ok, "rename"))
        goto done;
    if (!check(VfsUnlink("/newdir/b.txt") == 0, &ok, "unlink"))
        goto done;
    if (!check(VfsRmdir("/newdir") == 0, &ok, "rmdir"))
        goto done;
    if (!check(VfsLookup("", &n) == -RoboEinval, &ok, "empty path"))
        goto done;

done:
    VfsUnmountRoot();
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
void VfsFbReport(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    KfbPuts("VFS  vnodes ", mute);
    KfbPutu((unsigned long long)VnodeStatLive(), white);
    KfbPuts("\n", mute);
}
#endif
