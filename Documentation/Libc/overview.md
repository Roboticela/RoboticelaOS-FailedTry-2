# Roboticela libc

Roboticela libc is the native user-space C library. It is **not
POSIX-complete** and is not a port of glibc or musl.

```
Application
    ↓
Roboticela libc
    ↓
syscall wrappers
    ↓
Kernel
```

User programs link `libc.a` plus the CRT (`crt0.o`). They must not link
the host libc.

Public headers live in `libc/include`. Native OS APIs live in
`libc/include/robo/`. Kernel internals are never included from public
headers.
