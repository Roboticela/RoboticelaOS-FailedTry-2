#ifndef ROBO_SYSCALL_TYPES_H
#define ROBO_SYSCALL_TYPES_H

#include <stdint.h>
#include "arch/x86_64/exception.h"
#include "thread/thread.h"
#include "process/process.h"

typedef struct RoboSyscallContext {
    uint64_t number;
    struct robo_iframe *frame;
    RoboThread *thread;
    RoboProcess *process;
} RoboSyscallContext;

typedef uint64_t (*RoboSyscallHandler)(RoboSyscallContext *ctx);

#define ROBO_USER_ACCESS_READ  1u
#define ROBO_USER_ACCESS_WRITE 2u
#define ROBO_USER_ACCESS_EXEC  4u

#define ROBO_SYSCALL_PATH_MAX  111u
#define ROBO_SYSCALL_WRITE_MAX 4096u

#endif
