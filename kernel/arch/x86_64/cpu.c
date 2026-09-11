#include "arch/x86_64/cpu.h"
#include "arch/x86_64/tss.h"
#include "arch/x86_64/exception.h"
#include "arch/x86_64/syscall.h"
#include "kio.h"

static int g_cpu_ready;

int robo_cpu_init(void)
{
    uint16_t cs, ds, ss, tr;

    if (g_cpu_ready)
        return 1;

    __asm__ volatile ("cli");
    robo_tss_init();
    robo_gdt_init(robo_tss_ptr(), robo_tss_size());
    robo_idt_init();

    cs = robo_cpu_read_cs();
    ds = robo_cpu_read_ds();
    ss = robo_cpu_read_ss();
    tr = robo_cpu_read_tr();
    if (cs != ROBO_GDT_KERNEL_CODE || ds != ROBO_GDT_KERNEL_DATA ||
        ss != ROBO_GDT_KERNEL_DATA || tr != ROBO_GDT_TSS) {
        kprintf("CPU  GDT reload failed  CS 0x%x  DS 0x%x  SS 0x%x  TR 0x%x\n",
                (unsigned)cs, (unsigned)ds, (unsigned)ss, (unsigned)tr);
        return 0;
    }
    if (!robo_idt_gate_present(ROBO_EXC_DE) ||
        !robo_idt_gate_present(ROBO_EXC_DB) ||
        !robo_idt_gate_present(ROBO_EXC_BP) ||
        !robo_idt_gate_present(ROBO_EXC_UD) ||
        !robo_idt_gate_present(ROBO_EXC_DF) ||
        !robo_idt_gate_present(ROBO_EXC_GP) ||
        !robo_idt_gate_present(ROBO_EXC_PF)) {
        kprintf("CPU  IDT missing required exception gates\n");
        return 0;
    }

    robo_syscall_init();

    g_cpu_ready = 1;
    return 1;
}

int robo_cpu_ready(void)
{
    return g_cpu_ready;
}

void robo_cpu_report(void)
{
    if (!g_cpu_ready) {
        kprintf("CPU  not ready\n");
        return;
    }
    kprintf("CPU  GDT  CS 0x%x  DS 0x%x  TSS 0x%x\n",
            (unsigned)robo_cpu_read_cs(),
            (unsigned)robo_cpu_read_ds(),
            (unsigned)robo_cpu_read_tr());
    kprintf("CPU  IDT  %u gates  exceptions 0-31\n", (unsigned)ROBO_IDT_ENTRIES);
    kprintf("CPU  TSS  RSP0 0x%llx  IST1 0x%llx  (double fault)\n",
            (unsigned long long)robo_tss_rsp0(),
            (unsigned long long)robo_tss_ist1());
    kprintf("CPU  ready\n");
}

#ifndef CPU_HOST_TEST
void robo_cpu_fb_report(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    if (!g_cpu_ready)
        return;
    kfb_puts("CPU  GDT CS ", mute);
    kfb_puthex((unsigned long long)robo_cpu_read_cs(), white);
    kfb_puts("  TSS ", mute);
    kfb_puthex((unsigned long long)robo_cpu_read_tr(), white);
    kfb_puts("  IDT 256\n", mute);
}
#endif
