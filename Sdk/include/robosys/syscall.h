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

Long Syscall(Long number,
                       Long a1, Long a2, Long a3,
                       Long a4, Long a5, Long a6);

static inline Long Exit(int status)
{
    return Syscall((Long)RoboSysExit, (Long)status, 0, 0, 0, 0, 0);
}

static inline Long Read(int fd, void *buf, Ulong len)
{
    return Syscall((Long)RoboSysRead, (Long)fd,
                        (Long)(uintptr_t)buf, (Long)len, 0, 0, 0);
}

static inline Long Write(int fd, const void *buf, Ulong len)
{
    return Syscall((Long)RoboSysWrite, (Long)fd,
                        (Long)(uintptr_t)buf, (Long)len, 0, 0, 0);
}

static inline Long Open(const char *path, int flags, int mode)
{
    return Syscall((Long)RoboSysOpen, (Long)(uintptr_t)path,
                        (Long)flags, (Long)mode, 0, 0, 0);
}

static inline Long Close(int fd)
{
    return Syscall((Long)RoboSysClose, (Long)fd, 0, 0, 0, 0, 0);
}

static inline Long Mmap(void *addr, Ulong len, int prot,
                                  int flags, int fd, Ulong off)
{
    return Syscall((Long)RoboSysMmap, (Long)(uintptr_t)addr,
                        (Long)len, (Long)prot, (Long)flags,
                        (Long)fd, (Long)off);
}

static inline Long Munmap(void *addr, Ulong len)
{
    return Syscall((Long)RoboSysMunmap, (Long)(uintptr_t)addr,
                        (Long)len, 0, 0, 0, 0);
}

static inline Long ProcessCreate(const char *path)
{
    return Syscall((Long)RoboSysProcessCreate,
                        (Long)(uintptr_t)path, 0, 0, 0, 0, 0);
}

static inline Long ProcessSpawn(const char *path, char *const argv[],
                                           int argc)
{
    return Syscall((Long)RoboSysProcessCreate,
                        (Long)(uintptr_t)path,
                        (Long)(uintptr_t)argv, (Long)argc, 0, 0, 0);
}

static inline Long ThreadCreate(void *entry, void *stack, void *arg)
{
    return Syscall((Long)RoboSysThreadCreate,
                        (Long)(uintptr_t)entry, (Long)(uintptr_t)stack,
                        (Long)(uintptr_t)arg, 0, 0, 0);
}

static inline Long RoboSleep(Ulong milliseconds)
{
    return Syscall((Long)RoboSysSleep, (Long)milliseconds,
                        0, 0, 0, 0, 0);
}

static inline uint64_t Time(void)
{
    return (uint64_t)Syscall((Long)RoboSysTime, 0, 0, 0, 0, 0, 0);
}

static inline Long Ipc(int op, int handle, void *buf,
                                 Ulong len, int flags)
{
    return Syscall((Long)RoboSysIpc, (Long)op,
                        (Long)handle, (Long)(uintptr_t)buf,
                        (Long)len, (Long)flags, 0);
}

static inline Long AbiInfo(RoboAbiInfo *out)
{
    return Syscall((Long)RoboSysAbiInfo,
                        (Long)(uintptr_t)out, 0, 0, 0, 0, 0);
}

static inline Long Seek(int fd, Long offset, int whence)
{
    return Syscall((Long)RoboSysSeek, (Long)fd, offset,
                        (Long)whence, 0, 0, 0);
}

static inline Long Stat(const char *path, RoboStat *st)
{
    return Syscall((Long)RoboSysStat, (Long)(uintptr_t)path,
                        (Long)(uintptr_t)st, 0, 0, 0, 0);
}

static inline Long RoboMkdir(const char *path, int mode)
{
    return Syscall((Long)RoboSysMkdir, (Long)(uintptr_t)path,
                        (Long)mode, 0, 0, 0, 0);
}

static inline Long Rmdir(const char *path)
{
    return Syscall((Long)RoboSysRmdir, (Long)(uintptr_t)path,
                        0, 0, 0, 0, 0);
}

static inline Long Unlink(const char *path)
{
    return Syscall((Long)RoboSysUnlink, (Long)(uintptr_t)path,
                        0, 0, 0, 0, 0);
}

static inline Long Rename(const char *oldPath, const char *newPath)
{
    return Syscall((Long)RoboSysRename,
                        (Long)(uintptr_t)oldPath,
                        (Long)(uintptr_t)newPath, 0, 0, 0, 0);
}

static inline Long Wait(Pid pid, unsigned flags, int *status)
{
    return Syscall((Long)RoboSysWait, (Long)pid,
                        (Long)flags, (Long)(uintptr_t)status,
                        0, 0, 0);
}

static inline Pid Getpid(void)
{
    return (Pid)Syscall((Long)RoboSysGetpid,
                                  0, 0, 0, 0, 0, 0);
}

static inline Long DriverQuery(RoboDriverStatus *out)
{
    return Syscall((Long)RoboSysDriverQuery,
                        (Long)(uintptr_t)out, 0, 0, 0, 0, 0);
}

static inline Long Display(int op, int handle, void *buf,
                                     Ulong len, int flags)
{
    return Syscall((Long)RoboSysDisplay, (Long)op,
                        (Long)handle, (Long)(uintptr_t)buf,
                        (Long)len, (Long)flags, 0);
}

static inline Long ThreadCtl(int op, Long a2, Long a3)
{
    return Syscall((Long)RoboSysThread, (Long)op,
                        a2, a3, 0, 0, 0);
}

static inline Long ThreadYield(void)
{
    return ThreadCtl((int)RoboThreadOpYield, 0, 0);
}

static inline Long ThreadExitSelf(int status)
{
    return ThreadCtl((int)RoboThreadOpExit, (Long)status, 0);
}

static inline Long ThreadJoin(Tid tid)
{
    return ThreadCtl((int)RoboThreadOpJoin, (Long)tid, 0);
}

static inline Long ThreadSetFs(void *tcb)
{
    return ThreadCtl((int)RoboThreadOpSetFs,
                           (Long)(uintptr_t)tcb, 0);
}

static inline Tid Gettid(void)
{
    return (Tid)ThreadCtl((int)RoboThreadOpGettid, 0, 0);
}

static inline Pid Getppid(void)
{
    return (Pid)ThreadCtl((int)RoboThreadOpGetppid, 0, 0);
}

#endif
