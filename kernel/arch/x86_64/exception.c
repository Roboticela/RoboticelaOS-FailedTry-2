#include "arch/x86_64/exception.h"
#include "kio.h"

static unsigned g_bp_count;

static const char *exc_name(uint64_t vec)
{
    static const char *names[32] = {
        "Divide Error",
        "Debug",
        "NMI",
        "Breakpoint",
        "Overflow",
        "Bound Range",
        "Invalid Opcode",
        "Device Not Available",
        "Double Fault",
        "Coprocessor Segment",
        "Invalid TSS",
        "Segment Not Present",
        "Stack Fault",
        "General Protection Fault",
        "Page Fault",
        "Reserved",
        "x87 Floating Point",
        "Alignment Check",
        "Machine Check",
        "SIMD Floating Point",
        "Virtualization",
        "Control Protection",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved"
    };
    if (vec < 32)
        return names[vec];
    return "Unknown";
}

static uint64_t read_cr2(void)
{
    uint64_t v;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(v));
    return v;
}

static void halt_forever(void)
{
    for (;;)
        __asm__ volatile ("cli; hlt");
}

static void panic_regs(const struct robo_iframe *f)
{
    kprintf("Vector  : %llu\n", (unsigned long long)f->vector);
    kprintf("Error   : 0x%llx\n", (unsigned long long)f->error);
    kprintf("RIP     : 0x%llx\n", (unsigned long long)f->rip);
    kprintf("CS      : 0x%llx\n", (unsigned long long)f->cs);
    kprintf("RFLAGS  : 0x%llx\n", (unsigned long long)f->rflags);
    kprintf("RSP     : 0x%llx\n", (unsigned long long)f->rsp);
    kprintf("SS      : 0x%llx\n", (unsigned long long)f->ss);
}

static void panic_page_fault(const struct robo_iframe *f)
{
    uint64_t cr2 = read_cr2();
    uint64_t err = f->error;

    kprintf("KERNEL PANIC\n\n");
    kprintf("Page Fault\n\n");
    kprintf("Address : 0x%llx\n", (unsigned long long)cr2);
    kprintf("RIP     : 0x%llx\n", (unsigned long long)f->rip);
    kprintf("Access  : %s\n", (err & 2ull) ? "WRITE" : "READ");
    kprintf("Mode    : %s\n", (err & 4ull) ? "USER" : "KERNEL");
    kprintf("Present : %s\n", (err & 1ull) ? "YES" : "NO");
    if (err & 16ull)
        kprintf("Fetch   : YES\n");
    kprintf("Error   : 0x%llx\n", (unsigned long long)err);
    halt_forever();
}

static void panic_exception(const struct robo_iframe *f)
{
    kprintf("KERNEL PANIC\n\n");
    kprintf("%s\n\n", exc_name(f->vector));
    panic_regs(f);
    halt_forever();
}

unsigned robo_exception_breakpoint_count(void)
{
    return g_bp_count;
}

void robo_exception_dispatch(struct robo_iframe *frame)
{
    if (!frame)
        halt_forever();

    if (frame->vector == ROBO_EXC_BP) {
        g_bp_count++;
        kprintf("CPU  breakpoint  RIP 0x%llx\n", (unsigned long long)frame->rip);
        return;
    }

    if (frame->vector == ROBO_EXC_PF)
        panic_page_fault(frame);

    panic_exception(frame);
}
