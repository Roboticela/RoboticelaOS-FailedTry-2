.intel_syntax noprefix

.section .text
.global _start
_start:
    mov rax, 1
    mov rdi, 1
    syscall
.Lhang:
    jmp .Lhang
