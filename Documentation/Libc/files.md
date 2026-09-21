# Files

```
libc  →  syscall  →  FD  →  VFS  →  RoboFS
```

`open` / `read` / `write` / `close` / `lseek` / `stat` / `mkdir` /
`rmdir` / `unlink` / `rename` wrap the existing syscall ABI. Flags such
as `O_CREAT` match `RoboOCreat`.

`fstat` is not implemented (no dedicated syscall). `isatty` returns true
only for fds 0–2.

`FILE` uses the same descriptors. `gets()` is not provided.
