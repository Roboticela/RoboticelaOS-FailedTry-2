.intelSyntax noprefix
.section .text.boot, "ax", @progbits
.global _start
.extern kmain

_start:
    test rdi, rdi
    jnz 1f
    mov rdi, rcx
1:
    lea rsp, [rip + kstackEnd]
    and rsp, -16
    xor rbp, rbp
    call kmain
2:
    hlt
    jmp 2b

.section .bss
    .align 16
.global kstack
kstack:
    .space 16384
.global kstackEnd
kstackEnd:
