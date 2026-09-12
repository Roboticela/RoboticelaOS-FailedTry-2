#ifndef ROBO_DISPLAY_SYSCALL_H
#define ROBO_DISPLAY_SYSCALL_H

#include <stdint.h>
#include "syscall/syscall_types.h"

uint64_t robo_sys_display(RoboSyscallContext *ctx);
void robo_display_on_process_exit(uint64_t pid);

#endif
