#ifndef ROBO_SYSCALL_TABLE_H
#define ROBO_SYSCALL_TABLE_H

#include "syscall/syscall_types.h"

void SyscallTableInit(void);
RoboSyscallHandler SyscallLookup(uint64_t number);
int SyscallTableComplete(void);
const char *SyscallName(uint64_t number);

#endif
