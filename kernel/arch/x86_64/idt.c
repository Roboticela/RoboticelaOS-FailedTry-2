#include "arch/x86_64/idt.h"
#include <stddef.h>
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/tss.h"

struct idt_gate {
    uint16_t off_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t off_mid;
    uint32_t off_high;
    uint32_t zero;
} __attribute__((packed));

struct idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

extern void (*isr_stub_table[ROBO_IDT_EXCEPTIONS])(void);

static struct idt_gate g_idt[ROBO_IDT_ENTRIES] __attribute__((aligned(16)));
static struct idtr g_idtr;

static void idt_set_gate(unsigned vec, void (*handler)(void), uint8_t ist, uint8_t dpl)
{
    uint64_t addr = (uint64_t)(uintptr_t)handler;
    struct idt_gate *g = &g_idt[vec];

    g->off_low = (uint16_t)(addr & 0xFFFFu);
    g->selector = ROBO_GDT_KERNEL_CODE;
    g->ist = (uint8_t)(ist & 7u);
    g->type_attr = (uint8_t)(0x8Eu | ((dpl & 3u) << 5));
    g->off_mid = (uint16_t)((addr >> 16) & 0xFFFFu);
    g->off_high = (uint32_t)(addr >> 32);
    g->zero = 0;
}

void robo_idt_init(void)
{
    unsigned i;

    for (i = 0; i < ROBO_IDT_ENTRIES; i++) {
        g_idt[i].off_low = 0;
        g_idt[i].selector = 0;
        g_idt[i].ist = 0;
        g_idt[i].type_attr = 0;
        g_idt[i].off_mid = 0;
        g_idt[i].off_high = 0;
        g_idt[i].zero = 0;
    }

    for (i = 0; i < ROBO_IDT_EXCEPTIONS; i++)
        idt_set_gate(i, isr_stub_table[i], 0, 0);

    idt_set_gate(8, isr_stub_table[8], ROBO_TSS_IST_DF, 0);

    g_idtr.limit = (uint16_t)(sizeof(g_idt) - 1u);
    g_idtr.base = (uint64_t)(uintptr_t)g_idt;
    __asm__ volatile ("lidt %0" : : "m"(g_idtr) : "memory");
}

int robo_idt_gate_present(unsigned vec)
{
    if (vec >= ROBO_IDT_ENTRIES)
        return 0;
    return (g_idt[vec].type_attr & 0x80u) ? 1 : 0;
}

uint16_t robo_idt_limit(void)
{
    return g_idtr.limit;
}

uint64_t robo_idt_base(void)
{
    return g_idtr.base;
}
