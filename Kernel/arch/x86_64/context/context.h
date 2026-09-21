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
 * One context format: the Phase 5 interrupt frame (`struct Iframe`).
 * There is no second register-save convention. `CpuContext` is that frame.
 *
 * Ownership
 * ---------
 *   CPU (same-privilege interrupt / `int $48`) pushes:
 *     RIP, CS, RFLAGS, RSP, SS
 *
 *   `isrCommon` then pushes GPRs (Intel `push` order, RAX first):
 *     RAX, RBX, RCX, RDX, RSI, RDI, RBP, R8–R15
 *   plus the software vector and error-code slots already on the stack.
 *
 *   `X86ResumeIframe` pops GPRs, skips vector+error, `iretq`.
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
 *   R15                 ← iframe pointer / RSP at `isrCommon` after saves
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
 *   one address space (see RoboThread.addressSpace).
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

typedef struct Iframe CpuContext;
typedef struct Iframe CpuContext;

#define RoboIframeOffR15     0u
#define RoboIframeOffR14     8u
#define RoboIframeOffR13     16u
#define RoboIframeOffR12     24u
#define RoboIframeOffR11     32u
#define RoboIframeOffR10     40u
#define RoboIframeOffR9      48u
#define RoboIframeOffR8      56u
#define RoboIframeOffRbp     64u
#define RoboIframeOffRdi     72u
#define RoboIframeOffRsi     80u
#define RoboIframeOffRdx     88u
#define RoboIframeOffRcx     96u
#define RoboIframeOffRbx     104u
#define RoboIframeOffRax     112u
#define RoboIframeOffVector  120u
#define RoboIframeOffError   128u
#define RoboIframeOffRip     136u
#define RoboIframeOffCs      144u
#define RoboIframeOffRflags  152u
#define RoboIframeOffRsp     160u
#define RoboIframeOffSs      168u
#define RoboIframeSize        176u

#define RoboRflagsIf          (1ull << 9)

_Static_assert(offsetof(struct Iframe, r15) == RoboIframeOffR15, "r15");
_Static_assert(offsetof(struct Iframe, r14) == RoboIframeOffR14, "r14");
_Static_assert(offsetof(struct Iframe, r13) == RoboIframeOffR13, "r13");
_Static_assert(offsetof(struct Iframe, r12) == RoboIframeOffR12, "r12");
_Static_assert(offsetof(struct Iframe, r11) == RoboIframeOffR11, "r11");
_Static_assert(offsetof(struct Iframe, r10) == RoboIframeOffR10, "r10");
_Static_assert(offsetof(struct Iframe, r9) == RoboIframeOffR9, "r9");
_Static_assert(offsetof(struct Iframe, r8) == RoboIframeOffR8, "r8");
_Static_assert(offsetof(struct Iframe, rbp) == RoboIframeOffRbp, "rbp");
_Static_assert(offsetof(struct Iframe, rdi) == RoboIframeOffRdi, "rdi");
_Static_assert(offsetof(struct Iframe, rsi) == RoboIframeOffRsi, "rsi");
_Static_assert(offsetof(struct Iframe, rdx) == RoboIframeOffRdx, "rdx");
_Static_assert(offsetof(struct Iframe, rcx) == RoboIframeOffRcx, "rcx");
_Static_assert(offsetof(struct Iframe, rbx) == RoboIframeOffRbx, "rbx");
_Static_assert(offsetof(struct Iframe, rax) == RoboIframeOffRax, "rax");
_Static_assert(offsetof(struct Iframe, vector) == RoboIframeOffVector, "vec");
_Static_assert(offsetof(struct Iframe, error) == RoboIframeOffError, "err");
_Static_assert(offsetof(struct Iframe, rip) == RoboIframeOffRip, "rip");
_Static_assert(offsetof(struct Iframe, cs) == RoboIframeOffCs, "cs");
_Static_assert(offsetof(struct Iframe, rflags) == RoboIframeOffRflags, "rf");
_Static_assert(offsetof(struct Iframe, rsp) == RoboIframeOffRsp, "rsp");
_Static_assert(offsetof(struct Iframe, ss) == RoboIframeOffSs, "ss");
_Static_assert(sizeof(struct Iframe) == RoboIframeSize, "iframe size");

int X86IframeValid(const struct Iframe *frame);
void X86ContextFailFrames(struct Iframe *current,
                                  struct Iframe *next);
void X86ContextFail(struct Iframe *current,
                           struct Iframe *next,
                           uint64_t curId, uint64_t nextId,
                           const char *curState, const char *nextState);

void X86ContextReport(void);
#ifndef CONTEXT_HOST_TEST
void X86ContextFbReport(void);
#endif

#endif
