# Roboticela OS VFS

The Virtual File System is the kernel's filesystem-independent layer.
It answers how the kernel talks to filesystems. It does not describe how
RoboFS stores bytes on a disk.

```text
                 Application
                      │
                      ▼
                  Syscalls
                      │
                      ▼
                  File Descriptors
                      │
                      ▼
                     VFS
                      │
          ┌───────────┼───────────┐
          ▼           ▼           ▼
        RoboFS        FAT       ISO9660
          │
          ▼
       Storage
```

Today only RoboFS is a persistent backend. TestFS exists for VFS unit
tests. FAT and ISO9660 are not implemented.

## Architecture

Path resolution walks components from a start vnode (the root for
absolute paths). Each lookup is a backend operation. The VFS never
reads RoboFS object tables or bitmaps.

```text
Path → Mount → Vnode → filesystem-private object
```

A `RoboVnode` holds generic metadata (type, size, id, refcount, parent,
mount cover) plus `fs_private` and `ops`. Backends own the private
data.

Node types: file, directory, symlink, device, pipe, socket. Unused
types are allowed in the enum; backends return `RoboEnotsup` for
operations they do not implement. A NULL ops pointer is treated as
unsupported.

## Filesystem registration

`VfsRegister` records a `RoboFilesystem` with a unique name
(`"robofs"`, `"testfs"`). Duplicate names return `RoboEexist`. The
VFS dispatches through `RoboFilesystemOps` (`mount` / `unmount`) and
`RoboVnodeOps`. There is no `if (filesystem == ROBOFS)` in VFS code.

## Mounts

A `RoboMount` is a filesystem instance, its root vnode, and a mount
path. v1 mounts a single root:

```text
VfsInit
    → register robofs
    → format/mount memory block device
    → root vnode is /
```

`VfsRoot()` returns that vnode with an extra reference. Path
walk crosses `vnode->cover` when a later mount covers a directory.
Only root mounting is implemented now. There are no mount namespaces.

## The boot image and the root volume

Two things are separate here, and keeping them separate is what makes the boot
steady:

* the **boot image** (`RFS1`, `kernel/fs/robofs.c`) is the archive the bootloader
  hands the kernel — a flat list of `path=bytes` entries read through
  `FsFileAt`;
* the **root volume** is a RoboFS filesystem in a memory block device, and
  `VfsImportInitrd` copies every image entry into it, creating the
  directories each path names along the way. After that, paths work: a program
  can `stat`, `open` and `read` anything the image carried.

The root volume is sized from the image (`FsRootBlocks`), not from a
constant — see [filesystems/robofs.md](../filesystems/robofs.md#how-big-the-root-volume-is)
for the arithmetic and for what that replaced.

**An entry the volume cannot hold is skipped, named, and left on the image.**
RoboFS v1 caps one file at 4.03 MiB (direct + single indirect), so an image whose
ELF is bigger than that cannot have that file *in the filesystem*. Import prints
`VFS_IMPORT_SKIP  <path>`, removes the part it wrote (so a half-written file is
never left behind as a shorter one), and carries on with the next entry; the run
ends with `VFS_IMPORT_PARTIAL  <count>`. Walking on rather than stopping is the
point: the same run still imports the fonts and everything else after the large
file, and the ELF is still runnable, because `Exec` falls back to the image
when a path is not in the VFS (`kernel/exec/exec.c`).

A negative return from the walk, reported as `VFS_IMPORT_FAIL  <error>`, is a
different thing: the image itself could not be walked (a directory could not be
created, an entry could not be opened).

## Path resolution

`VfsLookup` / `VfsLookupAt` handle `/`, multiple slashes,
`.` , `..`, trailing slashes (must name a directory), and empty
components. Relative paths currently start at the VFS root (no cwd).

Limits: `RoboPathMax` 111, `RoboNameMax` 55, matching the syscall
path cap. Oversized paths and names are rejected. User strings are
copied into kernel buffers before VFS sees them.

`.` / `..` are interpreted by the VFS walker. They are not stored as
on-disk directory entries.

## File operations

`VfsOpen` resolves, optionally creates, type-checks, and builds a
`RoboFile` whose ops are `VfsFileOps()`. The file holds a vnode
reference. Read/write advance the file offset in those ops. Seek
supports `SEEK_SET` / `SEEK_CUR` / `SEEK_END` and rejects negative
results and signed overflow.

Directory APIs: `mkdir`, `rmdir`, `create`, `unlink`, `rename`,
`readdir` (backend optional). Rename resolves both parents and
calls the backend; it is not copy-plus-delete. Cross-mount rename
returns `RoboExdev`.

`VfsStatPath` fills a `RoboStat` (size, mode, inode, timestamps,
type, nlink). It is a Roboticela structure, not Linux `stat`.

## File descriptors

```text
SYS_OPEN → VFS path → RoboVnode → RoboFile → FD table → fd
SYS_READ/WRITE/SEEK/CLOSE → fd → RoboFile → vnode ops → backend
```

Console stdin/stdout/stderr stay console backends (`vnode == NULL`).
Regular files opened through VFS use the VFS file ops. Closing a VFS
file drops the vnode reference.

## Reference counting

Vnodes, mounts, and `RoboFile` objects are reference counted. A vnode
stays alive while any open file or parent link holds it. `release` runs
on the last put so the backend can free `fs_private`.

There is no dentry/inode/page cache in this phase. The public VFS API
is intentionally small so those caches can be added later.

## Errors

VFS returns the existing ABI codes (`ENOENT`, `EEXIST`, `ENOTDIR`,
`EISDIR`, `ENOTEMPTY`, `EACCES`, `EBADF`, `EINVAL`, `EFAULT`,
`ENOMEM`, `ENOTSUP`, `EIO`, `EXDEV`, `ENOSPC`, …). Backend errors are
passed through.

## Concurrency

Single CPU. `vfs_lock` (CLI) protects the filesystem registry and the
root mount pointer. RoboFS uses `rfs_lock` for metadata mutations.
Do not acquire `vfs_lock` while holding `rfs_lock`. Path walk is not
itself locked in v1; backends serialize their mutations.

## Tests

`VfsSelftest` mounts TestFS (`/hello.txt`, `/directory/nested.txt`),
exercises lookup/open/read/stat/mkdir/rename/unlink/rmdir, then
unmounts so the real root can be RoboFS.
