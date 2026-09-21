#ifndef ROBO_IPC_H
#define ROBO_IPC_H

#include <stdint.h>
#include "syscall/syscall_types.h"
#include "mm/page.h"

uint64_t IpcSyscall(RoboSyscallContext *ctx);
void IpcOnProcessExit(uint64_t pid);
int IpcShmOwns(PhysAddr pa);

#endif
