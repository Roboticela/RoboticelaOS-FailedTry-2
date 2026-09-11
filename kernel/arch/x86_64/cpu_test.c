#include "arch/x86_64/cpu.h"
#include "arch/x86_64/exception.h"
#include "kio.h"

static int cpu_fail(const char *tag)
{
    kprintf("CPU selftest: %s\n", tag);
    return 0;
}

int robo_cpu_selftest(void)
{
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) idtr;
    unsigned before;
    unsigned vec;

    if (!robo_cpu_ready())
        return cpu_fail("not ready");

    if (robo_cpu_read_cs() != ROBO_GDT_KERNEL_CODE)
        return cpu_fail("CS");
    if (robo_cpu_read_ds() != ROBO_GDT_KERNEL_DATA)
        return cpu_fail("DS");
    if (robo_cpu_read_ss() != ROBO_GDT_KERNEL_DATA)
        return cpu_fail("SS");
    if (robo_cpu_read_tr() != ROBO_GDT_TSS)
        return cpu_fail("TR");

    __asm__ volatile ("sidt %0" : "=m"(idtr));
    if (idtr.limit != robo_idt_limit() || idtr.base != robo_idt_base())
        return cpu_fail("IDTR");
    if (idtr.limit != (uint16_t)(ROBO_IDT_ENTRIES * 16u - 1u))
        return cpu_fail("IDT size");

    for (vec = 0; vec < ROBO_IDT_EXCEPTIONS; vec++) {
        if (!robo_idt_gate_present(vec))
            return cpu_fail("exception gate");
    }
    if (!robo_idt_gate_present(ROBO_EXC_DE) ||
        !robo_idt_gate_present(ROBO_EXC_DB) ||
        !robo_idt_gate_present(ROBO_EXC_BP) ||
        !robo_idt_gate_present(ROBO_EXC_UD) ||
        !robo_idt_gate_present(ROBO_EXC_DF) ||
        !robo_idt_gate_present(ROBO_EXC_GP) ||
        !robo_idt_gate_present(ROBO_EXC_PF))
        return cpu_fail("required gates");

    before = robo_exception_breakpoint_count();
    __asm__ volatile ("int3");
    if (robo_exception_breakpoint_count() != before + 1u)
        return cpu_fail("breakpoint");

    return 1;
}
