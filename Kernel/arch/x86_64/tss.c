#include "arch/x86_64/tss.h"
#include <stddef.h>

extern char kstackEnd[];

#define RoboDfStackSize 8192u

struct Tss {
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
    uint16_t iomapBase;
} __attribute__((packed));

static struct Tss gTss;
static uint8_t gDfStack[RoboDfStackSize] __attribute__((aligned(16)));

void TssInit(void)
{
    uint8_t *p = (uint8_t *)&gTss;
    unsigned i;
    for (i = 0; i < sizeof(gTss); i++)
        p[i] = 0;
    gTss.rsp0 = (uint64_t)(uintptr_t)kstackEnd;
    gTss.ist1 = (uint64_t)(uintptr_t)(gDfStack + RoboDfStackSize);
    gTss.iomapBase = (uint16_t)sizeof(gTss);
}

void *TssPtr(void)
{
    return &gTss;
}

uint32_t TssSize(void)
{
    return (uint32_t)sizeof(gTss);
}

uint64_t TssRsp0(void)
{
    return gTss.rsp0;
}

void TssSetRsp0(uint64_t rsp0)
{
    gTss.rsp0 = rsp0;
}

uint64_t TssIst1(void)
{
    return gTss.ist1;
}
