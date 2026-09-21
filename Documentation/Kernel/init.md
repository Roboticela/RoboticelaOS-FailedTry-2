# Roboticela OS Init Process

`/System/init` is PID **1**, the first user-space process and the root of
the service hierarchy. The kernel process is PID **0**. It provides
primitives (memory, processes, threads, scheduling, filesystems,
syscalls). It is not the permanent user-space manager after init starts.

```text
RoboBootloader → Kernel → RoboFS → ELF loader → /System/init → user space
```

Init is a genuine ring-3 ELF process. It is not a kernel thread and is
not executed inside the kernel. Load path:

```text
VFS → RoboFS → /System/init → ELF parser → user address space → user stack
     → user thread → IRETQ
```

The kernel creates init **once** during normal boot. Failure to load it
is a kernel panic (`INIT STARTUP FAILURE`). Unexpected exit of PID 1 is
a kernel panic (`INIT PROCESS EXITED`).

## Initial-process ABI (v1)

The ELF loader does not place `argc` / `argv` / `envp` on the user
stack. `_start` calls `main` with no arguments. Init identifies itself
as `/System/init`. There is no Unix environment block and no `PATH`.

`SYS_GETPID` returns the calling process id (1 for init).

`SYS_PROCESS_CREATE` takes a RoboFS path and returns the child pid.
Children of init have `parent = PID 1`.

`SYS_WAIT` (22): RDI = pid (0 = any child), RSI = flags
(`RoboWaitNohang` = 1), RDX = optional `int *` status. Returns the
reaped pid, `RoboEagain` if `NOHANG` and nothing is ready, or
`RoboEchild` if there is no matching child. This is the supervision
API. `SYS_IPC` (12) is the process-to-process port/channel ABI used by
RoboWindowServer. There is no second IPC system.

## File descriptors

Init receives fd 0/1/2 as console stdin/stdout/stderr through the normal
FD table. Services started with `SYS_PROCESS_CREATE` receive a **new**
stdio table (fresh console files). The parent table is not cloned.
`RoboFdentCloexec` exists on the clone path; process-create does not
clone, so close-on-exec does not apply yet.

## Services

v1 ships only real programs:

* `/System/services/test-service`
* `/System/services/crash-test`
* `/System/services/isol-test`
* `/System/services/compositor`
* `/System/services/window-server`
* `/bin/window-test` (started by init after the window server)

Future paths (`device-manager`, `network-manager`, `display-service`,
`session-manager`) are reserved. Empty binaries are not installed.

Restart policies: `NEVER`, `ON_FAILURE`, `ALWAYS`. v1 uses `ON_FAILURE`
with a restart limit and linear/exponential backoff for `crash-test`.

`RUNNING` means the process was created. `READY` is reserved for a
future ready notification. Successful `exec` is not treated as service
readiness.

## Orphans

If a parent is destroyed, remaining children are reparented to PID 1
when init exists, otherwise to the kernel process. The parent must
`SYS_WAIT` to reap zombies. Init’s loop keeps calling wait so it can
collect children (including future orphans).

## Shutdown

Service states include `STOPPING`. Hardware reboot is not implemented.
Init does not exit.
