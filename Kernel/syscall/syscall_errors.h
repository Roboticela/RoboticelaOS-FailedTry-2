#ifndef ROBO_SYSCALL_ERRORS_H
#define ROBO_SYSCALL_ERRORS_H

#include <robosys/syscallErrors.h>
#include <robosys/types.h>

static inline uint64_t SysErr(int code)
{
    if (code <= 0)
        code = RoboEinval;
    return (uint64_t)(-(int64_t)code);
}

#endif
