#ifndef ROBO_SYSCALL_TYPES_H
#define ROBO_SYSCALL_TYPES_H

#include <stdint.h>
#include "arch/x86_64/exception.h"
#include "thread/thread.h"
#include "process/process.h"

typedef struct RoboSyscallContext {
    uint64_t number;
    struct Iframe *frame;
    RoboThread *thread;
    RoboProcess *process;
} RoboSyscallContext;

typedef uint64_t (*RoboSyscallHandler)(RoboSyscallContext *ctx);

#define RoboUserAccessRead  1u
#define RoboUserAccessWrite 2u
#define RoboUserAccessExec  4u

#define RoboSyscallPathMax  111u
#define RoboSyscallWriteMax 4096u

#endif
