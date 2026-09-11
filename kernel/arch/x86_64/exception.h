#ifndef ROBO_ARCH_EXCEPTION_H
#define ROBO_ARCH_EXCEPTION_H

#include <stdint.h>

#define ROBO_EXC_DE  0u
#define ROBO_EXC_DB  1u
#define ROBO_EXC_BP  3u
#define ROBO_EXC_UD  6u
#define ROBO_EXC_DF  8u
#define ROBO_EXC_GP  13u
#define ROBO_EXC_PF  14u

struct robo_iframe {
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

void robo_exception_dispatch(struct robo_iframe *frame);
unsigned robo_exception_breakpoint_count(void);

#endif
