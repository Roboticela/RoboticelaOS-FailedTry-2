.intelSyntax noprefix

.section .text
.global _start
_start:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + msg]
    mov rdx, 23
    syscall
    mov rcx, 8000000
.Ldelay:
    dec rcx
    jnz .Ldelay
    mov rax, 2
    xor rdi, rdi
    syscall
.Lhang:
    jmp .Lhang

.section .rodata
msg:
    .ascii "Hello from user space.\n"
