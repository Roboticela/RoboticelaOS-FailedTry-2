# Roboticela OS x86_64 Syscall ABI v1

This document is the public contract between user-space programs and the
Roboticela OS kernel. Internal kernel APIs (`robo_process_create`,
`robo_vmm_map`, `robo_pmm_alloc`, and similar) are not part of this ABI
and must not be called from user space.

Architecture: **x86_64 only**. There is no ARM64, RISC-V, 32-bit, or
Linux compatibility layer.

ABI version: **major 1, minor 0**.

```c
#define ROBO_SYSCALL_ABI_MAJOR 1
#define ROBO_SYSCALL_ABI_MINOR 0
```

Query with `SYS_ABI_INFO` (optional). Programs are not required to query
before every call.

---

## Numbering ranges

| Range     | Role                         |
|-----------|------------------------------|
| 0–31      | Core ABI v1 (this document)  |
| 32        | SYS_THREAD (this document)   |
| 33–99     | Reserved core                |
| 100–199   | Process / thread (future)    |
| 200–299   | Memory (future)              |
| 300–399   | IPC (future)                 |
| 400–499   | Filesystem (future)          |
| 500+      | Experimental (`ROBO_SYS_EXPERIMENTAL_BASE`) |

Assigned numbers are never reused. A removed call keeps its number as
`ROBO_SYS_RESERVED_*` and returns `ROBO_ENOSYS`. Experimental numbers
are not stable merely because a program used them.

Syscall numbers are explicit constants. They do not depend on enum
ordering, source-file order, function addresses, or build flags.

---

## Calling convention

**Roboticela OS x86_64 Syscall ABI v1** uses:

| Role            | Register |
|-----------------|----------|
| syscall number  | RAX      |
| argument 1      | RDI      |
| argument 2      | RSI      |
| argument 3      | RDX      |
| argument 4      | R10      |
| argument 5      | R8       |
| argument 6      | R9       |
| return value    | RAX      |

Argument 4 is R10 because `SYSCALL` overwrites RCX with the user RIP.
This is a Roboticela decision driven by the x86_64 instruction, not a
Linux compatibility layer.

### Register classes

| Class              | Registers                         | Rule                                      |
|--------------------|-----------------------------------|-------------------------------------------|
| caller-clobbered   | RAX, RDI, RSI, RDX, R8, R9, R10   | User must assume they change              |
| syscall-clobbered  | RCX, R11                          | Hardware: RIP and RFLAGS                  |
| callee-preserved   | RBX, RBP, R12, R13, R14, R15      | Kernel restores them                      |
| kernel-saved       | user RIP, RSP, RFLAGS, CS, SS     | Held in the kernel iframe                 |

### Entry vs return

| Mechanism        | Use                                           |
|------------------|-----------------------------------------------|
| `IRETQ`          | First ring-3 entry and interrupt/reschedule return |
| `SYSCALL`/`SYSRETQ` | Fast user/kernel syscall transition        |

`SYSCALL` enters at `IA32_LSTAR` with `EFER.SCE = 1`. `IA32_STAR` is
programmed from the GDT:

* `[47:32] = 0x08` kernel CS → SYSCALL CS=`0x08`, SS=`0x10`
* `[63:48] = 0x10` → SYSRET CS=`0x20` (RPL 3 → `0x23`), SS=`0x18` (RPL 3 → `0x1B`)

`IA32_FMASK` clears `IF` for the stack switch. The dispatcher re-enables
interrupts so long syscalls stay preemptible, then clears `IF` again
before `SYSRETQ`. User `RFLAGS` are sanitized on return
(`(RFLAGS & 0xCD5) | 0x202`) so privileged bits cannot be restored.

A syscall always runs on the current thread kernel stack (`TSS.RSP0` /
`robo_syscall_kstack`). It never continues on the user stack.

Before `SYSRETQ`, user RIP and RSP must be canonical and inside the user
virtual window. Invalid return state terminates the process; the kernel
does not `SYSRET` to a bad address.

---

## Error convention

Failures return the two's-complement negation of a public error code in
RAX, always in **[-4095, -1]**.

Any other RAX value is success. User virtual addresses live in the high
canonical window (`0xFFFF808000000000` …), so they look negative as
signed 64-bit integers. **Do not** treat every negative RAX as an error.

```c
if ((unsigned long)r >= (unsigned long)-4095ul)
    err = (int)(-r);
```

| Code          | Value | Meaning                          |
|---------------|-------|----------------------------------|
| `ROBO_EPERM`  | 1     | Operation not permitted          |
| `ROBO_ENOENT` | 2     | Path not found                   |
| `ROBO_EIO`    | 3     | I/O error                        |
| `ROBO_EBADF`  | 4     | Bad file descriptor              |
| `ROBO_ENOMEM` | 5     | Out of memory                    |
| `ROBO_EACCES` | 6     | Permission denied                |
| `ROBO_EFAULT` | 7     | Bad user address                 |
| `ROBO_EBUSY`  | 8     | Resource busy                    |
| `ROBO_EEXIST` | 9     | Already exists                   |
| `ROBO_EINVAL` | 10    | Invalid argument                 |
| `ROBO_ENOSYS` | 11    | Syscall or operation not present |
| `ROBO_EINTR`  | 12    | Interrupted (reserved)           |
| `ROBO_ENOTSUP`| 13    | Not supported                    |
| `ROBO_EMFILE` | 14    | Too many process fds             |
| `ROBO_ENFILE` | 15    | Too many files                   |
| `ROBO_ENOTDIR`| 16    | Not a directory                  |
| `ROBO_EISDIR` | 17    | Is a directory                   |
| `ROBO_ENOTEMPTY` | 18 | Directory not empty              |
| `ROBO_EXDEV`  | 19    | Cross-device rename              |
| `ROBO_ENOSPC` | 20    | No space on the filesystem       |
| `ROBO_ECHILD` | 21    | No matching child process        |
| `ROBO_EAGAIN` | 22    | Try again / empty queue          |
| `ROBO_ENODEV` | 23    | Device not available             |
| `ROBO_EOVERFLOW` | 27 | Integer overflow                 |
| `ROBO_EADDRINUSE` | 28 | Address already in use           |
| `ROBO_ECONNREFUSED` | 29 | Connection refused             |
| `ROBO_ECONNRESET` | 30 | Connection reset                 |
| `ROBO_ETIMEDOUT` | 31 | Connection timed out             |
| `ROBO_ENETUNREACH` | 32 | Network unreachable              |
| `ROBO_EHOSTUNREACH` | 33 | Host unreachable                 |
| `ROBO_ENOTCONN` | 34 | Socket not connected             |
| `ROBO_EISCONN` | 35 | Socket already connected         |
| `ROBO_EAFNOSUPPORT` | 36 | Address family not supported     |
| `ROBO_EMSGSIZE` | 37 | Message too long                 |

Internal kernel error enums are never returned.

`SYS_TIME` unit is **nanoseconds** of monotonic time. That unit is
permanent for ABI v1.

---

## Time unit

`SYS_TIME` returns monotonic nanoseconds since timer initialization.
It is not wall-clock/calendar time. A later change of unit requires a
new ABI major version.

---

## Feature flags (`SYS_ABI_INFO`)

Optional `RoboAbiInfo` (when argument 1 is a user pointer):

| Flag                     | Meaning                          |
|--------------------------|----------------------------------|
| `ROBO_ABI_FEAT_WRITE`    | `SYS_WRITE` implemented          |
| `ROBO_ABI_FEAT_EXIT`     | `SYS_EXIT` implemented           |
| `ROBO_ABI_FEAT_TIME`     | `SYS_TIME` implemented           |
| `ROBO_ABI_FEAT_SLEEP`    | `SYS_SLEEP` implemented          |
| `ROBO_ABI_FEAT_MMAP`     | anonymous `SYS_MMAP`/`MUNMAP`    |
| `ROBO_ABI_FEAT_FS`       | RoboFS `OPEN`/`READ`/`CLOSE`     |
| `ROBO_ABI_FEAT_THREAD`   | `SYS_THREAD_CREATE`              |
| `ROBO_ABI_FEAT_PROCESS`  | `SYS_PROCESS_CREATE`             |
| `ROBO_ABI_FEAT_WAIT`     | `SYS_WAIT`                       |
| `ROBO_ABI_FEAT_DRIVER`   | `SYS_DRIVER_QUERY`               |
| `ROBO_ABI_FEAT_IPC`      | `SYS_IPC`                        |
| `ROBO_ABI_FEAT_DISPLAY`  | `SYS_DISPLAY`                    |
| `ROBO_ABI_FEAT_NET`      | `SYS_SOCKET`                     |
| `ROBO_ABI_FEAT_TLS`      | `SYS_THREAD` SET_FS / user TLS   |

A minor version does not imply every optional feature exists. Use flags.

---

## Summary

| Number | Name                 | Args | Return        | Blocking        |
|--------|----------------------|------|---------------|-----------------|
| 0      | SYS_ABI_INFO         | 0–1  | packed/error  | never           |
| 1      | SYS_EXIT             | 1    | never         | terminates      |
| 2      | SYS_READ             | 3    | bytes/error   | may block later |
| 3      | SYS_WRITE            | 3    | bytes/error   | may block       |
| 4      | SYS_OPEN             | 3    | fd/error      | may block       |
| 5      | SYS_CLOSE            | 1    | 0/error       | normally no     |
| 6      | SYS_MMAP             | 6    | address/error | normally no     |
| 7      | SYS_MUNMAP           | 2    | 0/error       | normally no     |
| 8      | SYS_PROCESS_CREATE   | 1    | pid/error     | may block       |
| 9      | SYS_THREAD_CREATE    | 3    | tid/error     | normally no     |
| 10     | SYS_SLEEP            | 1    | 0/error       | blocks          |
| 11     | SYS_TIME             | 0    | nanoseconds   | never           |
| 12     | SYS_IPC              | 5    | handle/bytes  | may block       |
| 13–15  | reserved             | —    | ENOSYS        | never           |
| 16     | SYS_SEEK             | 3    | offset/error  | normally no     |
| 17     | SYS_STAT             | 2    | 0/error       | normally no     |
| 18     | SYS_MKDIR            | 2    | 0/error       | may block       |
| 19     | SYS_RMDIR            | 1    | 0/error       | may block       |
| 20     | SYS_UNLINK           | 1    | 0/error       | may block       |
| 21     | SYS_RENAME           | 2    | 0/error       | may block       |
| 22     | SYS_WAIT             | 1–3  | pid/error     | may block       |
| 23     | SYS_GETPID           | 0    | pid           | never           |
| 24     | SYS_DRIVER_QUERY     | 1    | 0/error       | never           |
| 25     | SYS_DISPLAY          | 5    | 0/bytes/error | never           |
| 26     | SYS_SOCKET          | 6    | fd/bytes/error| may block       |
| 27–31  | reserved             | —    | ENOSYS        | never           |
| 32     | SYS_THREAD           | 1–3  | tid/status/0  | may block       |

---

## SYS_ABI_INFO

Number: `0`

Arguments: RDI = optional `RoboAbiInfo *` (0 to skip)

Returns: RAX = `(minor << 32) | major`. If RDI is non-zero, the struct is
also written to user memory.

Pointer: RDI, kernel writes, size `sizeof(RoboAbiInfo)`

Blocking: never

Permissions: current process address space

Side effects: none

ABI: v1

---

## SYS_EXIT

Number: `1`

Arguments: RDI = status

Returns: does not return

Pointer: none

Blocking: always terminates the **calling process** and every remaining
user thread of that process. No runnable user thread of an exited
process is left.

Permissions: caller only

Side effects: process state `TERMINATED`, threads reaped by the scheduler

ABI: v1

---

## SYS_READ

Number: `2`

Arguments: RDI = fd, RSI = buffer, RDX = length

Returns: bytes copied, or error.

- Invalid or unused fd: `ROBO_EBADF`
- Descriptor without read access (stdout/stderr): `ROBO_EBADF`
- stdin: `ROBO_ENOTSUP` until a terminal input backend exists
- RoboFS regular files: file bytes, advancing the open-file offset

Pointer: RSI is user memory the kernel writes. Count `0` returns `0`
without requiring a valid buffer.

Blocking: may block in a future input implementation; file reads do not

Blocking: may block in a future input implementation; file reads do not

Permissions: fd must belong to the current process

Side effects: advances the file position

ABI: v1

---

## SYS_WRITE

Number: `3`

Arguments: RDI = fd, RSI = buffer, RDX = length

Returns: bytes written, or error.

fd `1` and fd `2` are stdout and stderr only because process creation
installs console objects there. The syscall does not special-case those
integers; it looks up the descriptor and dispatches `RoboFileOps.write`.

Pointer: RSI is user memory the kernel reads. Ownership stays with the
caller; the kernel copies before using the bytes. Count `0` returns `0`
without requiring a valid buffer.

Blocking: may block

Permissions: fd must belong to the current process

Side effects: console output for stdout/stderr

ABI: v1

---

## SYS_OPEN

Number: `4`

Arguments: RDI = path, RSI = flags, RDX = mode

Returns: fd, or error. Access bits are `ROBO_O_RDONLY` (0),
`ROBO_O_WRONLY` (1), `ROBO_O_RDWR` (2). Optional `ROBO_O_CREAT` and
`ROBO_O_TRUNC`. Unknown bits return `ROBO_EINVAL`. Path is a
NUL-terminated user string, maximum 111 bytes plus NUL. The kernel
resolves the path through VFS; it does not pass the user pointer to a
filesystem backend.

Pointer: RDI is user memory. The kernel copies the string; it does not
retain the user pointer.

Blocking: may block

Permissions: current process receives the new fd

Side effects: fd table entry owned by the current process

ABI: v1

---

## SYS_CLOSE

Number: `5`

Arguments: RDI = fd

Returns: 0 or error

Pointer: none

Blocking: normally non-blocking

Permissions: fd must belong to the current process. Process A cannot
close Process B's descriptors.

Side effects: releases the fd and drops the open-file reference. The
underlying object is destroyed when its last reference is gone.

ABI: v1

---

## SYS_MMAP

Number: `6`

Arguments:

* RDI = address (0 to allocate, or `MAP_FIXED`)
* RSI = length
* RDX = protection (`ROBO_PROT_READ/WRITE/EXEC`)
* R10 = flags (`ROBO_MAP_PRIVATE`, `ROBO_MAP_ANON`, `ROBO_MAP_FIXED`)
* R8  = fd (`-1` for anonymous)
* R9  = offset (must be 0 for anonymous)

Returns: mapped user address, or error

v1 subset: anonymous mappings only (`ROBO_MAP_ANON`). File maps return
`ROBO_ENOSYS`. Write-xor-execute is rejected (`ROBO_EINVAL`). Kernel
addresses and non-canonical addresses are rejected. Length is rounded
up to pages. Mapping is created in the **current** process address space
only.

Pointer: none (the returned range becomes user memory)

Blocking: normally non-blocking

Permissions: current process address space

Side effects: pages are owned by the current process until `SYS_MUNMAP`
or process exit

ABI: v1

---

## SYS_MUNMAP

Number: `7`

Arguments: RDI = address (page-aligned), RSI = length

Returns: 0 or error

The range must lie in the current process user window. Kernel memory,
kernel stacks, and process structures cannot be unmapped.

Pointer: none

Blocking: normally non-blocking

Permissions: current process address space

Side effects: removes the mapping and frees the physical pages

ABI: v1

---

## SYS_PROCESS_CREATE

Number: `8`

Arguments: RDI = path to an ELF in RoboFS. Optional RSI = user `char **`
argv, RDX = argc. argc 0 keeps the historical default (argv[0] is the
basename of the path). argc 1–8 copies those strings onto the child
stack. Extra arguments beyond 8 return `ROBO_EINVAL`.

Returns: new pid, or error

This is not a raw `robo_process_create()` export. The kernel copies the
path, loads the ELF into a new address space, and schedules a user
thread. The child is a separate process with its own resources.

Pointer: RDI is a user string; the kernel does not retain it

Blocking: may block

Permissions: current process may create a child; there is no interface
to attach a thread to an arbitrary foreign process

Side effects: new process + thread, new address space, fds 0–2

ABI: v1

---

## SYS_THREAD_CREATE

Number: `9`

Arguments: RDI = user entry, RSI = user stack, RDX = argument (placed in
the new thread's RDI)

Returns: new tid, or error

Entry must be user executable memory of the **current** process. Stack
must be writable user memory of the current process. The kernel never
takes a user-supplied kernel function pointer.

Pointer: RDI/RSI are addresses in the current address space, not copied

Blocking: normally non-blocking

Permissions: new thread belongs to the current process and shares its
address space

Side effects: additional runnable thread

ABI: v1

---

## SYS_SLEEP

Number: `10`

Arguments: RDI = milliseconds

Returns: 0 or error. Zero milliseconds returns immediately.

The thread enters `SLEEPING` until the monotonic deadline. This is not a
busy loop. A timer interrupt may preempt other threads while this one
sleeps.

Pointer: none

Blocking: always (when milliseconds > 0)

Permissions: calling thread

Side effects: scheduler sleep / wake

ABI: v1

---

## SYS_TIME

Number: `11`

Arguments: none

Returns: monotonic nanoseconds (never an error for a live kernel)

Pointer: none

Blocking: never

Permissions: none

Side effects: none

ABI: v1

---

## SYS_IPC

Number: `12`

Arguments: RDI = operation, RSI = handle, RDX = buffer, R10 = length,
R8 = flags (`ROBO_IPC_NOWAIT` = 1)

Operations:

| Op | Name | Result |
|----|------|--------|
| 1 | SEND | bytes copied onto the peer channel |
| 2 | RECV | bytes copied from the channel or listen port |
| 3 | LISTEN | port handle (`buf` = name) |
| 4 | CONNECT | channel handle (`buf` = name) |
| 5 | CLOSE | 0 |
| 6 | SHM_CREATE | shm id (`len` = bytes, max 8 MiB) |
| 7 | SHM_MAP | user VA of shared pages (`handle` = shm id) |
| 8 | SHM_UNMAP | 0 (`buf` = mapped VA) |

LISTEN/CONNECT names are C strings, max 31 bytes. Recv on a listen port
returns `RoboIpcAccept` (`magic` `ROBO_IPC_ACCEPT_MAGIC`, server-side
handle, peer pid). The kernel stamps the caller pid; clients cannot
forge it.

SEND/RECV copy at most 256 bytes. Empty SEND with handle 0 returns
`ROBO_EBADF`. Peer close returns `ROBO_EIO`. `NOWAIT` with an empty
queue returns `ROBO_EAGAIN`. Blocking RECV uses `robo_thread_block`.

Shared pages are identity-mapped physical frames. `SYS_MUNMAP` of an
SHM VA does not free those frames while any shm object still owns them.
Process exit closes ports/channels and drops shm maps.

Pointer: `buf` is user memory for names, payloads, or mapped VAs

Blocking: RECV/LISTEN accept without `NOWAIT` may block

Permissions: handles belong to the calling process

Side effects: creates kernel port/channel/shm objects

ABI: v1

---

## SYS_DISPLAY

Number: `25`

Arguments: RDI = operation, RSI unused, RDX = buffer, R10 = length,
R8 = flags (`ROBO_DISP_NOWAIT`; `CLAIM` uses `ROBO_DISP_ROLE_OUTPUT` /
`ROBO_DISP_ROLE_INPUT`)

Operations: `CLAIM` (1), `INFO` (2), `PRESENT` (3), `INPUT` (4),
`INJECT` (5), `RELEASE` (6). `CLAIM` takes a role in flags (output,
input, or both). Missing framebuffer still claims in HEADLESS mode
(`INFO.ready = 0`). `PRESENT` copies a user `RoboDisplayPresent` buffer
onto the UEFI framebuffer at `dest_x`,`dest_y` (output owner only).
`INPUT`/`INJECT` require the input owner. RoboCompositor claims output;
RoboWindowServer claims input.

Pointer: user structs in `sdk/include/robosys/display.h` and
`sdk/include/robosys/input.h`

Blocking: never (empty input returns `ROBO_EAGAIN`)

Permissions: display owner after CLAIM

Side effects: framebuffer contents; input queue

ABI: v1

---

## SYS_SOCKET

Number: `26`

Arguments: RDI = operation, RSI–R9 per operation. Socket handles are
ordinary file descriptors (`ROBO_FILE_SOCKET`). `SYS_CLOSE` on that
descriptor closes the socket.

Operations (`sdk/include/robosys/socket.h`):

| Op | Name | Arguments |
|----|------|-----------|
| 1 | SOCKET | RSI domain, RDX type, R10 protocol |
| 2 | BIND | RSI fd, RDX `RoboSockAddr *` |
| 3 | LISTEN | RSI fd, RDX backlog |
| 4 | ACCEPT | RSI fd, RDX optional `RoboSockAddr *` |
| 5 | CONNECT | RSI fd, RDX `RoboSockAddr *` |
| 6 | SEND | RSI fd, RDX buf, R10 len |
| 7 | RECV | RSI fd, RDX buf, R10 len |
| 8 | SENDTO | RSI fd, RDX buf, R10 len, R9 `RoboSockAddr *` |
| 9 | RECVFROM | RSI fd, RDX buf, R10 len, R9 `RoboSockAddr *` |
| 10 | SHUTDOWN | RSI fd, RDX how |
| 11 | GETSOCKNAME | RSI fd, RDX `RoboSockAddr *` |
| 12 | GETPEERNAME | RSI fd, RDX `RoboSockAddr *` |
| 13 | INFO | RDX `RoboNetInfo *` |
| 14 | PING | RSI IPv4 (host order), RDX timeout ms |

`RoboSockAddr.port` and `RoboSockAddr.addr` are host byte order.
Domain is `ROBO_AF_INET`. Types are `ROBO_SOCK_STREAM` (TCP) and
`ROBO_SOCK_DGRAM` (UDP).

Blocking: CONNECT, RECV, ACCEPT, SEND (full buffer), PING, and BIND
ARP resolution may block. Empty non-blocking receive returns
`ROBO_EAGAIN` (non-blocking is not exposed in this phase).

Pointer: user buffers copied with `robo_copy_from_user` /
`robo_copy_to_user`.

Side effects: NIC transmit/receive, ARP cache, TCP/UDP state

ABI: v1

---

## SYS_THREAD

Number: `32`

Multiplexed thread and TLS operations. Numbers 27–31 remain
`ROBO_ENOSYS` forever.

Arguments: RDI = operation.

| Op | Name | Arguments | Returns |
|----|------|-----------|---------|
| 1 | YIELD | none | 0 |
| 2 | EXIT | RSI = status | never (this thread) |
| 3 | JOIN | RSI = tid | exit status, or error |
| 4 | SET_FS | RSI = user TCB address | 0 |
| 5 | GETTID | none | tid |
| 6 | GETPPID | none | parent pid |

`SYS_EXIT` (1) still terminates the whole process. `THREAD EXIT` ends
only the calling thread. The last thread of a process still tears the
process down.

`SET_FS` writes `IA32_FS_BASE` for the current thread and restores it
on every context switch. libc places a TCB at that address (`self` at
offset 0, `errno` at offset 8).

JOIN waits for a same-process user thread. `ROBO_ESRCH` if the tid is
unknown. Unjoined user threads remain until join or process destroy.

Pointer: SET_FS requires 16 writable user bytes

Blocking: JOIN and YIELD may reschedule

ABI: v1

---

## SYS_SEEK

Number: `16`

Arguments: RDI = fd, RSI = offset (signed), RDX = whence
(`ROBO_SEEK_SET` 0, `ROBO_SEEK_CUR` 1, `ROBO_SEEK_END` 2)

Returns: the resulting file offset, or error. Negative results are
rejected. Console descriptors return `ROBO_ENOTSUP` if they have no
seek op.

Pointer: none

Blocking: normally non-blocking

Permissions: fd must belong to the current process

Side effects: updates the open-file offset

ABI: v1

---

## SYS_STAT

Number: `17`

Arguments: RDI = path, RSI = `RoboStat *`

Returns: 0 or error. The kernel copies a `RoboStat` to user memory
after VFS lookup. Layout is defined in `sdk/include/robosys/types.h`.

Pointer: RDI is a user string; RSI is user memory the kernel writes

Blocking: may block

Permissions: current process address space

Side effects: none

ABI: v1

---

## SYS_MKDIR

Number: `18`

Arguments: RDI = path, RSI = mode (stored, not yet enforced)

Returns: 0 or error (`ROBO_EEXIST`, `ROBO_ENOTDIR`, …)

Pointer: RDI is a user string

Blocking: may block

ABI: v1

---

## SYS_RMDIR

Number: `19`

Arguments: RDI = path

Returns: 0 or error. Non-empty directories return `ROBO_ENOTEMPTY`.

Pointer: RDI is a user string

ABI: v1

---

## SYS_UNLINK

Number: `20`

Arguments: RDI = path

Returns: 0 or error. Directories must use `SYS_RMDIR`.

Pointer: RDI is a user string

ABI: v1

---

## SYS_RENAME

Number: `21`

Arguments: RDI = old path, RSI = new path

Returns: 0 or error. Cross-mount rename returns `ROBO_EXDEV`. The
object id is unchanged when the backend supports it.

Pointer: both arguments are user strings

ABI: v1

---

## SYS_WAIT

Number: `22`

Arguments: RDI = pid (0 = any child), RSI = flags, RDX = optional
`int *` exit status (0 to ignore)

Returns: reaped child pid, or error.

Flags: `ROBO_WAIT_NOHANG` (1) returns `ROBO_EAGAIN` instead of blocking.
`ROBO_ECHILD` means no matching child. The kernel reaps the zombie; user
space does not scan the process table.

Pointer: RDX, kernel writes `sizeof(int)` when non-zero

Blocking: yes, unless `NOHANG`

Permissions: caller’s own children only

Side effects: child process object destroyed after status is collected

ABI: v1

---

## SYS_GETPID

Number: `23`

Arguments: none

Returns: calling process id (PID 0 is the kernel process; PID 1 is
`/System/init`)

Pointer: none

Blocking: never

Permissions: none

Side effects: none

ABI: v1

---

## SYS_DRIVER_QUERY

Number: `24`

Arguments: RDI = `RoboDriverStatus *`

Returns: 0 or a negated error (`ROBO_EINVAL`, `ROBO_EFAULT`)

Pointer: RDI, kernel writes, size `sizeof(RoboDriverStatus)`

Blocking: never

Permissions: current process address space

Side effects: none. This is a snapshot of Driver Manager diagnostics
(PCI count, loaded drivers, storage/graphics/input readiness). It does
not install packages or bind devices.

ABI: v1

---

## Stability

After ABI v1 is published:

* syscall numbers
* argument register positions
* argument meanings
* return-value semantics
* units
* user-visible structure layouts (`RoboAbiInfo`)

do not change without a compatibility strategy. Prefer a new syscall
number or ABI v2.

Future work (signals, futexes, dynamic linking) gets new numbers.

Public headers live in `sdk/include/robosys/`. They do not expose
`RoboProcess`, `RoboThread`, page tables, or kernel heap structures.
