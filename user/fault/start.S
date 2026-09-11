.intel_syntax noprefix

.section .text
.global _start
_start:
    mov rax, 0xFFFF808001000000
    mov al, byte ptr [rax]
    mov rax, 1
    xor rdi, rdi
    syscall
.Lhang:
    jmp .Lhang
