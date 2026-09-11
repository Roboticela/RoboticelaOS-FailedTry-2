.intel_syntax noprefix

.macro ISR_NOERR vec
.global isr_\vec
isr_\vec:
    push 0
    push \vec
    jmp isr_common
.endm

.macro ISR_ERR vec
.global isr_\vec
isr_\vec:
    push \vec
    jmp isr_common
.endm

.section .text
.extern robo_exception_dispatch
.extern robo_x86_resume_iframe

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR 8
ISR_NOERR 9
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31
ISR_NOERR 32
ISR_NOERR 33
ISR_NOERR 34
ISR_NOERR 35
ISR_NOERR 36
ISR_NOERR 37
ISR_NOERR 38
ISR_NOERR 39
ISR_NOERR 40
ISR_NOERR 41
ISR_NOERR 42
ISR_NOERR 43
ISR_NOERR 44
ISR_NOERR 45
ISR_NOERR 46
ISR_NOERR 47
ISR_NOERR 48

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    cld
    mov rax, cr2
    mov [rip + robo_saved_cr2], rax
    mov rdi, rsp
    sub rsp, 8
    call robo_exception_dispatch
    /*
     * RAX is the iframe to resume (current or next thread).
     * Resume switches RSP, pops GPRs, and iretq. Shared by IRQ 32
     * and int $48. Never returns here.
     */
    jmp robo_x86_resume_iframe

.global robo_pf_probe_read
.global robo_pf_probe_read_rip
.global robo_pf_probe_read_resume
robo_pf_probe_read:
robo_pf_probe_read_rip:
    mov al, byte ptr [rdi]
robo_pf_probe_read_resume:
    xor eax, eax
    ret

.global robo_gdt_reload
robo_gdt_reload:
    lgdt [rdi]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax
    pop rax
    push 0x08
    push rax
    retfq

.section .rodata
.align 8
.global isr_stub_table
isr_stub_table:
    .quad isr_0
    .quad isr_1
    .quad isr_2
    .quad isr_3
    .quad isr_4
    .quad isr_5
    .quad isr_6
    .quad isr_7
    .quad isr_8
    .quad isr_9
    .quad isr_10
    .quad isr_11
    .quad isr_12
    .quad isr_13
    .quad isr_14
    .quad isr_15
    .quad isr_16
    .quad isr_17
    .quad isr_18
    .quad isr_19
    .quad isr_20
    .quad isr_21
    .quad isr_22
    .quad isr_23
    .quad isr_24
    .quad isr_25
    .quad isr_26
    .quad isr_27
    .quad isr_28
    .quad isr_29
    .quad isr_30
    .quad isr_31

.global irq_stub_table
irq_stub_table:
    .quad isr_32
    .quad isr_33
    .quad isr_34
    .quad isr_35
    .quad isr_36
    .quad isr_37
    .quad isr_38
    .quad isr_39
    .quad isr_40
    .quad isr_41
    .quad isr_42
    .quad isr_43
    .quad isr_44
    .quad isr_45
    .quad isr_46
    .quad isr_47

.section .bss
.align 8
.global robo_saved_cr2
robo_saved_cr2:
    .quad 0
