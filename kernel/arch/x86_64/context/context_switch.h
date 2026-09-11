#ifndef ROBO_ARCH_CONTEXT_SWITCH_H
#define ROBO_ARCH_CONTEXT_SWITCH_H

#include "arch/x86_64/context/context.h"

/*
 * robo_x86_context_switch(current, next)
 * --------------------------------------
 * Inputs  : RDI = current iframe (already saved by CPU + isr_common)
 *           RSI = next iframe (on the next thread's kernel stack)
 * Outputs : RAX = next iframe (the frame isr_common / resume must restore)
 * Modified: RAX (and flags). Callee-saved registers are preserved so C
 *           epilogues in exception_dispatch remain valid.
 * Stack   : stays on the current thread until `robo_x86_resume_iframe`.
 *           Switching RSP inside C would corrupt the dispatch frame.
 * IF      : remains 0 (interrupt gate / cli). Never `sti` here.
 * Return  : returns to the C caller with RAX = next. Does not resume
 *           the next thread itself. Resume is `robo_x86_resume_iframe`.
 *
 * This is not an ordinary C function in spirit: after dispatch returns,
 * execution continues on a different stack via `mov rsp, rax` / `iretq`.
 *
 * robo_x86_resume_iframe
 * ----------------------
 * Entered with RAX = iframe. Loads that stack, pops GPRs, `iretq`.
 * Shared by timer preemption and voluntary `int $48`.
 *
 * New threads
 * -----------
 * RIP = robo_thread_trampoline, CS = 0x08, SS = 0x10, RFLAGS.IF = 1.
 * The trampoline `call`s `robo_thread_bootstrap` so SysV alignment holds
 * (RSP % 16 == 8 at the C function entry).
 */

struct robo_iframe *robo_x86_context_switch(struct robo_iframe *current,
                                            struct robo_iframe *next);
void robo_x86_resume_iframe(void);

void robo_thread_trampoline(void);
void robo_task_trampoline(void);
void robo_reg_preserve_loop(volatile uint64_t *ok, volatile uint64_t *bad);
void robo_reg_preserve_iters(volatile uint64_t *ok, volatile uint64_t *bad,
                             uint64_t iterations);

int robo_context_selftest(void);

#endif
