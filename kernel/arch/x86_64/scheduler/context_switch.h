#ifndef ROBO_ARCH_CONTEXT_SWITCH_H
#define ROBO_ARCH_CONTEXT_SWITCH_H

#include "arch/x86_64/exception.h"

/*
 * x86_64 context switch (Phase 8)
 * -------------------------------
 * Task context IS the Phase 5 interrupt frame (`struct robo_iframe`).
 * There is no second register-save convention.
 *
 * Hardware (privilege-same interrupt / int $48) pushes:
 *   RIP, CS, RFLAGS, RSP, SS
 *
 * isr_common then saves GPRs (Intel push order):
 *   RAX RBX RCX RDX RSI RDI RBP R8-R15
 * plus dummy error + vector.
 *
 * SysV ABI (C helpers):
 *   Caller-saved: RAX RCX RDX RSI RDI R8-R11
 *   Callee-saved: RBX RBP R12-R15
 * Interrupt entry already saved both sets, so C may clobber caller-saved
 * registers; callee-saved are restored by the C epilogue before we reload
 * the iframe.
 *
 * iretq restores RIP, CS, RFLAGS, RSP, SS.
 *
 * Switching:
 *   exception_dispatch returns the next task's iframe pointer in RAX.
 *   isr_common does `mov rsp, rax` then the normal pop / iretq path.
 *   That iretq resumes the selected task. No extra iretq.
 *
 * Voluntary yield / sleep / exit:
 *   `int $48` (ROBO_VEC_RESCHED) uses the same stub and iframe layout.
 *   It is not a PIC IRQ; no EOI.
 *
 * New tasks:
 *   A synthetic iframe is built on the new kernel stack with
 *   RIP = robo_task_trampoline, CS = 0x08, SS = 0x10, RFLAGS.IF = 1.
 *   The trampoline `call`s the C bootstrap so SysV stack alignment holds
 *   (RSP % 16 == 8 at the C function entry).
 */

void robo_task_trampoline(void);
void robo_reg_preserve_loop(volatile uint64_t *ok, volatile uint64_t *bad);

#endif
