#ifndef ROBO_ARCH_CONTEXT_H
#define ROBO_ARCH_CONTEXT_H

#include <stddef.h>
#include <stdint.h>
#include "arch/x86_64/exception.h"
#include "arch/x86_64/gdt.h"

/*
 * Phase 10 — x86_64 CPU context
 * =============================
 *
 * One context format: the Phase 5 interrupt frame (`struct robo_iframe`).
 * There is no second register-save convention. `cpu_context_t` is that frame.
 *
 * Ownership
 * ---------
 *   CPU (same-privilege interrupt / `int $48`) pushes:
 *     RIP, CS, RFLAGS, RSP, SS
 *
 *   `isr_common` then pushes GPRs (Intel `push` order, RAX first):
 *     RAX, RBX, RCX, RDX, RSI, RDI, RBP, R8–R15
 *   plus the software vector and error-code slots already on the stack.
 *
 *   `robo_x86_resume_iframe` pops GPRs, skips vector+error, `iretq`.
 *   `iretq` restores RIP, CS, RFLAGS, RSP, SS.
 *
 *   The context-switch routine does not invent another frame. It validates
 *   the next iframe and returns it so the resume path can switch RSP.
 *
 * Stack layout (low address = current RSP at iframe pointer)
 * ----------------------------------------------------------
 *
 *   Higher addresses
 *   ────────────────
 *   saved SS
 *   saved RSP
 *   saved RFLAGS
 *   saved CS
 *   saved RIP
 *   error code          (0 if the vector has no hardware error)
 *   vector
 *   RAX
 *   RBX
 *   RCX
 *   RDX
 *   RSI
 *   RDI
 *   RBP
 *   R8
 *   R9
 *   R10
 *   R11
 *   R12
 *   R13
 *   R14
 *   R15                 ← iframe pointer / RSP at `isr_common` after saves
 *   ────────────────
 *   Lower addresses
 *
 * Paths
 * -----
 *   Timer preemption : IRQ 32 → PIC EOI → scheduler_tick → resume → iretq
 *   Voluntary yield  : `int $48` → scheduler_resched → same resume → iretq
 *   New thread       : synthetic iframe; RIP = trampoline; first iretq only
 *   Termination      : thread marked TERMINATED, `int $48`, never restored
 *
 * Segments
 * --------
 *   Kernel CS = 0x08, SS = 0x10. DS/ES are loaded once at GDT reload.
 *   FS/GS are zeroed at GDT reload and are not used for TLS. They are
 *   not switched. CR3 is not part of this frame; kernel threads share
 *   one address space (see RoboThread.address_space).
 *
 * RFLAGS / IF
 * -----------
 *   Interrupt gates run with IF=0. The selected thread's IF is restored
 *   by `iretq` from its saved RFLAGS. No `sti` in the switch path.
 *
 * FPU / SSE / AVX
 * ---------------
 *   Not saved. Kernel threads must not rely on SIMD state surviving a
 *   preemption. Future user mode needs XSAVE.
 *
 * TSS
 * ---
 *   Independent of this frame. RSP0 is updated to the next kernel stack
 *   top. IST1 (double-fault) is never modified by context switching.
 */

typedef struct robo_iframe cpu_context_t;
typedef struct robo_iframe robo_cpu_context_t;

#define ROBO_IFRAME_OFF_R15     0u
#define ROBO_IFRAME_OFF_R14     8u
#define ROBO_IFRAME_OFF_R13     16u
#define ROBO_IFRAME_OFF_R12     24u
#define ROBO_IFRAME_OFF_R11     32u
#define ROBO_IFRAME_OFF_R10     40u
#define ROBO_IFRAME_OFF_R9      48u
#define ROBO_IFRAME_OFF_R8      56u
#define ROBO_IFRAME_OFF_RBP     64u
#define ROBO_IFRAME_OFF_RDI     72u
#define ROBO_IFRAME_OFF_RSI     80u
#define ROBO_IFRAME_OFF_RDX     88u
#define ROBO_IFRAME_OFF_RCX     96u
#define ROBO_IFRAME_OFF_RBX     104u
#define ROBO_IFRAME_OFF_RAX     112u
#define ROBO_IFRAME_OFF_VECTOR  120u
#define ROBO_IFRAME_OFF_ERROR   128u
#define ROBO_IFRAME_OFF_RIP     136u
#define ROBO_IFRAME_OFF_CS      144u
#define ROBO_IFRAME_OFF_RFLAGS  152u
#define ROBO_IFRAME_OFF_RSP     160u
#define ROBO_IFRAME_OFF_SS      168u
#define ROBO_IFRAME_SIZE        176u

#define ROBO_RFLAGS_IF          (1ull << 9)

_Static_assert(offsetof(struct robo_iframe, r15) == ROBO_IFRAME_OFF_R15, "r15");
_Static_assert(offsetof(struct robo_iframe, r14) == ROBO_IFRAME_OFF_R14, "r14");
_Static_assert(offsetof(struct robo_iframe, r13) == ROBO_IFRAME_OFF_R13, "r13");
_Static_assert(offsetof(struct robo_iframe, r12) == ROBO_IFRAME_OFF_R12, "r12");
_Static_assert(offsetof(struct robo_iframe, r11) == ROBO_IFRAME_OFF_R11, "r11");
_Static_assert(offsetof(struct robo_iframe, r10) == ROBO_IFRAME_OFF_R10, "r10");
_Static_assert(offsetof(struct robo_iframe, r9) == ROBO_IFRAME_OFF_R9, "r9");
_Static_assert(offsetof(struct robo_iframe, r8) == ROBO_IFRAME_OFF_R8, "r8");
_Static_assert(offsetof(struct robo_iframe, rbp) == ROBO_IFRAME_OFF_RBP, "rbp");
_Static_assert(offsetof(struct robo_iframe, rdi) == ROBO_IFRAME_OFF_RDI, "rdi");
_Static_assert(offsetof(struct robo_iframe, rsi) == ROBO_IFRAME_OFF_RSI, "rsi");
_Static_assert(offsetof(struct robo_iframe, rdx) == ROBO_IFRAME_OFF_RDX, "rdx");
_Static_assert(offsetof(struct robo_iframe, rcx) == ROBO_IFRAME_OFF_RCX, "rcx");
_Static_assert(offsetof(struct robo_iframe, rbx) == ROBO_IFRAME_OFF_RBX, "rbx");
_Static_assert(offsetof(struct robo_iframe, rax) == ROBO_IFRAME_OFF_RAX, "rax");
_Static_assert(offsetof(struct robo_iframe, vector) == ROBO_IFRAME_OFF_VECTOR, "vec");
_Static_assert(offsetof(struct robo_iframe, error) == ROBO_IFRAME_OFF_ERROR, "err");
_Static_assert(offsetof(struct robo_iframe, rip) == ROBO_IFRAME_OFF_RIP, "rip");
_Static_assert(offsetof(struct robo_iframe, cs) == ROBO_IFRAME_OFF_CS, "cs");
_Static_assert(offsetof(struct robo_iframe, rflags) == ROBO_IFRAME_OFF_RFLAGS, "rf");
_Static_assert(offsetof(struct robo_iframe, rsp) == ROBO_IFRAME_OFF_RSP, "rsp");
_Static_assert(offsetof(struct robo_iframe, ss) == ROBO_IFRAME_OFF_SS, "ss");
_Static_assert(sizeof(struct robo_iframe) == ROBO_IFRAME_SIZE, "iframe size");

int robo_x86_iframe_valid(const struct robo_iframe *frame);
void robo_x86_context_fail_frames(struct robo_iframe *current,
                                  struct robo_iframe *next);
void robo_x86_context_fail(struct robo_iframe *current,
                           struct robo_iframe *next,
                           uint64_t cur_id, uint64_t next_id,
                           const char *cur_state, const char *next_state);

void robo_x86_context_report(void);
#ifndef CONTEXT_HOST_TEST
void robo_x86_context_fb_report(void);
#endif

#endif
