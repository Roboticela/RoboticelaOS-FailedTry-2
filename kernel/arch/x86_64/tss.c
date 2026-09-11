#include "arch/x86_64/tss.h"
#include <stddef.h>

extern char kstack_end[];

#define ROBO_DF_STACK_SIZE 8192u

struct robo_tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

static struct robo_tss g_tss;
static uint8_t g_df_stack[ROBO_DF_STACK_SIZE] __attribute__((aligned(16)));

void robo_tss_init(void)
{
    uint8_t *p = (uint8_t *)&g_tss;
    unsigned i;
    for (i = 0; i < sizeof(g_tss); i++)
        p[i] = 0;
    g_tss.rsp0 = (uint64_t)(uintptr_t)kstack_end;
    g_tss.ist1 = (uint64_t)(uintptr_t)(g_df_stack + ROBO_DF_STACK_SIZE);
    g_tss.iomap_base = (uint16_t)sizeof(g_tss);
}

void *robo_tss_ptr(void)
{
    return &g_tss;
}

uint32_t robo_tss_size(void)
{
    return (uint32_t)sizeof(g_tss);
}

uint64_t robo_tss_rsp0(void)
{
    return g_tss.rsp0;
}

uint64_t robo_tss_ist1(void)
{
    return g_tss.ist1;
}
