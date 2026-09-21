#ifndef ROBO_ARCH_EXCEPTION_H
#define ROBO_ARCH_EXCEPTION_H

#include <stdint.h>

#define RoboExcDe  0u
#define RoboExcDb  1u
#define RoboExcBp  3u
#define RoboExcUd  6u
#define RoboExcDf  8u
#define RoboExcGp  13u
#define RoboExcPf  14u

#define RoboPfP     (1ull << 0) /* page present */
#define RoboPfWr    (1ull << 1) /* 1 = write, 0 = read */
#define RoboPfUs    (1ull << 2) /* 1 = user, 0 = supervisor */
#define RoboPfRsvd  (1ull << 3) /* reserved bit set in PTE */
#define RoboPfId    (1ull << 4) /* 1 = instruction fetch */

struct Iframe {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

struct Iframe *ExceptionDispatch(struct Iframe *frame);
unsigned ExceptionBreakpointCount(void);
unsigned UserFaultCount(void);
int PfCatchRead(uint64_t va, uint64_t *cr2, uint64_t *rip, uint64_t *err);
uint64_t PfProbeRip(void);
int PfSelftest(void);

#endif
