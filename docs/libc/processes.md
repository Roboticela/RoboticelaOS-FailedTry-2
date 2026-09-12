# Processes

Native API (`robo/process.h`):

* `robo_getpid` / `robo_getppid`
* `robo_process_create(path)` — historical one-argument spawn
* `robo_spawn(path, argv)` — argv copied onto the child stack
* `robo_wait(pid, &status)`
* `robo_exit` → `exit` → `SYS_EXIT` (whole process)

There is no `fork()`, `execve()`, or `waitpid` option soup. The kernel
process model is spawn-from-ELF, not Unix copy-on-write.

`getpid` / `exit` are thin aliases.
