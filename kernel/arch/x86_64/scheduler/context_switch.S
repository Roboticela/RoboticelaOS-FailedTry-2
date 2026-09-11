.intel_syntax noprefix

.section .text

/*
 * First resume of a newly created kernel task.
 * Entered via iretq with RSP 16-byte aligned (no return address).
 * `call` then satisfies SysV (RSP % 16 == 8 at C entry).
 */
.global robo_task_trampoline
.extern robo_task_bootstrap
.extern robo_task_exit
robo_task_trampoline:
    call robo_task_bootstrap
    call robo_task_exit
    ud2

/*
 * Register-preservation stress loop. SysV: rdi = ok, rsi = bad.
 * Owns RBX, RBP, R12-R15. Never returns. Preemption must restore them.
 */
.global robo_reg_preserve_loop
robo_reg_preserve_loop:
    mov rbx, 0xB1B1B1B1B1B1B1B1
    mov rbp, 0xB2B2B2B2B2B2B2B2
    mov r12, 0xC2C2C2C2C2C2C2C2
    mov r13, 0xC3C3C3C3C3C3C3C3
    mov r14, 0xC4C4C4C4C4C4C4C4
    mov r15, 0xC5C5C5C5C5C5C5C5
.Lreg_loop:
    mov rax, 0xB1B1B1B1B1B1B1B1
    cmp rbx, rax
    jne .Lreg_bad
    mov rax, 0xB2B2B2B2B2B2B2B2
    cmp rbp, rax
    jne .Lreg_bad
    mov rax, 0xC2C2C2C2C2C2C2C2
    cmp r12, rax
    jne .Lreg_bad
    mov rax, 0xC3C3C3C3C3C3C3C3
    cmp r13, rax
    jne .Lreg_bad
    mov rax, 0xC4C4C4C4C4C4C4C4
    cmp r14, rax
    jne .Lreg_bad
    mov rax, 0xC5C5C5C5C5C5C5C5
    cmp r15, rax
    jne .Lreg_bad
    inc qword ptr [rdi]
    pause
    jmp .Lreg_loop
.Lreg_bad:
    inc qword ptr [rsi]
    jmp .Lreg_loop
