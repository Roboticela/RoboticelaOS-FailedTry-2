#ifndef ROBO_SYSCALL_H
#define ROBO_SYSCALL_H

#include <stdint.h>
#include "roboticela/bootinfo.h"
#include "arch/x86_64/exception.h"

void SyscallSubsystemInit(void);
void SyscallSetTrace(int on);
int SyscallTrace(void);
uint64_t SyscallCount(void);
uint64_t SysretCount(void);
unsigned SyscallSlept(void);
int UserCplSeen(void);
unsigned UserTimerIrqs(void);
void UserNoteIframe(const struct Iframe *frame);
int HelloFromUser(void);
int WritePtrRejected(void);
int SysSawEnosys(void);
int SysSawEfault(void);
int SysSawEbadf(void);
int SysIsoA(void);
int SysIsoB(void);
int SyscallSelftest(const RoboBootInfo *info);
#ifndef SYSCALL_HOST_TEST
void SyscallFbReport(void);
#endif

#endif
