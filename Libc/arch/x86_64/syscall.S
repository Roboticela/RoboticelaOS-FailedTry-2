.intel_syntax noprefix

/*
 * User ABI wrapper. SysV C:
 *   rdi=number rsi=a1 rdx=a2 rcx=a3 r8=a4 r9=a5  8(rsp)=a6
 * Hardware SYSCALL:
 *   rax=number rdi=a1 rsi=a2 rdx=a3 r10=a4 r8=a5 r9=a6
 */
.section .text
.global robo_syscall
.global Syscall
robo_syscall:
Syscall:
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    mov r10, r8
    mov r8, r9
    mov r9, qword ptr [rsp + 8]
    syscall
    ret
