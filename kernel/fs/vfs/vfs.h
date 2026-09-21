#ifndef ROBO_VFS_H
#define ROBO_VFS_H

#include "fs/vfs/vnode.h"
#include "fs/vfs/mount.h"
#include "fs/vfs/path.h"
#include "fs/file.h"
#include "fs/block/block.h"

int robo_vfs_init(void);
int robo_vfs_register(RoboFilesystem *fs);
const RoboFilesystem *robo_vfs_find_fs(const char *name);

int robo_vfs_mount_root(const char *fs_name, RoboBlockDevice *dev,
                        uint32_t flags);
int robo_vfs_unmount_root(void);
RoboVnode *robo_vfs_root(void);

int robo_vfs_lookup(const char *path, RoboVnode **out);
int robo_vfs_lookup_at(RoboVnode *start, const char *path, uint32_t flags,
                       RoboVnode **out, char *last, unsigned last_max);

int robo_vfs_open(const char *path, uint32_t flags, uint32_t mode,
                  RoboFile **out);
int64_t robo_vfs_read(RoboFile *file, void *buffer, size_t count);
int64_t robo_vfs_write(RoboFile *file, const void *buffer, size_t count);
int robo_vfs_seek(RoboFile *file, int64_t offset, int whence);
int robo_vfs_stat_path(const char *path, RoboStat *st);
int robo_vfs_mkdir(const char *path, uint32_t mode);
int robo_vfs_rmdir(const char *path);
int robo_vfs_unlink(const char *path);
int robo_vfs_rename(const char *old_path, const char *new_path);
int robo_vfs_create(const char *path, RoboVnodeType type, uint32_t mode);
int robo_vfs_readdir(RoboFile *directory, RoboDirEntry *entry);

const RoboFileOps *robo_vfs_file_ops(void);

/*
 * Copy every file of the boot image into the root filesystem.
 *
 * @return 0 when every entry was stored; the number of entries the volume could not hold (each one named
 *         in a `VFS_IMPORT_SKIP` line, and left off the filesystem rather than half-written) when some
 *         were skipped; a negative error when the walk itself could not continue.
 */
int robo_vfs_import_initrd(void);

int robo_vfs_selftest(void);
#if !defined(PROCESS_HOST_TEST) && !defined(FD_HOST_TEST) && !defined(RFS_HOST_TEST)
void robo_vfs_fb_report(void);
#endif

#endif
