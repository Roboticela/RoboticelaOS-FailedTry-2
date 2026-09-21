#include "arch/x86_64/gdt.h"
#include <stddef.h>

struct gdtEnt {
    uint16_t limitLow;
    uint16_t baseLow;
    uint8_t baseMid;
    uint8_t access;
    uint8_t gran;
    uint8_t baseHigh;
} __attribute__((packed));

struct gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

extern void GdtReload(const struct gdtr *gdtr);

#define GDT_COUNT 7

static struct gdtEnt gGdt[GDT_COUNT] __attribute__((aligned(16)));
static struct gdtr gGdtr;

static void GdtClear(struct gdtEnt *e)
{
    e->limitLow = 0;
    e->baseLow = 0;
    e->baseMid = 0;
    e->access = 0;
    e->gran = 0;
    e->baseHigh = 0;
}

static void GdtSetCode(struct gdtEnt *e, uint8_t dpl)
{
    GdtClear(e);
    e->access = (uint8_t)(0x9Au | ((dpl & 3u) << 5));
    e->gran = 0x20u;
}

static void GdtSetData(struct gdtEnt *e, uint8_t dpl)
{
    GdtClear(e);
    e->access = (uint8_t)(0x92u | ((dpl & 3u) << 5));
    e->gran = 0xC0u;
}

static void GdtSetTss(uint64_t base, uint32_t limit)
{
    struct gdtEnt *e = &gGdt[5];
    uint32_t *hi = (uint32_t *)&gGdt[6];

    e->limitLow = (uint16_t)(limit & 0xFFFFu);
    e->baseLow = (uint16_t)(base & 0xFFFFu);
    e->baseMid = (uint8_t)((base >> 16) & 0xFFu);
    e->access = 0x89u;
    e->gran = (uint8_t)((limit >> 16) & 0x0Fu);
    e->baseHigh = (uint8_t)((base >> 24) & 0xFFu);
    hi[0] = (uint32_t)(base >> 32);
    hi[1] = 0;
}

void GdtInit(void *tss, uint32_t tssSize)
{
    uint16_t sel = RoboGdtTss;

    GdtClear(&gGdt[0]);
    GdtSetCode(&gGdt[1], 0);
    GdtSetData(&gGdt[2], 0);
    GdtSetData(&gGdt[3], 3);
    GdtSetCode(&gGdt[4], 3);
    GdtSetTss((uint64_t)(uintptr_t)tss, tssSize ? tssSize - 1u : 0);

    gGdtr.limit = (uint16_t)(sizeof(gGdt) - 1u);
    gGdtr.base = (uint64_t)(uintptr_t)gGdt;
    GdtReload(&gGdtr);
    __asm__ volatile ("ltr %0" : : "r"(sel) : "memory");
}

uint16_t CpuReadCs(void)
{
    uint16_t v;
    __asm__ volatile ("mov %%cs, %0" : "=r"(v));
    return v;
}

uint16_t CpuReadDs(void)
{
    uint16_t v;
    __asm__ volatile ("mov %%ds, %0" : "=r"(v));
    return v;
}

uint16_t CpuReadSs(void)
{
    uint16_t v;
    __asm__ volatile ("mov %%ss, %0" : "=r"(v));
    return v;
}

uint16_t CpuReadTr(void)
{
    uint16_t v;
    __asm__ volatile ("str %0" : "=r"(v));
    return v;
}
