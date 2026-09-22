.intel_syntax noprefix

.section .text
.global RegPreserveTest
RegPreserveTest:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    mov rbx, 0x1111111111111111
    mov rbp, 0x2222222222222222
    mov r12, 0x3333333333333333
    mov r13, 0x4444444444444444
    mov r14, 0x5555555555555555
    mov r15, 0x6666666666666666

    mov rax, 11
    xor rdi, rdi
    xor rsi, rsi
    xor rdx, rdx
    xor r10, r10
    xor r8, r8
    xor r9, r9
    syscall

    mov rax, 0
    mov rdi, 0x1111111111111111
    cmp rbx, rdi
    jne .Lfail
    mov rdi, 0x2222222222222222
    cmp rbp, rdi
    jne .Lfail
    mov rdi, 0x3333333333333333
    cmp r12, rdi
    jne .Lfail
    mov rdi, 0x4444444444444444
    cmp r13, rdi
    jne .Lfail
    mov rdi, 0x5555555555555555
    cmp r14, rdi
    jne .Lfail
    mov rdi, 0x6666666666666666
    cmp r15, rdi
    jne .Lfail

    lea rsi, [rip + .Lmsg]
    mov rdi, 1
    mov rdx, 4
    mov rax, 3
    syscall

    mov rdi, 0x1111111111111111
    cmp rbx, rdi
    jne .Lfail
    mov rdi, 0x2222222222222222
    cmp rbp, rdi
    jne .Lfail
    mov rdi, 0x3333333333333333
    cmp r12, rdi
    jne .Lfail
    mov rdi, 0x4444444444444444
    cmp r13, rdi
    jne .Lfail
    mov rdi, 0x5555555555555555
    cmp r14, rdi
    jne .Lfail
    mov rdi, 0x6666666666666666
    cmp r15, rdi
    jne .Lfail

    xor eax, eax
    jmp .Lout
.Lfail:
    mov eax, 1
.Lout:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

.section .rodata
.Lmsg:
    .ascii "reg\n"
