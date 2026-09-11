#ifndef ROBO_ARCH_SYSCALL_ENTRY_H
#define ROBO_ARCH_SYSCALL_ENTRY_H

#include <stdint.h>

/*
 * x86_64 SYSCALL/SYSRET vs IRETQ
 * ------------------------------
 * IRETQ  — privilege-level process/thread entry and interrupt return.
 *          The ELF loader uses iretq to enter ring 3 the first time.
 *          Timer/yield/exit reschedule also resume with iretq.
 *
 * SYSCALL/SYSRET — fast user/kernel syscall transition.
 *   SYSCALL: RCX = user RIP, R11 = user RFLAGS, RSP still user.
 *   The entry stub switches onto the current thread kernel stack
 *   (robo_syscall_kstack, kept in lockstep with TSS.RSP0), builds a
 *   standard iframe, then runs the generic dispatcher.
 *   SYSRETQ returns to ring 3 when the iframe still describes the
 *   calling user thread. A different next thread (exit, sleep wake
 *   onto someone else) uses iretq via robo_x86_resume_iframe.
 *
 * STAR:  [47:32] = kernel CS 0x08, [63:48] = 0x10
 *        SYSCALL CS=0x08 SS=0x10
 *        SYSRET  CS=0x20 SS=0x18 (CPU applies RPL 3)
 *
 * FMASK: IF is cleared for the stack switch, then re-enabled in C so
 * long syscalls stay preemptible. Return path clears IF again before
 * SYSRET/IRET.
 */

extern uint64_t robo_syscall_kstack;

void robo_syscall_entry(void);
void robo_syscall_cpu_init(void);

#endif
