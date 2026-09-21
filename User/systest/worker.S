.intelSyntax noprefix

.section .text
.global worker
worker:
    mov byte ptr [rdi], 0xAB
.Lsleep:
    mov rax, 10
    mov rdi, 1000
    xor rsi, rsi
    xor rdx, rdx
    xor r10, r10
    xor r8, r8
    xor r9, r9
    syscall
    jmp .Lsleep
