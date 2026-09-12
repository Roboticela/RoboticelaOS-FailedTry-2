# libc ABI (x86_64)

ABI version follows syscall ABI **major 1, minor 0**.

## Types

| Type | Size | Align |
|------|------|-------|
| `int8_t` / `uint8_t` | 1 | 1 |
| `int16_t` / `uint16_t` | 2 | 2 |
| `int32_t` / `uint32_t` | 4 | 4 |
| `int64_t` / `uint64_t` | 8 | 8 |
| `size_t` / `uintptr_t` | 8 | 8 |
| `ptrdiff_t` / `intptr_t` | 8 | 8 |
| pointers | 8 | 8 |

Calling convention is SysV AMD64 (Clang `-target x86_64-unknown-none`).

## Syscalls

User code uses `robo_syscall` (`libc/arch/x86_64/syscall.S`). Hardware
registers are `RAX, RDI, RSI, RDX, R10, R8, R9`. See
`docs/abi/syscall-v1.md`.

Errors occupy `RAX` in `[-4095, -1]`. libc stores the positive code in
thread-local `errno`.

## FILE

`FILE` is an opaque wrapper around a Roboticela file descriptor.
`stdin`/`stdout`/`stderr` are fds 0/1/2.

## Threads / TLS

Each thread has an FS-based TCB:

| Offset | Field |
|--------|--------|
| 0 | `self` (pointer to TCB) |
| 8 | `errno` (`int`) |

`SYS_THREAD` op `SET_FS` installs the base. The kernel restores FS on
every context switch.

## Process startup

See `startup.md`.
