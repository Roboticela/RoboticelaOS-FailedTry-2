#ifndef ROBO_SYSCALL_TABLE_H
#define ROBO_SYSCALL_TABLE_H

#include "syscall/syscall_types.h"

void robo_syscall_table_init(void);
RoboSyscallHandler robo_syscall_lookup(uint64_t number);
int robo_syscall_table_complete(void);
const char *robo_syscall_name(uint64_t number);

#endif
