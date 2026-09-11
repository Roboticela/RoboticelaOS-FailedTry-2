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

#define ROBO_PF_P     (1ull << 0) /* page present */
#define ROBO_PF_WR    (1ull << 1) /* 1 = write, 0 = read */
#define ROBO_PF_US    (1ull << 2) /* 1 = user, 0 = supervisor */
#define ROBO_PF_RSVD  (1ull << 3) /* reserved bit set in PTE */
#define ROBO_PF_ID    (1ull << 4) /* 1 = instruction fetch */

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

struct robo_iframe *robo_exception_dispatch(struct robo_iframe *frame);
unsigned robo_exception_breakpoint_count(void);
int robo_pf_catch_read(uint64_t va, uint64_t *cr2, uint64_t *rip, uint64_t *err);
uint64_t robo_pf_probe_rip(void);
int robo_pf_selftest(void);

#endif
