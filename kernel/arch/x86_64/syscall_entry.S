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
 * Switch onto the current thread kernel stack (robo_syscall_kstack),
 * build a standard iframe, dispatch, then iretq back to ring 3.
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
 */
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
    sub rsp, 8
    call robo_syscall_dispatch
    jmp robo_x86_resume_iframe
