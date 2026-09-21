.intelSyntax noprefix

.macro ISR_NOERR vec
.global isr\vec
isr\vec:
    push 0
    push \vec
    jmp isrCommon
.endm

.macro ISR_ERR vec
.global isr\vec
isr\vec:
    push \vec
    jmp isrCommon
.endm

.section .text
.extern ExceptionDispatch
.extern X86ResumeIframe

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

isrCommon:
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
    mov [rip + SavedCr2], rax
    mov rdi, rsp
    sub rsp, 8
    call ExceptionDispatch
    /*
     * RAX is the iframe to resume (current or next thread).
     * Resume switches RSP, pops GPRs, and iretq. Shared by IRQ 32
     * and int $48. Never returns here.
     */
    jmp X86ResumeIframe

.global PfProbeRead
.global PfProbeReadRip
.global PfProbeReadResume
PfProbeRead:
PfProbeReadRip:
    mov al, byte ptr [rdi]
PfProbeReadResume:
    xor eax, eax
    ret

.global GdtReload
GdtReload:
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
.global isrStubTable
isrStubTable:
    .quad isr0
    .quad isr1
    .quad isr2
    .quad isr3
    .quad isr4
    .quad isr5
    .quad isr6
    .quad isr7
    .quad isr8
    .quad isr9
    .quad isr10
    .quad isr11
    .quad isr12
    .quad isr13
    .quad isr14
    .quad isr15
    .quad isr16
    .quad isr17
    .quad isr18
    .quad isr19
    .quad isr20
    .quad isr21
    .quad isr22
    .quad isr23
    .quad isr24
    .quad isr25
    .quad isr26
    .quad isr27
    .quad isr28
    .quad isr29
    .quad isr30
    .quad isr31

.global irqStubTable
irqStubTable:
    .quad isr32
    .quad isr33
    .quad isr34
    .quad isr35
    .quad isr36
    .quad isr37
    .quad isr38
    .quad isr39
    .quad isr40
    .quad isr41
    .quad isr42
    .quad isr43
    .quad isr44
    .quad isr45
    .quad isr46
    .quad isr47

.section .bss
.align 8
.global SavedCr2
SavedCr2:
    .quad 0
