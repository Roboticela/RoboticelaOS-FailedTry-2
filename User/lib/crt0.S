.intel_syntax noprefix

.section .text
.global _start
.extern main
_start:
    xor ebp, ebp
    and rsp, -16
    call main
    mov rdi, rax
    mov rax, 1
    syscall
.Lhang:
    jmp .Lhang
