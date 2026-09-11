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
types are allowed in the enum; backends return `ROBO_ENOTSUP` for
operations they do not implement. A NULL ops pointer is treated as
unsupported.

## Filesystem registration

`robo_vfs_register` records a `RoboFilesystem` with a unique name
(`"robofs"`, `"testfs"`). Duplicate names return `ROBO_EEXIST`. The
VFS dispatches through `RoboFilesystemOps` (`mount` / `unmount`) and
`RoboVnodeOps`. There is no `if (filesystem == ROBOFS)` in VFS code.

## Mounts

A `RoboMount` is a filesystem instance, its root vnode, and a mount
path. v1 mounts a single root:

```text
robo_vfs_init
    → register robofs
    → format/mount memory block device
    → root vnode is /
```

`robo_vfs_root()` returns that vnode with an extra reference. Path
walk crosses `vnode->cover` when a later mount covers a directory.
Only root mounting is implemented now. There are no mount namespaces.

## Path resolution

`robo_vfs_lookup` / `robo_vfs_lookup_at` handle `/`, multiple slashes,
`.` , `..`, trailing slashes (must name a directory), and empty
components. Relative paths currently start at the VFS root (no cwd).

Limits: `ROBO_PATH_MAX` 111, `ROBO_NAME_MAX` 55, matching the syscall
path cap. Oversized paths and names are rejected. User strings are
copied into kernel buffers before VFS sees them.

`.` / `..` are interpreted by the VFS walker. They are not stored as
on-disk directory entries.

## File operations

`robo_vfs_open` resolves, optionally creates, type-checks, and builds a
`RoboFile` whose ops are `robo_vfs_file_ops()`. The file holds a vnode
reference. Read/write advance the file offset in those ops. Seek
supports `SEEK_SET` / `SEEK_CUR` / `SEEK_END` and rejects negative
results and signed overflow.

Directory APIs: `mkdir`, `rmdir`, `create`, `unlink`, `rename`,
`readdir` (backend optional). Rename resolves both parents and
calls the backend; it is not copy-plus-delete. Cross-mount rename
returns `ROBO_EXDEV`.

`robo_vfs_stat_path` fills a `RoboStat` (size, mode, inode, timestamps,
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

`robo_vfs_selftest` mounts TestFS (`/hello.txt`, `/directory/nested.txt`),
exercises lookup/open/read/stat/mkdir/rename/unlink/rmdir, then
unmounts so the real root can be RoboFS.
