.intel_syntax noprefix

.section .text
.global _start
.extern robo_libc_start
_start:
    xor ebp, ebp
    mov rdi, qword ptr [rsp]
    lea rsi, [rsp + 8]
    mov rcx, rdi
    shl rcx, 3
    lea rdx, [rsi + rcx + 8]
    and rsp, -16
    call robo_libc_start
.Lhang:
    jmp .Lhang
