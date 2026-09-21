# Processes

Native API (`robo/process.h`):

* `Getpid` / `Getppid`
* `ProcessCreate(path)` — historical one-argument spawn
* `Spawn(path, argv)` — argv copied onto the child stack
* `Wait(pid, &status)`
* `Exit` → `exit` → `SYS_EXIT` (whole process)

There is no `fork()`, `execve()`, or `waitpid` option soup. The kernel
process model is spawn-from-ELF, not Unix copy-on-write.

`getpid` / `exit` are thin aliases.
