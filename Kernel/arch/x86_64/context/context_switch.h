#ifndef ROBO_ARCH_CONTEXT_SWITCH_H
#define ROBO_ARCH_CONTEXT_SWITCH_H

#include "arch/x86_64/context/context.h"

/*
 * X86ContextSwitch(current, next)
 * --------------------------------------
 * Inputs  : RDI = current iframe (already saved by CPU + isrCommon)
 *           RSI = next iframe (on the next thread's kernel stack)
 * Outputs : RAX = next iframe (the frame isrCommon / resume must restore)
 * Modified: RAX (and flags). Callee-saved registers are preserved so C
 *           epilogues in exception_dispatch remain valid.
 * Stack   : stays on the current thread until `X86ResumeIframe`.
 *           Switching RSP inside C would corrupt the dispatch frame.
 * IF      : remains 0 (interrupt gate / cli). Never `sti` here.
 * Return  : returns to the C caller with RAX = next. Does not resume
 *           the next thread itself. Resume is `X86ResumeIframe`.
 *
 * This is not an ordinary C function in spirit: after dispatch returns,
 * execution continues on a different stack via `mov rsp, rax` / `iretq`.
 *
 * X86ResumeIframe
 * ----------------------
 * Entered with RAX = iframe. Loads that stack, pops GPRs, `iretq`.
 * Shared by timer preemption and voluntary `int $48`.
 *
 * New threads
 * -----------
 * RIP = ThreadTrampoline, CS = 0x08, SS = 0x10, RFLAGS.IF = 1.
 * The trampoline `call`s `ThreadBootstrap` so SysV alignment holds
 * (RSP % 16 == 8 at the C function entry).
 */

struct Iframe *X86ContextSwitch(struct Iframe *current,
                                            struct Iframe *next);
void X86ResumeIframe(void);

void ThreadTrampoline(void);
void TaskTrampoline(void);
void RegPreserveLoop(volatile uint64_t *ok, volatile uint64_t *bad);
void RegPreserveIters(volatile uint64_t *ok, volatile uint64_t *bad,
                             uint64_t iterations);

int ContextSelftest(void);

#endif
