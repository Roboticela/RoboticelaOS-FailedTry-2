#ifndef ROBO_SYSCALL_DISPATCH_H
#define ROBO_SYSCALL_DISPATCH_H

#include "arch/x86_64/exception.h"

struct robo_iframe *robo_syscall_dispatch(struct robo_iframe *frame);

#endif
