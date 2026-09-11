#ifndef ROBO_SYSCALL_ERRORS_H
#define ROBO_SYSCALL_ERRORS_H

#include <robosys/syscall_errors.h>
#include <robosys/types.h>

static inline uint64_t robo_sys_err(int code)
{
    if (code <= 0)
        code = ROBO_EINVAL;
    return (uint64_t)(-(int64_t)code);
}

#endif
