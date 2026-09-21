#include "arch/x86_64/mm/paging_cpu.h"
#include "mm/vmm/paging.h"

#define IA32_EFER 0xC0000080u
#define IA32_EFER_NXE (1ull << 11)
#define IA32_FS_BASE 0xC0000100u
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

PhysAddr CpuReadCr3(void)
{
    PhysAddr v;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(v));
    return v & RoboPteAddr;
}

void CpuWriteCr3(PhysAddr pml4)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pml4 & RoboPteAddr) : "memory");
}

void CpuInvlpg(VirtAddr va)
{
    __asm__ volatile ("invlpg (%0)" : : "r"(va) : "memory");
}

int CpuNxSupported(void)
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

int CpuNxEnabled(void)
{
    return (rdmsr(IA32_EFER) & IA32_EFER_NXE) != 0;
}

void CpuEnableNx(void)
{
    uint64_t efer;
    if (!CpuNxSupported())
        return;
    efer = rdmsr(IA32_EFER);
    if (efer & IA32_EFER_NXE)
        return;
    wrmsr(IA32_EFER, efer | IA32_EFER_NXE);
}

void CpuEnableWp(void)
{
    unsigned long cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= CR0_WP;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

void CpuWriteFsBase(uint64_t value)
{
    wrmsr(IA32_FS_BASE, value);
}
