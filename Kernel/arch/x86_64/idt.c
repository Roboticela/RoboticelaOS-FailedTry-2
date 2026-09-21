#include "arch/x86_64/idt.h"
#include <stddef.h>
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/tss.h"

struct idtGate {
    uint16_t offLow;
    uint16_t selector;
    uint8_t ist;
    uint8_t typeAttr;
    uint16_t offMid;
    uint32_t offHigh;
    uint32_t zero;
} __attribute__((packed));

struct idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

extern void (*isrStubTable[RoboIdtExceptions])(void);

static struct idtGate gIdt[RoboIdtEntries] __attribute__((aligned(16)));
static struct idtr gIdtr;

static void IdtSetGate(unsigned vec, void (*handler)(void), uint8_t ist, uint8_t dpl)
{
    uint64_t addr = (uint64_t)(uintptr_t)handler;
    struct idtGate *g = &gIdt[vec];

    g->offLow = (uint16_t)(addr & 0xFFFFu);
    g->selector = RoboGdtKernelCode;
    g->ist = (uint8_t)(ist & 7u);
    g->typeAttr = (uint8_t)(0x8Eu | ((dpl & 3u) << 5));
    g->offMid = (uint16_t)((addr >> 16) & 0xFFFFu);
    g->offHigh = (uint32_t)(addr >> 32);
    g->zero = 0;
}

void IdtSet(unsigned vec, void (*handler)(void), uint8_t ist)
{
    if (vec >= RoboIdtEntries || !handler)
        return;
    IdtSetGate(vec, handler, ist, 0);
}

void IdtInit(void)
{
    unsigned i;

    for (i = 0; i < RoboIdtEntries; i++) {
        gIdt[i].offLow = 0;
        gIdt[i].selector = 0;
        gIdt[i].ist = 0;
        gIdt[i].typeAttr = 0;
        gIdt[i].offMid = 0;
        gIdt[i].offHigh = 0;
        gIdt[i].zero = 0;
    }

    for (i = 0; i < RoboIdtExceptions; i++)
        IdtSetGate(i, isrStubTable[i], 0, 0);

    IdtSetGate(8, isrStubTable[8], RoboTssIstDf, 0);

    gIdtr.limit = (uint16_t)(sizeof(gIdt) - 1u);
    gIdtr.base = (uint64_t)(uintptr_t)gIdt;
    __asm__ volatile ("lidt %0" : : "m"(gIdtr) : "memory");
}

int IdtGatePresent(unsigned vec)
{
    if (vec >= RoboIdtEntries)
        return 0;
    return (gIdt[vec].typeAttr & 0x80u) ? 1 : 0;
}

uint16_t IdtLimit(void)
{
    return gIdtr.limit;
}

uint64_t IdtBase(void)
{
    return gIdtr.base;
}
