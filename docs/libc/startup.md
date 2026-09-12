# Process startup

```
Kernel ELF loader
    ↓
SysV stack (argc, argv[], NULL, envp NULL, AT_NULL, 0)
    ↓
_start (crt0.S)
    ↓
robo_libc_start
    ↓
TLS + heap + stdio + environ
    ↓
main(argc, argv, envp)
    ↓
exit(status) → SYS_EXIT
```

`RSP` is 16-byte aligned at `_start`. `int main(void)` is valid; extra
argument registers are ignored.

`SYS_PROCESS_CREATE` with argc 0 plants `argv[0]` as the ELF basename.
Non-zero argc copies up to eight user strings.

There is no `fork()`. Spawn a new process with `robo_spawn(path, argv)`.
