.intel_syntax noprefix

.section .bss
.align 8
.global robo_syscall_kstack
robo_syscall_kstack:
    .quad 0
robo_user_rsp:
    .quad 0

.section .text
.global robo_syscall_entry
.extern robo_syscall_dispatch
.extern robo_x86_resume_iframe

/*
 * SYSCALL: RCX = user RIP, R11 = user RFLAGS, RSP still user.
 * FMASK cleared IF. Switch onto the current thread kernel stack
 * (robo_syscall_kstack, kept in lockstep with TSS.RSP0), build a
 * standard iframe, dispatch, then SYSRETQ to the same user thread
 * or IRETQ when dispatch selected a different iframe.
 *
 *   Higher addresses
 *   SS (0x1B)
 *   RSP (user)
 *   RFLAGS (R11)
 *   CS (0x23)
 *   RIP (RCX)
 *   error 0
 *   vector 0x80
 *   GPRs (same order as isr_common)
 *   Lower addresses  ← iframe / RSP
 *
 * Hardware clobbers RCX and R11. User RCX/R11 are not preserved.
 * RBX, RBP, R12–R15 are restored from the iframe.
 */
.equ IFRAME_CS, 144

robo_syscall_entry:
    mov [rip + robo_user_rsp], rsp
    mov rsp, [rip + robo_syscall_kstack]
    push 0x1B
    push qword ptr [rip + robo_user_rsp]
    push r11
    push 0x23
    push rcx
    push 0
    push 0x80
    push rax
    mov eax, 0x10
    mov ss, ax
    mov ds, ax
    mov es, ax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    cld
    mov rdi, rsp
    and rsp, -16
    sub rsp, 8
    call robo_syscall_dispatch
    cli
    cmp qword ptr [rax + IFRAME_CS], 0x23
    jne robo_x86_resume_iframe
    mov rsp, rax
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    mov eax, 0x1B
    mov ds, ax
    mov es, ax
    pop rax
    add rsp, 16
    pop rcx
    add rsp, 8
    pop r11
    and r11, 0xCD5
    or r11, 0x202
    pop rsp
    sysretq
