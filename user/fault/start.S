.intel_syntax noprefix

.section .text
.global _start
_start:
    mov rax, 0xFFFF808001000000
    mov al, byte ptr [rax]
    mov rax, 2
    mov rdi, 1
    syscall
.Lhang:
    jmp .Lhang
