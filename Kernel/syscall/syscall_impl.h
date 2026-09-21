#ifndef ROBO_SYSCALL_IMPL_H
#define ROBO_SYSCALL_IMPL_H

#include "syscall/syscall_types.h"

uint64_t SysAbiInfo(RoboSyscallContext *ctx);
uint64_t SysExit(RoboSyscallContext *ctx);
uint64_t SysRead(RoboSyscallContext *ctx);
uint64_t SysWrite(RoboSyscallContext *ctx);
uint64_t SysOpen(RoboSyscallContext *ctx);
uint64_t SysClose(RoboSyscallContext *ctx);
uint64_t SysMmap(RoboSyscallContext *ctx);
uint64_t SysMunmap(RoboSyscallContext *ctx);
uint64_t SysProcessCreate(RoboSyscallContext *ctx);
uint64_t SysThreadCreate(RoboSyscallContext *ctx);
uint64_t SysSleep(RoboSyscallContext *ctx);
uint64_t RoboSysTime(RoboSyscallContext *ctx);
uint64_t SysIpc(RoboSyscallContext *ctx);
uint64_t SysDisplay(RoboSyscallContext *ctx);
uint64_t SysSocket(RoboSyscallContext *ctx);
uint64_t SysThread(RoboSyscallContext *ctx);
uint64_t SysSeek(RoboSyscallContext *ctx);
uint64_t SysStat(RoboSyscallContext *ctx);
uint64_t SysMkdir(RoboSyscallContext *ctx);
uint64_t SysRmdir(RoboSyscallContext *ctx);
uint64_t SysUnlink(RoboSyscallContext *ctx);
uint64_t SysRename(RoboSyscallContext *ctx);
uint64_t SysWait(RoboSyscallContext *ctx);
uint64_t SysGetpid(RoboSyscallContext *ctx);
uint64_t SysDriverQuery(RoboSyscallContext *ctx);
uint64_t SysEnosys(RoboSyscallContext *ctx);

#endif
