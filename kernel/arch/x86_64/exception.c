#include "arch/x86_64/exception.h"
#include "arch/x86_64/timer/pit.h"
#include "scheduler/scheduler.h"
#include "kio.h"

#include <stddef.h>

extern volatile uint64_t robo_saved_cr2;
extern void robo_pf_probe_read(uint64_t va);
extern char robo_pf_probe_read_rip[];
extern char robo_pf_probe_read_resume[];
extern void robo_irq_dispatch(struct robo_iframe *frame);

static unsigned g_bp_count;

static int g_pf_recover;
static uint64_t g_pf_expect_va;
static uint64_t g_pf_resume;
static int g_pf_caught;
static uint64_t g_pf_got_cr2;
static uint64_t g_pf_got_rip;
static uint64_t g_pf_got_err;

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

static void halt_forever(void)
{
    for (;;)
        __asm__ volatile ("cli; hlt");
}

static void pf_hex(const char *label, uint64_t v)
{
    static const char *d = "0123456789abcdef";
    char buf[19];
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 16; i++)
        buf[2 + i] = d[(v >> (60 - 4 * i)) & 15ull];
    buf[18] = 0;
    kprintf("%s%s\n", label, buf);
}

static void panic_regs(const struct robo_iframe *f)
{
    kprintf("Vector  : %llu\n", (unsigned long long)f->vector);
    pf_hex("Error   : ", f->error);
    pf_hex("RIP     : ", f->rip);
    pf_hex("CS      : ", f->cs);
    pf_hex("RFLAGS  : ", f->rflags);
    pf_hex("RSP     : ", f->rsp);
    pf_hex("SS      : ", f->ss);
}

static void pf_diagnose(const struct robo_iframe *f, uint64_t cr2)
{
    uint64_t err = f->error;

    kprintf("========================================\n");
    kprintf("          KERNEL PANIC\n");
    kprintf("========================================\n\n");
    kprintf("Page Fault\n\n");
    pf_hex("Address : ", cr2);
    pf_hex("RIP     : ", f->rip);
    pf_hex("Error   : ", err);
    kprintf("Access  : %s\n", (err & ROBO_PF_WR) ? "WRITE" : "READ");
    kprintf("Mode    : %s\n", (err & ROBO_PF_US) ? "USER" : "KERNEL");
    kprintf("Present : %s\n", (err & ROBO_PF_P) ? "YES" : "NO");
    kprintf("Reserved : %s\n", (err & ROBO_PF_RSVD) ? "YES" : "NO");
    kprintf("Execute : %s\n", (err & ROBO_PF_ID) ? "YES" : "NO");
    kprintf("\n========================================\n");
}

static void panic_page_fault(struct robo_iframe *f)
{
    uint64_t cr2 = robo_saved_cr2;

    pf_diagnose(f, cr2);

    if (g_pf_recover && cr2 == g_pf_expect_va) {
        g_pf_got_cr2 = cr2;
        g_pf_got_rip = f->rip;
        g_pf_got_err = f->error;
        g_pf_caught = 1;
        f->rip = g_pf_resume;
        return;
    }

    halt_forever();
}

static void panic_exception(const struct robo_iframe *f)
{
    kprintf("========================================\n");
    kprintf("          KERNEL PANIC\n");
    kprintf("========================================\n\n");
    kprintf("%s\n\n", exc_name(f->vector));
    panic_regs(f);
    kprintf("\n========================================\n");
    halt_forever();
}

unsigned robo_exception_breakpoint_count(void)
{
    return g_bp_count;
}

int robo_pf_catch_read(uint64_t va, uint64_t *cr2, uint64_t *rip, uint64_t *err)
{
    g_pf_caught = 0;
    g_pf_got_cr2 = 0;
    g_pf_got_rip = 0;
    g_pf_got_err = 0;
    g_pf_expect_va = va;
    g_pf_resume = (uint64_t)(uintptr_t)robo_pf_probe_read_resume;
    g_pf_recover = 1;
    __asm__ volatile ("" ::: "memory");
    robo_pf_probe_read(va);
    __asm__ volatile ("" ::: "memory");
    g_pf_recover = 0;
    if (!g_pf_caught)
        return 0;
    if (cr2) *cr2 = g_pf_got_cr2;
    if (rip) *rip = g_pf_got_rip;
    if (err) *err = g_pf_got_err;
    return 1;
}

uint64_t robo_pf_probe_rip(void)
{
    return (uint64_t)(uintptr_t)robo_pf_probe_read_rip;
}

struct robo_iframe *robo_exception_dispatch(struct robo_iframe *frame)
{
    if (!frame) {
        halt_forever();
        return 0;
    }

    if (frame->vector == ROBO_EXC_BP) {
        g_bp_count++;
        kprintf("CPU  breakpoint  RIP 0x%llx\n", (unsigned long long)frame->rip);
        return frame;
    }

    if (frame->vector == ROBO_VEC_RESCHED)
        return robo_scheduler_resched(frame);

    if (frame->vector >= 32 && frame->vector < 48) {
        robo_irq_dispatch(frame);
        if (frame->vector == ROBO_IRQ_BASE)
            return robo_scheduler_tick(frame);
        return frame;
    }

    if (frame->vector == ROBO_EXC_PF) {
        panic_page_fault(frame);
        return frame;
    }

    panic_exception(frame);
    return frame;
}
