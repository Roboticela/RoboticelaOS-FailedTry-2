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
| 0  | stdin  | CONSOLE | read   | no keyboard yet → `RoboEnotsup` |
| 1  | stdout | CONSOLE | write  | kernel console (`kwrite`) |
| 2  | stderr | CONSOLE | write  | kernel console (`kwrite`) |

Stdin, stdout, and stderr are ordinary descriptors. Syscalls do not
branch on `fd == 1`. Closing them is allowed. A later `write(1, …)`
then returns `RoboEbadf` unless a new object is installed in that slot.

## Types

Extensible `RoboFileType` values: regular file, directory, pipe, socket,
device, console. Not every object has a filesystem inode. Pipes and
sockets are prepared as backends that currently return `RoboEnotsup`.

## Operations

`RoboFileOps` may omit any function. The generic layer never calls a
NULL pointer; missing ops return `RoboEnotsup`.

Supported now:

- console write (stdout/stderr)
- RoboFS regular-file read (initrd, read-only)
- close / reference drop

Seek exists for regular files. `ioctl` and `poll` are reserved. Ioctl
requests will use namespace in bits `[63:32]` and command in `[31:0]`.
No commands are assigned in this phase.

## Reference counting and locking

`FileCreate` starts at refcount 1. `FdAlloc` takes an extra
reference. `FdGet` takes a reference that the caller must
`FilePut` after the operation, so a concurrent close cannot free
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
allocation fails, the file is released (`RoboEmfile`). Flags other
than `RoboORdonly` return `RoboEinval`. Initrd is read-only;
writes to regular files return `RoboEnotsup`.

User pointers are copied with `copy_from_user` / `copy_to_user`. Count
`0` validates the fd and returns `0` without touching the buffer.
Invalid fds return `RoboEbadf`. Bad pointers return `RoboEfault`.

Public ABI numbers are unchanged (ABI v1). See
[syscall-v1.md](../abi/syscall-v1.md).

## Process lifecycle

Process destroy closes every descriptor exactly once and frees the
table. `FdTableClone` exists for future inheritance: it shares
`RoboFile` objects (extra refs) and skips `CLOEXEC` entries. Process
create does not clone yet; each process gets a fresh stdio set.

## Limits

`RoboMaxFds` is 16 per process. Exhaustion returns `RoboEmfile`.

## Diagnostics

`FdTableDump` and `FdSelftest` print a table and the
verification banner. Dump is debug-gated. Do not treat these as part
of the user ABI.
