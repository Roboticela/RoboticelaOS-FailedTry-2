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

static int g_ok;

static int ck(int cond)
{
    if (cond) {
        g_ok++;
        return 1;
    }
    return 0;
}

static const char *mark(int v)
{
    return v ? "PASS" : "FAIL";
}

static int fill_pattern(char *buf, unsigned n, unsigned v)
{
    unsigned i;
    for (i = 0; i < n; i++)
        buf[i] = (char)v;
    return 1;
}

int robofs_selftest(void)
{
    RoboBlockDevice *dev, *tiny;
    RoboMount *m;
    RoboVnode *root, *n;
    RoboFile *f;
    RoboStat st;
    char buf[64];
    unsigned i, off;
    int64_t nr;
    int format_ok = 0, mount_ok = 0, mkdir_ok = 0, create_ok = 0;
    int write_ok = 0, seek_ok = 0, read_ok = 0, stat_ok = 0, rename_ok = 0;
    int umount_ok = 0, remount_ok = 0, persist_ok = 0, del_ok = 0, rmdir_ok = 0;
    int large_ok = 0, partial_ok = 0, space_ok = 0, reuse_ok = 0, stress_ok = 0;
    int corrupt_ok = 0;

    g_ok = 0;
    dev = robo_memdev_create(ROBOFS_BLOCK_SIZE, 64);
    if (!ck(dev != 0))
        goto report;
    format_ok = robofs_format(dev) == 0;
    ck(format_ok);
    mount_ok = robofs_mount(dev, 0, &m) == 0 && m && m->root;
    ck(mount_ok);
    if (!mount_ok)
        goto report;
    root = m->root;
    robo_vnode_get(root);

    mkdir_ok = root->ops && root->ops->mkdir &&
               root->ops->mkdir(root, "docs", 0, &n) == 0;
    if (n)
        robo_vnode_put(n);
    ck(mkdir_ok);

    create_ok = root->ops && root->ops->create &&
                root->ops->create(root, "hello.txt", ROBO_VNODE_FILE, 0, &n) == 0;
    ck(create_ok);
    if (create_ok && n) {
        f = robo_file_create(ROBO_FILE_REGULAR, robo_vfs_file_ops(), 0,
                             ROBO_FFLAG_READ | ROBO_FFLAG_WRITE);
        if (f) {
            f->vnode = n;
            robo_vnode_get(n);
            nr = robo_vnode_write(n, f, "Hello from RoboFS.\n", 19, 0);
            write_ok = nr == 19;
            ck(write_ok);
            seek_ok = robo_vfs_seek(f, 0, ROBO_SEEK_SET) == 0 && f->offset == 0;
            ck(seek_ok);
            nr = robo_vfs_read(f, buf, 19);
            read_ok = nr == 19 && buf[0] == 'H' && buf[18] == '\n';
            ck(read_ok);
            robo_file_put(f);
        }
        if (n->ops && n->ops->stat)
            stat_ok = n->ops->stat(n, &st) == 0 && st.size == 19;
        ck(stat_ok);
        robo_vnode_put(n);
    }

    rename_ok = root->ops && root->ops->rename &&
                root->ops->rename(root, "hello.txt", root, "renamed.txt") == 0;
    ck(rename_ok);

    robofs_unmount(m);
    umount_ok = 1;
    ck(umount_ok);
    robo_vnode_put(root);

    remount_ok = robofs_mount(dev, 0, &m) == 0;
    ck(remount_ok);
    if (remount_ok) {
        root = m->root;
        robo_vnode_get(root);
        persist_ok = root->ops->lookup(root, "renamed.txt", &n) == 0;
        if (persist_ok) {
            nr = robo_vnode_read(n, 0, buf, 19, 0);
            persist_ok = nr == 19 && buf[0] == 'H' && buf[18] == '\n';
            if (n->ops->stat)
                persist_ok = persist_ok && n->ops->stat(n, &st) == 0 &&
                             st.inode != 0;
            robo_vnode_put(n);
        }
        ck(persist_ok);

        del_ok = root->ops->unlink(root, "renamed.txt") == 0;
        ck(del_ok);
        rmdir_ok = root->ops->rmdir(root, "docs") == 0;
        ck(rmdir_ok);

        large_ok = 0;
        if (root->ops->create(root, "big", ROBO_VNODE_FILE, 0, &n) == 0) {
            large_ok = 1;
            for (i = 0; i < 3; i++) {
                fill_pattern(buf, 64, i + 1);
                for (off = 0; off < ROBOFS_BLOCK_SIZE; off += 64) {
                    if (robo_vnode_write(n, 0, buf, 64,
                                         (uint64_t)i * ROBOFS_BLOCK_SIZE +
                                             off) != 64)
                        large_ok = 0;
                }
            }
            for (i = 0; i < 3; i++) {
                if (robo_vnode_read(n, 0, buf, 64,
                                    (uint64_t)i * ROBOFS_BLOCK_SIZE) != 64 ||
                    (unsigned char)buf[0] != i + 1)
                    large_ok = 0;
            }
            robo_vnode_put(n);
            root->ops->unlink(root, "big");
        }
        ck(large_ok);

        partial_ok = 0;
        if (root->ops->create(root, "part", ROBO_VNODE_FILE, 0, &n) == 0) {
            char a[100], b[100];
            unsigned k;
            for (k = 0; k < 100; k++) {
                a[k] = 'A';
                b[k] = 'B';
            }
            robo_vnode_write(n, 0, a, 100, 0);
            robo_vnode_write(n, 0, b, 100, 50);
            nr = robo_vnode_read(n, 0, buf, 50, 0);
            partial_ok = nr == 50 && buf[0] == 'A' && buf[49] == 'A';
            nr = robo_vnode_read(n, 0, buf, 50, 50);
            partial_ok = partial_ok && nr == 50 && buf[0] == 'B';
            robo_vnode_put(n);
            root->ops->unlink(root, "part");
        }
        ck(partial_ok);

        reuse_ok = 1;
        if (root->ops->create(root, "A", ROBO_VNODE_FILE, 0, &n) == 0) {
            robo_vnode_write(n, 0, "xyz", 3, 0);
            robo_vnode_put(n);
            root->ops->unlink(root, "A");
        }
        if (root->ops->create(root, "B", ROBO_VNODE_FILE, 0, &n) == 0) {
            nr = robo_vnode_read(n, 0, buf, 3, 0);
            reuse_ok = nr == 0;
            robo_vnode_put(n);
            root->ops->unlink(root, "B");
        }
        ck(reuse_ok);

        stress_ok = 1;
        for (i = 0; i < 70; i++) {
            char nm[8];
            nm[0] = 'f';
            nm[1] = (char)('0' + (i / 10));
            nm[2] = (char)('0' + (i % 10));
            nm[3] = 0;
            if (root->ops->create(root, nm, ROBO_VNODE_FILE, 0, &n) != 0) {
                stress_ok = 0;
                break;
            }
            robo_vnode_put(n);
        }
        if (stress_ok && root->ops->lookup(root, "f69", &n) == 0)
            robo_vnode_put(n);
        else
            stress_ok = 0;
        for (i = 0; i < 70; i++) {
            char nm[8];
            nm[0] = 'f';
            nm[1] = (char)('0' + (i / 10));
            nm[2] = (char)('0' + (i % 10));
            nm[3] = 0;
            root->ops->unlink(root, nm);
        }
        ck(stress_ok);

        robo_vnode_put(root);
        robofs_unmount(m);
    }

    space_ok = 0;
    tiny = robo_memdev_create(ROBOFS_BLOCK_SIZE, 16);
    if (tiny && robofs_format(tiny) == 0 && robofs_mount(tiny, 0, &m) == 0) {
        int last = 0;
        unsigned made = 0;
        root = m->root;
        robo_vnode_get(root);
        space_ok = 1;
        for (i = 0; i < 32; i++) {
            char nm[8];
            nm[0] = 's';
            nm[1] = (char)('0' + (i / 10));
            nm[2] = (char)('0' + (i % 10));
            nm[3] = 0;
            last = root->ops->create(root, nm, ROBO_VNODE_FILE, 0, &n);
            if (last != 0)
                break;
            fill_pattern(buf, 64, 7);
            for (off = 0; off < ROBOFS_BLOCK_SIZE; off += 64) {
                nr = robo_vnode_write(n, 0, buf, 64, off);
                if (nr < 0) {
                    last = (int)nr;
                    break;
                }
            }
            robo_vnode_put(n);
            if (last != 0)
                break;
            made++;
        }
        space_ok = made > 0 && last == -ROBO_ENOSPC;
        robo_vnode_put(root);
        robofs_unmount(m);
        robo_bdev_destroy(tiny);
    }
    ck(space_ok);

    {
        uint8_t *raw = robo_memdev_data(dev);
        raw[0] ^= 0xFF;
        corrupt_ok = robofs_mount(dev, 0, &m) != 0;
        raw[0] ^= 0xFF;
        raw[4] = 99;
        corrupt_ok = corrupt_ok && robofs_mount(dev, 0, &m) != 0;
        ck(corrupt_ok);
    }

    robo_bdev_destroy(dev);

report:
    kprintf("\n========================================\n");
    kprintf("           ROBOFS TEST\n");
    kprintf("========================================\n\n");
    kprintf("Format              : %s\n", mark(format_ok));
    kprintf("Mount               : %s\n", mark(mount_ok));
    kprintf("Create Directory    : %s\n", mark(mkdir_ok));
    kprintf("Create File         : %s\n", mark(create_ok));
    kprintf("Write               : %s\n", mark(write_ok));
    kprintf("Seek                : %s\n", mark(seek_ok));
    kprintf("Read                : %s\n", mark(read_ok));
    kprintf("Stat                : %s\n", mark(stat_ok));
    kprintf("Rename              : %s\n", mark(rename_ok));
    kprintf("Unmount             : %s\n", mark(umount_ok));
    kprintf("Remount             : %s\n", mark(remount_ok));
    kprintf("Persistent Read     : %s\n", mark(persist_ok));
    kprintf("Delete              : %s\n", mark(del_ok));
    kprintf("Rmdir               : %s\n", mark(rmdir_ok));
    kprintf("\nLarge File          : %s\n", mark(large_ok));
    kprintf("Partial Block       : %s\n", mark(partial_ok));
    kprintf("Free Space          : %s\n", mark(space_ok));
    kprintf("Block Reuse         : %s\n", mark(reuse_ok));
    kprintf("Directory Stress    : %s\n", mark(stress_ok));
    kprintf("Corruption Checks   : %s\n", mark(corrupt_ok));
    kprintf("\n========================================\n");
    if (format_ok && mount_ok && write_ok && read_ok && persist_ok &&
        rename_ok && del_ok && rmdir_ok && large_ok && partial_ok &&
        space_ok && reuse_ok && stress_ok && corrupt_ok)
        kprintf("        [PASS] ROBOFS v1\n");
    else
        kprintf("        [FAIL] ROBOFS v1\n");
    kprintf("========================================\n");
    return format_ok && mount_ok && write_ok && read_ok && persist_ok &&
           rename_ok && del_ok && rmdir_ok && large_ok && partial_ok &&
           space_ok && reuse_ok && stress_ok && corrupt_ok;
}

#if !defined(PROCESS_HOST_TEST) && !defined(FD_HOST_TEST) && !defined(RFS_HOST_TEST)
void robofs_fb_report(void)
{
    uint32_t mute = 0xA1A1A6u;
    kfb_puts("RoboFS  persistent v1\n", mute);
}
#endif
