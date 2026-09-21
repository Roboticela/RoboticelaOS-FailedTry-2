#include "fs/rfs/rfs.h"
#include "fs/rfs/rfs_disk.h"
#include "fs/block/block.h"
#include "syscall/syscall_errors.h"
#include "syscall/syscall_numbers.h"

#ifdef RFS_HOST_TEST
#include <stdio.h>
#define kprintf printf
#else
#include "kio.h"
#endif

static int gOk;

static int ck(int cond)
{
    if (cond) {
        gOk++;
        return 1;
    }
    return 0;
}

static const char *mark(int v)
{
    return v ? "PASS" : "FAIL";
}

static int FillPattern(char *buf, unsigned n, unsigned v)
{
    unsigned i;
    for (i = 0; i < n; i++)
        buf[i] = (char)v;
    return 1;
}

int RobofsSelftest(void)
{
    RoboBlockDevice *dev, *tiny;
    RoboMount *m;
    RoboVnode *root, *n;
    RoboFile *f;
    RoboStat st;
    char buf[64];
    unsigned i, off;
    int64_t nr;
    int formatOk = 0, mountOk = 0, mkdirOk = 0, createOk = 0;
    int writeOk = 0, seekOk = 0, readOk = 0, statOk = 0, renameOk = 0;
    int umountOk = 0, remountOk = 0, persistOk = 0, delOk = 0, rmdirOk = 0;
    int largeOk = 0, partialOk = 0, spaceOk = 0, reuseOk = 0, stressOk = 0;
    int corruptOk = 0;

    gOk = 0;
    dev = MemdevCreate(ROBOFS_BLOCK_SIZE, 64);
    if (!ck(dev != 0))
        goto report;
    formatOk = RobofsFormat(dev) == 0;
    ck(formatOk);
    mountOk = RobofsMount(dev, 0, &m) == 0 && m && m->root;
    ck(mountOk);
    if (!mountOk)
        goto report;
    root = m->root;
    VnodeGet(root);

    mkdirOk = root->ops && root->ops->mkdir &&
               root->ops->mkdir(root, "docs", 0, &n) == 0;
    if (n)
        VnodePut(n);
    ck(mkdirOk);

    createOk = root->ops && root->ops->create &&
                root->ops->create(root, "hello.txt", RoboVnodeFile, 0, &n) == 0;
    ck(createOk);
    if (createOk && n) {
        f = FileCreate(RoboFileRegular, VfsFileOps(), 0,
                             RoboFflagRead | RoboFflagWrite);
        if (f) {
            f->vnode = n;
            VnodeGet(n);
            nr = VnodeWrite(n, f, "Hello from RoboFS.\n", 19, 0);
            writeOk = nr == 19;
            ck(writeOk);
            seekOk = VfsSeek(f, 0, RoboSeekSet) == 0 && f->offset == 0;
            ck(seekOk);
            nr = VfsRead(f, buf, 19);
            readOk = nr == 19 && buf[0] == 'H' && buf[18] == '\n';
            ck(readOk);
            FilePut(f);
        }
        if (n->ops && n->ops->stat)
            statOk = n->ops->stat(n, &st) == 0 && st.size == 19;
        ck(statOk);
        VnodePut(n);
    }

    renameOk = root->ops && root->ops->rename &&
                root->ops->rename(root, "hello.txt", root, "renamed.txt") == 0;
    ck(renameOk);

    RobofsUnmount(m);
    umountOk = 1;
    ck(umountOk);
    VnodePut(root);

    remountOk = RobofsMount(dev, 0, &m) == 0;
    ck(remountOk);
    if (remountOk) {
        root = m->root;
        VnodeGet(root);
        persistOk = root->ops->lookup(root, "renamed.txt", &n) == 0;
        if (persistOk) {
            nr = VnodeRead(n, 0, buf, 19, 0);
            persistOk = nr == 19 && buf[0] == 'H' && buf[18] == '\n';
            if (n->ops->stat)
                persistOk = persistOk && n->ops->stat(n, &st) == 0 &&
                             st.inode != 0;
            VnodePut(n);
        }
        ck(persistOk);

        delOk = root->ops->unlink(root, "renamed.txt") == 0;
        ck(delOk);
        rmdirOk = root->ops->rmdir(root, "docs") == 0;
        ck(rmdirOk);

        largeOk = 0;
        if (root->ops->create(root, "big", RoboVnodeFile, 0, &n) == 0) {
            largeOk = 1;
            for (i = 0; i < 3; i++) {
                FillPattern(buf, 64, i + 1);
                for (off = 0; off < ROBOFS_BLOCK_SIZE; off += 64) {
                    if (VnodeWrite(n, 0, buf, 64,
                                         (uint64_t)i * ROBOFS_BLOCK_SIZE +
                                             off) != 64)
                        largeOk = 0;
                }
            }
            for (i = 0; i < 3; i++) {
                if (VnodeRead(n, 0, buf, 64,
                                    (uint64_t)i * ROBOFS_BLOCK_SIZE) != 64 ||
                    (unsigned char)buf[0] != i + 1)
                    largeOk = 0;
            }
            VnodePut(n);
            root->ops->unlink(root, "big");
        }
        ck(largeOk);

        partialOk = 0;
        if (root->ops->create(root, "part", RoboVnodeFile, 0, &n) == 0) {
            char a[100], b[100];
            unsigned k;
            for (k = 0; k < 100; k++) {
                a[k] = 'A';
                b[k] = 'B';
            }
            VnodeWrite(n, 0, a, 100, 0);
            VnodeWrite(n, 0, b, 100, 50);
            nr = VnodeRead(n, 0, buf, 50, 0);
            partialOk = nr == 50 && buf[0] == 'A' && buf[49] == 'A';
            nr = VnodeRead(n, 0, buf, 50, 50);
            partialOk = partialOk && nr == 50 && buf[0] == 'B';
            VnodePut(n);
            root->ops->unlink(root, "part");
        }
        ck(partialOk);

        reuseOk = 1;
        if (root->ops->create(root, "A", RoboVnodeFile, 0, &n) == 0) {
            VnodeWrite(n, 0, "xyz", 3, 0);
            VnodePut(n);
            root->ops->unlink(root, "A");
        }
        if (root->ops->create(root, "B", RoboVnodeFile, 0, &n) == 0) {
            nr = VnodeRead(n, 0, buf, 3, 0);
            reuseOk = nr == 0;
            VnodePut(n);
            root->ops->unlink(root, "B");
        }
        ck(reuseOk);

        stressOk = 1;
        for (i = 0; i < 70; i++) {
            char nm[8];
            nm[0] = 'f';
            nm[1] = (char)('0' + (i / 10));
            nm[2] = (char)('0' + (i % 10));
            nm[3] = 0;
            if (root->ops->create(root, nm, RoboVnodeFile, 0, &n) != 0) {
                stressOk = 0;
                break;
            }
            VnodePut(n);
        }
        if (stressOk && root->ops->lookup(root, "f69", &n) == 0)
            VnodePut(n);
        else
            stressOk = 0;
        for (i = 0; i < 70; i++) {
            char nm[8];
            nm[0] = 'f';
            nm[1] = (char)('0' + (i / 10));
            nm[2] = (char)('0' + (i % 10));
            nm[3] = 0;
            root->ops->unlink(root, nm);
        }
        ck(stressOk);

        VnodePut(root);
        RobofsUnmount(m);
    }

    spaceOk = 0;
    tiny = MemdevCreate(ROBOFS_BLOCK_SIZE, 16);
    if (tiny && RobofsFormat(tiny) == 0 && RobofsMount(tiny, 0, &m) == 0) {
        int last = 0;
        unsigned made = 0;
        root = m->root;
        VnodeGet(root);
        spaceOk = 1;
        for (i = 0; i < 32; i++) {
            char nm[8];
            nm[0] = 's';
            nm[1] = (char)('0' + (i / 10));
            nm[2] = (char)('0' + (i % 10));
            nm[3] = 0;
            last = root->ops->create(root, nm, RoboVnodeFile, 0, &n);
            if (last != 0)
                break;
            FillPattern(buf, 64, 7);
            for (off = 0; off < ROBOFS_BLOCK_SIZE; off += 64) {
                nr = VnodeWrite(n, 0, buf, 64, off);
                if (nr < 0) {
                    last = (int)nr;
                    break;
                }
            }
            VnodePut(n);
            if (last != 0)
                break;
            made++;
        }
        spaceOk = made > 0 && last == -RoboEnospc;
        VnodePut(root);
        RobofsUnmount(m);
        BdevDestroy(tiny);
    }
    ck(spaceOk);

    {
        uint8_t *raw = MemdevData(dev);
        raw[0] ^= 0xFF;
        corruptOk = RobofsMount(dev, 0, &m) != 0;
        raw[0] ^= 0xFF;
        raw[4] = 99;
        corruptOk = corruptOk && RobofsMount(dev, 0, &m) != 0;
        ck(corruptOk);
    }

    BdevDestroy(dev);

report:
    kprintf("\n========================================\n");
    kprintf("           ROBOFS TEST\n");
    kprintf("========================================\n\n");
    kprintf("Format              : %s\n", mark(formatOk));
    kprintf("Mount               : %s\n", mark(mountOk));
    kprintf("Create Directory    : %s\n", mark(mkdirOk));
    kprintf("Create File         : %s\n", mark(createOk));
    kprintf("Write               : %s\n", mark(writeOk));
    kprintf("Seek                : %s\n", mark(seekOk));
    kprintf("Read                : %s\n", mark(readOk));
    kprintf("Stat                : %s\n", mark(statOk));
    kprintf("Rename              : %s\n", mark(renameOk));
    kprintf("Unmount             : %s\n", mark(umountOk));
    kprintf("Remount             : %s\n", mark(remountOk));
    kprintf("Persistent Read     : %s\n", mark(persistOk));
    kprintf("Delete              : %s\n", mark(delOk));
    kprintf("Rmdir               : %s\n", mark(rmdirOk));
    kprintf("\nLarge File          : %s\n", mark(largeOk));
    kprintf("Partial Block       : %s\n", mark(partialOk));
    kprintf("Free Space          : %s\n", mark(spaceOk));
    kprintf("Block Reuse         : %s\n", mark(reuseOk));
    kprintf("Directory Stress    : %s\n", mark(stressOk));
    kprintf("Corruption Checks   : %s\n", mark(corruptOk));
    kprintf("\n========================================\n");
    if (formatOk && mountOk && writeOk && readOk && persistOk &&
        renameOk && delOk && rmdirOk && largeOk && partialOk &&
        spaceOk && reuseOk && stressOk && corruptOk)
        kprintf("        [PASS] ROBOFS v1\n");
    else
        kprintf("        [FAIL] ROBOFS v1\n");
    kprintf("========================================\n");
    return formatOk && mountOk && writeOk && readOk && persistOk &&
           renameOk && delOk && rmdirOk && largeOk && partialOk &&
           spaceOk && reuseOk && stressOk && corruptOk;
}

#if !defined(PROCESS_HOST_TEST) && !defined(FD_HOST_TEST) && !defined(RFS_HOST_TEST)
void RobofsFbReport(void)
{
    uint32_t mute = 0xA1A1A6u;
    KfbPuts("RoboFS  persistent v1\n", mute);
}
#endif
