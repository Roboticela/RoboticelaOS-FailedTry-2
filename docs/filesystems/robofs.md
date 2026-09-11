# RoboFS v1

RoboFS is the first native persistent filesystem of Roboticela OS. It
lives entirely behind the VFS. Applications never call RoboFS APIs.

```text
User Application
        │
        ▼
     Syscalls
        │
        ▼
  File Descriptor → RoboFile → VFS → RoboFS → Block Device API
                                                     │
                                      ┌──────────────┼──────────────┐
                                      ▼              ▼              ▼
                                   Memory         VirtIO          NVMe
                                 (v1 tests)     (future)       (future)
```

## Goals

Correctness, a clear on-disk format, small code, and remount
persistence. v1 does not implement journaling, compression, snapshots,
encryption, deduplication, copy-on-write, or a page cache.

## Block device

RoboFS talks only to `RoboBlockDevice` (`read` / `write` by LBA). The
v1 kernel uses a memory-backed device (`robo_memdev_create`). Hardware
drivers can plug in later without changing RoboFS.

## Mount / format

`robofs_format` writes superblock, allocation bitmap, object table, and
an empty root directory. `robofs_mount` validates magic, major version,
checksum, block size, bounds, and the root object. Failure unmounts
cleanly. `robofs_register` exposes the backend as VFS type `"robofs"`.

Timestamps stored on disk are the kernel monotonic clock (`robo_time_now`)
until wall-clock time exists. They are not calendar time.

Permissions/uid/gid fields exist on disk and in `RoboStat` but are not
enforced yet.

## Crash semantics

**There is no journal.** A power loss during metadata updates may
corrupt the filesystem. Successful in-memory operations are internally
consistent, errors are returned, and metadata writes are issued in a
sensible order (data block, then object, then superblock). Do not treat
v1 as crash-safe.

## Limitations

* One filesystem instance per block device
* Direct + single indirect block mapping only
* Empty-directory `rmdir` only
* `.` / `..` are VFS path syntax, not directory entries
* Memory block device in the kernel; no VirtIO/NVMe/SATA yet
* Initrd (`RFS1` pack in `kernel/fs/robofs.c`) remains a separate
  boot archive used by the ELF loader. Persistent RoboFS is `kernel/fs/rfs/`
  with magic `RBFS`.

## Future

Journaling should wrap metadata mutations without replacing the format
wholesale. Reserved superblock flags exist for encryption and similar
extensions. Additional filesystems (FAT, ISO9660) attach through VFS,
not by changing RoboFS.

See [robofs-format.md](robofs-format.md) for the exact disk layout.
