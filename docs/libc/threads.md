# Threads

Native API (`robo/thread.h`):

* `robo_thread_create` / `robo_thread_exit` / `robo_thread_join` /
  `robo_thread_yield`
* mutex, condition variable, semaphore (user-space spinning + yield)

This is not pthreads. Condition variables are a yield-based wait, not a
kernel futex.

`SYS_THREAD_CREATE` still exists for raw entry/stack/arg. libc allocates
a stack and TCB with `mmap`, then installs FS in a trampoline before
calling the user function.

`errno` is per-thread. Do not share libc mutable globals across threads
without a mutex.
