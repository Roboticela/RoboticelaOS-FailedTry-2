#ifndef ROBO_IPC_H
#define ROBO_IPC_H

#include <stdint.h>
#include "syscall/syscall_types.h"
#include "mm/page.h"

uint64_t robo_ipc_syscall(RoboSyscallContext *ctx);
void robo_ipc_on_process_exit(uint64_t pid);
int robo_ipc_shm_owns(phys_addr_t pa);

#endif
