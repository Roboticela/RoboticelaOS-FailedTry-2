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

## The largest file v1 can hold

Direct + single indirect means 8 + 1024 block pointers, so **4,227,072 bytes
(4.03 MiB) is the ceiling for one file** — with a 4096-byte block. `map_block`
answers `ROBO_EIO` past it, and a write that crosses the ceiling fails partway.

This is not a theoretical number: the runtime's own ELF is larger than it, and
the boot image carries it. What the kernel does about that is stated in
[kernel/vfs.md](../kernel/vfs.md#the-boot-image-and-the-root-volume): the file
stays on the image, is named in a `VFS_IMPORT_SKIP` line, and is still
*executable*, because the loader reads an image file it cannot find in the VFS.

## How big the root volume is

`robo_fs_root_blocks(block_size)` (`kernel/fs/robofs.c`) sizes it from what the
boot image holds: the payload rounded up to blocks, plus eight blocks per file
(an object slot, a directory entry and the tail block) plus 64 blocks of
metadata and directory blocks, plus 256 blocks (1 MiB) left free for the files
the system writes at run time. The result is capped at 16384 blocks (64 MiB) so
a corrupt length field cannot ask for the heap.

It used to be a constant 512 blocks. That was a guess about the image, and the
image outgrew it: once the payload passed two megabytes the import stopped
partway through and every file after the stopping point — the fonts among them —
was simply absent from the filesystem, with one `VFS_IMPORT_FAIL` line to say so.

## Future

Journaling should wrap metadata mutations without replacing the format
wholesale. Reserved superblock flags exist for encryption and similar
extensions. Additional filesystems (FAT, ISO9660) attach through VFS,
not by changing RoboFS.

See [robofs-format.md](robofs-format.md) for the exact disk layout.
