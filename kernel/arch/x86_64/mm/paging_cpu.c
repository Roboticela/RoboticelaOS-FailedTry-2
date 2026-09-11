#include "arch/x86_64/mm/paging_cpu.h"
#include "mm/vmm/paging.h"

#define IA32_EFER 0xC0000080u
#define IA32_EFER_NXE (1ull << 11)
#define CR0_WP (1ull << 16)

static uint64_t rdmsr(uint32_t id)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(id));
    return ((uint64_t)hi << 32) | lo;
}

static void wrmsr(uint32_t id, uint64_t v)
{
    uint32_t lo = (uint32_t)v;
    uint32_t hi = (uint32_t)(v >> 32);
    __asm__ volatile ("wrmsr" : : "c"(id), "a"(lo), "d"(hi) : "memory");
}

phys_addr_t robo_cpu_read_cr3(void)
{
    phys_addr_t v;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(v));
    return v & ROBO_PTE_ADDR;
}

void robo_cpu_write_cr3(phys_addr_t pml4)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pml4 & ROBO_PTE_ADDR) : "memory");
}

void robo_cpu_invlpg(virt_addr_t va)
{
    __asm__ volatile ("invlpg (%0)" : : "r"(va) : "memory");
}

int robo_cpu_nx_supported(void)
{
    uint32_t eax, ebx, ecx, edx;
    eax = 0x80000000u;
    __asm__ volatile ("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (eax < 0x80000001u)
        return 0;
    eax = 0x80000001u;
    __asm__ volatile ("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (edx & (1u << 20)) != 0;
}

int robo_cpu_nx_enabled(void)
{
    return (rdmsr(IA32_EFER) & IA32_EFER_NXE) != 0;
}

void robo_cpu_enable_nx(void)
{
    uint64_t efer;
    if (!robo_cpu_nx_supported())
        return;
    efer = rdmsr(IA32_EFER);
    if (efer & IA32_EFER_NXE)
        return;
    wrmsr(IA32_EFER, efer | IA32_EFER_NXE);
}

void robo_cpu_enable_wp(void)
{
    unsigned long cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= CR0_WP;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
}
