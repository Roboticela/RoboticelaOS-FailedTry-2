# Implementation status

Roboticela libc is **not POSIX-complete**.

## Implemented

| Area | Coverage |
|------|----------|
| stdint / stddef / stdbool / stdarg / limits | x86_64 types |
| string | memcpy, memmove, memset, memcmp, strlen, strnlen, strcpy, strncpy, strcmp, strncmp, strchr, strrchr, strstr, strcat, strncat |
| stdlib | malloc family, abort/exit/_Exit, atoi/strtol/strtoul, qsort/bsearch, abs/labs, getenv/setenv/unsetenv |
| stdio | FILE, stdin/out/err, printf family, puts/fputs/putchar, fopen/fclose/fread/fwrite |
| errno | per-thread, ABI error codes |
| time | time, clock, clock_gettime, nanosleep, sleep, usleep |
| files | open/close/read/write/lseek/stat/mkdir/rmdir/unlink/rename |
| process | getpid/getppid/spawn/wait/exit |
| thread | create/join/exit/yield, mutex/cond/sem, FS TLS |
| sockets | TCP/UDP via SYS_SOCKET |
| CRT | crt0, crti, crtn, libc.a |

## Time limitation

The kernel `SYS_TIME` is monotonic nanoseconds since boot. `CLOCK_REALTIME`
and `time()` currently report that same clock. They are **not** wall-clock
UTC.

## Not implemented

fork, execve, signals, pthreads, locales, iconv, epoll, io_uring,
dynamic linker, `gets`, `fstat`, full printf flags/length modifiers,
C++ runtime.

## Chromium

Foundation only: threads, TLS, mutex, files, sockets, mmap, processes,
time, environment. Not a Chromium platform layer.
