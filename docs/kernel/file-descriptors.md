# File descriptors

A file descriptor is a process-local integer handle, not a file. User
space never sees kernel pointers. Negative values are invalid.

```
fd  →  FD table entry  →  RoboFile  →  RoboFileOps  →  backend
```

`RoboFile` is the open-object: offset, access flags, operations, private
backend state, and a reference count. Multiple descriptors (and later
multiple processes) may share one `RoboFile`, which is how `dup` and
shared offsets work. Two separate `open()` calls create two `RoboFile`
objects with independent offsets.

## Process ownership

Every `RoboProcess` owns a `RoboFDTable`. Threads in that process share
it. The scheduler has no table of its own. Current process comes from
the current thread.

At process creation the table is filled with standard streams:

| fd | name   | type    | access | backend |
|----|--------|---------|--------|---------|
| 0  | stdin  | CONSOLE | read   | no keyboard yet → `ROBO_ENOTSUP` |
| 1  | stdout | CONSOLE | write  | kernel console (`kwrite`) |
| 2  | stderr | CONSOLE | write  | kernel console (`kwrite`) |

Stdin, stdout, and stderr are ordinary descriptors. Syscalls do not
branch on `fd == 1`. Closing them is allowed. A later `write(1, …)`
then returns `ROBO_EBADF` unless a new object is installed in that slot.

## Types

Extensible `RoboFileType` values: regular file, directory, pipe, socket,
device, console. Not every object has a filesystem inode. Pipes and
sockets are prepared as backends that currently return `ROBO_ENOTSUP`.

## Operations

`RoboFileOps` may omit any function. The generic layer never calls a
NULL pointer; missing ops return `ROBO_ENOTSUP`.

Supported now:

- console write (stdout/stderr)
- RoboFS regular-file read (initrd, read-only)
- close / reference drop

Seek exists for regular files. `ioctl` and `poll` are reserved. Ioctl
requests will use namespace in bits `[63:32]` and command in `[31:0]`.
No commands are assigned in this phase.

## Reference counting and locking

`robo_file_create` starts at refcount 1. `robo_fd_alloc` takes an extra
reference. `robo_fd_get` takes a reference that the caller must
`robo_file_put` after the operation, so a concurrent close cannot free
the object while it is in use.

Close clears the table slot first, then drops the table’s reference.
The last `put` runs `ops->close` (if present) and frees the `RoboFile`.

Table mutate/lookup runs with CPU interrupt flag cleared (single CPU).
Do not nest this with the scheduler lock.

## Syscall path

```
read/write/open/close
        ↓
SYS_READ / SYS_WRITE / SYS_OPEN / SYS_CLOSE
        ↓
current process FD table
        ↓
RoboFile + RoboFileOps
        ↓
console / RoboFS / future pipe, socket, device
```

`SYS_OPEN` copies a bounded user path, opens through RoboFS, wraps a
`RoboFile`, installs an fd, and drops the create reference. If fd
allocation fails, the file is released (`ROBO_EMFILE`). Flags other
than `ROBO_O_RDONLY` return `ROBO_EINVAL`. Initrd is read-only;
writes to regular files return `ROBO_ENOTSUP`.

User pointers are copied with `copy_from_user` / `copy_to_user`. Count
`0` validates the fd and returns `0` without touching the buffer.
Invalid fds return `ROBO_EBADF`. Bad pointers return `ROBO_EFAULT`.

Public ABI numbers are unchanged (ABI v1). See
[syscall-v1.md](../abi/syscall-v1.md).

## Process lifecycle

Process destroy closes every descriptor exactly once and frees the
table. `robo_fd_table_clone` exists for future inheritance: it shares
`RoboFile` objects (extra refs) and skips `CLOEXEC` entries. Process
create does not clone yet; each process gets a fresh stdio set.

## Limits

`ROBO_MAX_FDS` is 16 per process. Exhaustion returns `ROBO_EMFILE`.

## Diagnostics

`robo_fd_table_dump` and `robo_fd_selftest` print a table and the
verification banner. Dump is debug-gated. Do not treat these as part
of the user ABI.
