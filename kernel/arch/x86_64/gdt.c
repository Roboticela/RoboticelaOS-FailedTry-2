#include "arch/x86_64/gdt.h"
#include <stddef.h>

struct gdt_ent {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t gran;
    uint8_t base_high;
} __attribute__((packed));

struct gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

extern void robo_gdt_reload(const struct gdtr *gdtr);

#define GDT_COUNT 7

static struct gdt_ent g_gdt[GDT_COUNT] __attribute__((aligned(16)));
static struct gdtr g_gdtr;

static void gdt_clear(struct gdt_ent *e)
{
    e->limit_low = 0;
    e->base_low = 0;
    e->base_mid = 0;
    e->access = 0;
    e->gran = 0;
    e->base_high = 0;
}

static void gdt_set_code(struct gdt_ent *e, uint8_t dpl)
{
    gdt_clear(e);
    e->access = (uint8_t)(0x9Au | ((dpl & 3u) << 5));
    e->gran = 0x20u;
}

static void gdt_set_data(struct gdt_ent *e, uint8_t dpl)
{
    gdt_clear(e);
    e->access = (uint8_t)(0x92u | ((dpl & 3u) << 5));
    e->gran = 0xC0u;
}

static void gdt_set_tss(uint64_t base, uint32_t limit)
{
    struct gdt_ent *e = &g_gdt[5];
    uint32_t *hi = (uint32_t *)&g_gdt[6];

    e->limit_low = (uint16_t)(limit & 0xFFFFu);
    e->base_low = (uint16_t)(base & 0xFFFFu);
    e->base_mid = (uint8_t)((base >> 16) & 0xFFu);
    e->access = 0x89u;
    e->gran = (uint8_t)((limit >> 16) & 0x0Fu);
    e->base_high = (uint8_t)((base >> 24) & 0xFFu);
    hi[0] = (uint32_t)(base >> 32);
    hi[1] = 0;
}

void robo_gdt_init(void *tss, uint32_t tss_size)
{
    uint16_t sel = ROBO_GDT_TSS;

    gdt_clear(&g_gdt[0]);
    gdt_set_code(&g_gdt[1], 0);
    gdt_set_data(&g_gdt[2], 0);
    gdt_set_data(&g_gdt[3], 3);
    gdt_set_code(&g_gdt[4], 3);
    gdt_set_tss((uint64_t)(uintptr_t)tss, tss_size ? tss_size - 1u : 0);

    g_gdtr.limit = (uint16_t)(sizeof(g_gdt) - 1u);
    g_gdtr.base = (uint64_t)(uintptr_t)g_gdt;
    robo_gdt_reload(&g_gdtr);
    __asm__ volatile ("ltr %0" : : "r"(sel) : "memory");
}

uint16_t robo_cpu_read_cs(void)
{
    uint16_t v;
    __asm__ volatile ("mov %%cs, %0" : "=r"(v));
    return v;
}

uint16_t robo_cpu_read_ds(void)
{
    uint16_t v;
    __asm__ volatile ("mov %%ds, %0" : "=r"(v));
    return v;
}

uint16_t robo_cpu_read_ss(void)
{
    uint16_t v;
    __asm__ volatile ("mov %%ss, %0" : "=r"(v));
    return v;
}

uint16_t robo_cpu_read_tr(void)
{
    uint16_t v;
    __asm__ volatile ("str %0" : "=r"(v));
    return v;
}
