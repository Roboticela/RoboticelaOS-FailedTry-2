#ifndef ROBOSYS_SYSCALL_H
#define ROBOSYS_SYSCALL_H

#include "syscall_numbers.h"
#include "syscall_errors.h"
#include "types.h"

/*
 * User-side client for Roboticela OS x86_64 Syscall ABI v1.
 *
 *   RAX = number
 *   RDI RSI RDX R10 R8 R9 = arg1..arg6
 *   syscall
 *   RAX = result
 *
 * RCX and R11 are clobbered by the hardware SYSCALL/SYSRET pair.
 * RBX, RBP, R12–R15 are preserved.
 *
 * Errors occupy RAX in [-4095, -1]. Other values are success, including
 * high canonical mmap addresses.
 */

robo_long robo_syscall(robo_long number,
                       robo_long a1, robo_long a2, robo_long a3,
                       robo_long a4, robo_long a5, robo_long a6);

static inline robo_long robo_exit(int status)
{
    return robo_syscall((robo_long)ROBO_SYS_EXIT, (robo_long)status, 0, 0, 0, 0, 0);
}

static inline robo_long robo_read(int fd, void *buf, robo_ulong len)
{
    return robo_syscall((robo_long)ROBO_SYS_READ, (robo_long)fd,
                        (robo_long)(uintptr_t)buf, (robo_long)len, 0, 0, 0);
}

static inline robo_long robo_write(int fd, const void *buf, robo_ulong len)
{
    return robo_syscall((robo_long)ROBO_SYS_WRITE, (robo_long)fd,
                        (robo_long)(uintptr_t)buf, (robo_long)len, 0, 0, 0);
}

static inline robo_long robo_open(const char *path, int flags, int mode)
{
    return robo_syscall((robo_long)ROBO_SYS_OPEN, (robo_long)(uintptr_t)path,
                        (robo_long)flags, (robo_long)mode, 0, 0, 0);
}

static inline robo_long robo_close(int fd)
{
    return robo_syscall((robo_long)ROBO_SYS_CLOSE, (robo_long)fd, 0, 0, 0, 0, 0);
}

static inline robo_long robo_mmap(void *addr, robo_ulong len, int prot,
                                  int flags, int fd, robo_ulong off)
{
    return robo_syscall((robo_long)ROBO_SYS_MMAP, (robo_long)(uintptr_t)addr,
                        (robo_long)len, (robo_long)prot, (robo_long)flags,
                        (robo_long)fd, (robo_long)off);
}

static inline robo_long robo_munmap(void *addr, robo_ulong len)
{
    return robo_syscall((robo_long)ROBO_SYS_MUNMAP, (robo_long)(uintptr_t)addr,
                        (robo_long)len, 0, 0, 0, 0);
}

static inline robo_long robo_process_create(const char *path)
{
    return robo_syscall((robo_long)ROBO_SYS_PROCESS_CREATE,
                        (robo_long)(uintptr_t)path, 0, 0, 0, 0, 0);
}

static inline robo_long robo_thread_create(void *entry, void *stack, void *arg)
{
    return robo_syscall((robo_long)ROBO_SYS_THREAD_CREATE,
                        (robo_long)(uintptr_t)entry, (robo_long)(uintptr_t)stack,
                        (robo_long)(uintptr_t)arg, 0, 0, 0);
}

static inline robo_long robo_sleep(robo_ulong milliseconds)
{
    return robo_syscall((robo_long)ROBO_SYS_SLEEP, (robo_long)milliseconds,
                        0, 0, 0, 0, 0);
}

static inline uint64_t robo_time(void)
{
    return (uint64_t)robo_syscall((robo_long)ROBO_SYS_TIME, 0, 0, 0, 0, 0, 0);
}

static inline robo_long robo_ipc(int op, int handle, void *buf,
                                 robo_ulong len, int flags)
{
    return robo_syscall((robo_long)ROBO_SYS_IPC, (robo_long)op,
                        (robo_long)handle, (robo_long)(uintptr_t)buf,
                        (robo_long)len, (robo_long)flags, 0);
}

static inline robo_long robo_abi_info(RoboAbiInfo *out)
{
    return robo_syscall((robo_long)ROBO_SYS_ABI_INFO,
                        (robo_long)(uintptr_t)out, 0, 0, 0, 0, 0);
}

static inline robo_long robo_seek(int fd, robo_long offset, int whence)
{
    return robo_syscall((robo_long)ROBO_SYS_SEEK, (robo_long)fd, offset,
                        (robo_long)whence, 0, 0, 0);
}

static inline robo_long robo_stat(const char *path, RoboStat *st)
{
    return robo_syscall((robo_long)ROBO_SYS_STAT, (robo_long)(uintptr_t)path,
                        (robo_long)(uintptr_t)st, 0, 0, 0, 0);
}

static inline robo_long robo_mkdir(const char *path, int mode)
{
    return robo_syscall((robo_long)ROBO_SYS_MKDIR, (robo_long)(uintptr_t)path,
                        (robo_long)mode, 0, 0, 0, 0);
}

static inline robo_long robo_rmdir(const char *path)
{
    return robo_syscall((robo_long)ROBO_SYS_RMDIR, (robo_long)(uintptr_t)path,
                        0, 0, 0, 0, 0);
}

static inline robo_long robo_unlink(const char *path)
{
    return robo_syscall((robo_long)ROBO_SYS_UNLINK, (robo_long)(uintptr_t)path,
                        0, 0, 0, 0, 0);
}

static inline robo_long robo_rename(const char *old_path, const char *new_path)
{
    return robo_syscall((robo_long)ROBO_SYS_RENAME,
                        (robo_long)(uintptr_t)old_path,
                        (robo_long)(uintptr_t)new_path, 0, 0, 0, 0);
}

static inline robo_long robo_wait(robo_pid pid, unsigned flags, int *status)
{
    return robo_syscall((robo_long)ROBO_SYS_WAIT, (robo_long)pid,
                        (robo_long)flags, (robo_long)(uintptr_t)status,
                        0, 0, 0);
}

static inline robo_pid robo_getpid(void)
{
    return (robo_pid)robo_syscall((robo_long)ROBO_SYS_GETPID,
                                  0, 0, 0, 0, 0, 0);
}

#endif
