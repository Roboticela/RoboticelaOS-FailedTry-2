#ifndef ROBO_VFS_H
#define ROBO_VFS_H

#include "fs/vfs/vnode.h"
#include "fs/vfs/mount.h"
#include "fs/vfs/path.h"
#include "fs/file.h"
#include "fs/block/block.h"

int VfsInit(void);
int VfsRegister(RoboFilesystem *fs);
const RoboFilesystem *VfsFindFs(const char *name);

int VfsMountRoot(const char *fsName, RoboBlockDevice *dev,
                        uint32_t flags);
int VfsUnmountRoot(void);
RoboVnode *VfsRoot(void);

int VfsLookup(const char *path, RoboVnode **out);
int VfsLookupAt(RoboVnode *start, const char *path, uint32_t flags,
                       RoboVnode **out, char *last, unsigned lastMax);

int VfsOpen(const char *path, uint32_t flags, uint32_t mode,
                  RoboFile **out);
int64_t VfsRead(RoboFile *file, void *buffer, size_t count);
int64_t VfsWrite(RoboFile *file, const void *buffer, size_t count);
int VfsSeek(RoboFile *file, int64_t offset, int whence);
int VfsStatPath(const char *path, RoboStat *st);
int VfsMkdir(const char *path, uint32_t mode);
int VfsRmdir(const char *path);
int VfsUnlink(const char *path);
int VfsRename(const char *oldPath, const char *newPath);
int VfsCreate(const char *path, RoboVnodeType type, uint32_t mode);
int VfsReaddir(RoboFile *directory, RoboDirEntry *entry);

const RoboFileOps *VfsFileOps(void);

/*
 * Copy every file of the boot image into the root filesystem.
 *
 * @return 0 when every entry was stored; the number of entries the volume could not hold (each one named
 *         in a `VFS_IMPORT_SKIP` line, and left off the filesystem rather than half-written) when some
 *         were skipped; a negative error when the walk itself could not continue.
 */
int VfsImportInitrd(void);

int VfsSelftest(void);
#if !defined(PROCESS_HOST_TEST) && !defined(FD_HOST_TEST) && !defined(RFS_HOST_TEST)
void VfsFbReport(void);
#endif

#endif
