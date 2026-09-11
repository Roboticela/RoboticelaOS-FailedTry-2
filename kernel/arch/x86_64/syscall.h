#ifndef ROBO_ARCH_SYSCALL_H
#define ROBO_ARCH_SYSCALL_H

#include "syscall/syscall.h"
#include "syscall/syscall_dispatch.h"
#include "syscall/syscall_numbers.h"
#include "arch/x86_64/syscall/syscall_entry.h"

#define ROBO_SYS_VEC 0x80ull

void robo_syscall_init(void);

#endif
