.intel_syntax noprefix

.section .text

/*
 * First resume of a newly created kernel thread.
 *
 * Entered via iretq. After iretq, RSP is 16-byte aligned and there is no
 * return address. `call` then satisfies SysV (RSP % 16 == 8 at C entry).
 * Only a new thread starts here. A resumed thread continues at the RIP
 * saved in its interrupt frame.
 */
.global robo_thread_trampoline
.global robo_task_trampoline
.extern robo_thread_bootstrap
.extern robo_thread_exit
robo_thread_trampoline:
robo_task_trampoline:
    call robo_thread_bootstrap
    call robo_thread_exit
    ud2

/*
 * Validate next iframe and return it in RAX.
 *
 * Contract (SysV):
 *   RDI = current iframe
 *   RSI = next iframe
 *   RAX = next iframe on success
 *   IF unchanged (must already be 0)
 *   RSP unchanged (resume switches stacks)
 *   Does not return on a corrupt next frame.
 */
.equ IFRAME_RIP, 136
.equ IFRAME_CS,  144
.equ IFRAME_RSP, 160
.equ IFRAME_SS,  168

.global robo_x86_context_switch
.extern robo_x86_context_fail_frames
robo_x86_context_switch:
    test rsi, rsi
    jz .Lswitch_fail
    mov rax, qword ptr [rsi + IFRAME_CS]
    cmp rax, 0x08
    je .Lcs_ok
    cmp rax, 0x23
    jne .Lswitch_fail
.Lcs_ok:
    mov rax, qword ptr [rsi + IFRAME_SS]
    cmp rax, 0x10
    je .Lss_ok
    cmp rax, 0x1B
    jne .Lswitch_fail
.Lss_ok:
    mov rax, qword ptr [rsi + IFRAME_RIP]
    test rax, rax
    jz .Lswitch_fail
    mov rax, qword ptr [rsi + IFRAME_RSP]
    test rax, rax
    jz .Lswitch_fail
    mov rax, rsi
    ret
.Lswitch_fail:
    call robo_x86_context_fail_frames
    ud2

/*
 * Restore the iframe in RAX and resume that thread.
 *
 *   mov rsp, rax     — stack switch
 *   pop GPRs
 *   add rsp, 16      — skip vector + error
 *   iretq            — RIP, CS, RFLAGS, RSP, SS
 */
.global robo_x86_resume_iframe
.extern robo_x86_context_fail_frames
robo_x86_resume_iframe:
    test rax, rax
    jz .Lresume_fail
    mov rsp, rax
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    push rax
    mov rax, qword ptr [rsp + 16]
    cmp rax, 0x23
    je .Lresume_user_seg
    mov ax, 0x10
    jmp .Lresume_load_seg
.Lresume_user_seg:
    mov ax, 0x1B
.Lresume_load_seg:
    mov ds, ax
    mov es, ax
    pop rax
    iretq
.Lresume_fail:
    xor edi, edi
    xor esi, esi
    call robo_x86_context_fail_frames
    cli
.Lresume_halt:
    hlt
    jmp .Lresume_halt

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

/*
 * Bounded register-preservation check.
 * SysV: rdi = ok, rsi = bad, rdx = iteration count.
 * Returns after `rdx` successful checks (or keeps counting failures).
 */
.global robo_reg_preserve_iters
robo_reg_preserve_iters:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    push rdx
    cmp qword ptr [rsp], 0
    je .Lregn_done
    mov rbx, 0xB1B1B1B1B1B1B1B1
    mov rbp, 0xB2B2B2B2B2B2B2B2
    mov r12, 0xC2C2C2C2C2C2C2C2
    mov r13, 0xC3C3C3C3C3C3C3C3
    mov r14, 0xC4C4C4C4C4C4C4C4
    mov r15, 0xC5C5C5C5C5C5C5C5
.Lregn_loop:
    mov rax, 0xB1B1B1B1B1B1B1B1
    cmp rbx, rax
    jne .Lregn_bad
    mov rax, 0xB2B2B2B2B2B2B2B2
    cmp rbp, rax
    jne .Lregn_bad
    mov rax, 0xC2C2C2C2C2C2C2C2
    cmp r12, rax
    jne .Lregn_bad
    mov rax, 0xC3C3C3C3C3C3C3C3
    cmp r13, rax
    jne .Lregn_bad
    mov rax, 0xC4C4C4C4C4C4C4C4
    cmp r14, rax
    jne .Lregn_bad
    mov rax, 0xC5C5C5C5C5C5C5C5
    cmp r15, rax
    jne .Lregn_bad
    inc qword ptr [rdi]
    pause
    dec qword ptr [rsp]
    jnz .Lregn_loop
    jmp .Lregn_done
.Lregn_bad:
    inc qword ptr [rsi]
    pause
    dec qword ptr [rsp]
    jnz .Lregn_loop
.Lregn_done:
    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret
