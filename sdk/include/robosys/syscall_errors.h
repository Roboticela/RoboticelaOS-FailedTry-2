#ifndef ROBOSYS_SYSCALL_ERRORS_H
#define ROBOSYS_SYSCALL_ERRORS_H

/*
 * Roboticela OS x86_64 Syscall ABI v1 — errors.
 *
 * Failure: RAX is the two's-complement negation of one of these codes
 * and always lies in [-4095, -1].
 *
 * Success: any other RAX value (byte counts, fds, pids, tids, mmap
 * addresses, timestamps). User virtual addresses occupy the high
 * canonical window, so they are negative as signed integers; do not
 * treat every negative RAX as an error.
 *
 *   long r = robo_mmap(...);
 *   if ((unsigned long)r >= (unsigned long)-4095ul) {
 *       int err = (int)(-r);
 *   }
 *
 * These are ABI errors, not internal kernel enums.
 */

#define ROBO_EPERM    1
#define ROBO_ENOENT   2
#define ROBO_EIO      3
#define ROBO_EBADF    4
#define ROBO_ENOMEM   5
#define ROBO_EACCES   6
#define ROBO_EFAULT   7
#define ROBO_EBUSY    8
#define ROBO_EXIST    9
#define ROBO_EEXIST   9
#define ROBO_EINVAL   10
#define ROBO_ENOSYS   11
#define ROBO_EINTR    12
#define ROBO_ENOTSUP  13
#define ROBO_EMFILE   14
#define ROBO_ENFILE   15
#define ROBO_ENOTDIR  16
#define ROBO_EISDIR   17
#define ROBO_ENOTEMPTY 18
#define ROBO_EXDEV    19
#define ROBO_ENOSPC   20
#define ROBO_ECHILD   21
#define ROBO_EAGAIN   22
#define ROBO_ENODEV   23
#define ROBO_EBADABI  24
#define ROBO_EBADSIG  25
#define ROBO_EVERIFY  26
#define ROBO_EOVERFLOW 27
#define ROBO_EADDRINUSE 28
#define ROBO_ECONNREFUSED 29
#define ROBO_ECONNRESET 30
#define ROBO_ETIMEDOUT 31
#define ROBO_ENETUNREACH 32
#define ROBO_EHOSTUNREACH 33
#define ROBO_ENOTCONN 34
#define ROBO_EISCONN 35
#define ROBO_EAFNOSUPPORT 36
#define ROBO_EMSGSIZE 37
#define ROBO_EPIPE    38
#define ROBO_ESRCH    39
#define ROBO_EWOULDBLOCK ROBO_EAGAIN

#endif
