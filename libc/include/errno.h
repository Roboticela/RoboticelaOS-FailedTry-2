#ifndef ROBO_LIBC_ERRNO_H
#define ROBO_LIBC_ERRNO_H

/*
 * Numeric values match the Roboticela syscall ABI (sdk/include/robosys
 * syscall_errors.h). errno is per-thread via FS-based TLS.
 */

#define EPERM        1
#define ENOENT       2
#define EIO          3
#define EBADF        4
#define ENOMEM       5
#define EACCES       6
#define EFAULT       7
#define EBUSY        8
#define EEXIST       9
#define EINVAL       10
#define ENOSYS       11
#define EINTR        12
#define ENOTSUP      13
#define EMFILE       14
#define ENFILE       15
#define ENOTDIR      16
#define EISDIR       17
#define ENOTEMPTY    18
#define EXDEV        19
#define ENOSPC       20
#define ECHILD       21
#define EAGAIN       22
#define ENODEV       23
#define EOVERFLOW    27
#define EADDRINUSE   28
#define ECONNREFUSED 29
#define ECONNRESET   30
#define ETIMEDOUT    31
#define ENETUNREACH  32
#define EHOSTUNREACH 33
#define ENOTCONN     34
#define EISCONN      35
#define EAFNOSUPPORT 36
#define EMSGSIZE     37
#define EPIPE        38
#define ESRCH        39
#define ERANGE       EOVERFLOW
#define EWOULDBLOCK  EAGAIN

int *__errno_location(void);
#define errno (*__errno_location())

#endif
