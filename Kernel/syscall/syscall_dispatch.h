#ifndef ROBO_SYSCALL_DISPATCH_H
#define ROBO_SYSCALL_DISPATCH_H

#include "arch/x86_64/exception.h"

struct Iframe *SyscallDispatch(struct Iframe *frame);

#endif
