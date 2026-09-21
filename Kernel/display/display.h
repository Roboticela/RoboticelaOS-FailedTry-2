#ifndef ROBO_DISPLAY_SYSCALL_H
#define ROBO_DISPLAY_SYSCALL_H

#include <stdint.h>
#include "syscall/syscall_types.h"

uint64_t SysDisplay(RoboSyscallContext *ctx);
void DisplayOnProcessExit(uint64_t pid);

#endif
