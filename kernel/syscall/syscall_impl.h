#ifndef ROBO_SYSCALL_IMPL_H
#define ROBO_SYSCALL_IMPL_H

#include "syscall/syscall_types.h"

uint64_t robo_sys_abi_info(RoboSyscallContext *ctx);
uint64_t robo_sys_exit(RoboSyscallContext *ctx);
uint64_t robo_sys_read(RoboSyscallContext *ctx);
uint64_t robo_sys_write(RoboSyscallContext *ctx);
uint64_t robo_sys_open(RoboSyscallContext *ctx);
uint64_t robo_sys_close(RoboSyscallContext *ctx);
uint64_t robo_sys_mmap(RoboSyscallContext *ctx);
uint64_t robo_sys_munmap(RoboSyscallContext *ctx);
uint64_t robo_sys_process_create(RoboSyscallContext *ctx);
uint64_t robo_sys_thread_create(RoboSyscallContext *ctx);
uint64_t robo_sys_sleep(RoboSyscallContext *ctx);
uint64_t robo_sys_time(RoboSyscallContext *ctx);
uint64_t robo_sys_ipc(RoboSyscallContext *ctx);
uint64_t robo_sys_enosys(RoboSyscallContext *ctx);

#endif
