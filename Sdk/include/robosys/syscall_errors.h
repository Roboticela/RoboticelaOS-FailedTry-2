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
 *   long r = Mmap(...);
 *   if ((unsigned long)r >= (unsigned long)-4095ul) {
 *       int err = (int)(-r);
 *   }
 *
 * These are ABI errors, not internal kernel enums.
 */

#define RoboEperm    1
#define RoboEnoent   2
#define RoboEio      3
#define RoboEbadf    4
#define RoboEnomem   5
#define RoboEacces   6
#define RoboEfault   7
#define RoboEbusy    8
#define RoboExist    9
#define RoboEexist   9
#define RoboEinval   10
#define RoboEnosys   11
#define RoboEintr    12
#define RoboEnotsup  13
#define RoboEmfile   14
#define RoboEnfile   15
#define RoboEnotdir  16
#define RoboEisdir   17
#define RoboEnotempty 18
#define RoboExdev    19
#define RoboEnospc   20
#define RoboEchild   21
#define RoboEagain   22
#define RoboEnodev   23
#define RoboEbadabi  24
#define RoboEbadsig  25
#define RoboEverify  26
#define RoboEoverflow 27
#define RoboEaddrinuse 28
#define RoboEconnrefused 29
#define RoboEconnreset 30
#define RoboEtimedout 31
#define RoboEnetunreach 32
#define RoboEhostunreach 33
#define RoboEnotconn 34
#define RoboEisconn 35
#define RoboEafnosupport 36
#define RoboEmsgsize 37
#define RoboEpipe    38
#define RoboEsrch    39
#define RoboEwouldblock RoboEagain

#endif
