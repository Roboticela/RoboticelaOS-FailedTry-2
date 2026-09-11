#include "arch/x86_64/syscall/syscall_entry.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/context/context.h"
#include "syscall/syscall_table.h"
#include "arch/x86_64/syscall.h"

#define IA32_EFER     0xC0000080u
#define IA32_STAR     0xC0000081u
#define IA32_LSTAR    0xC0000082u
#define IA32_FMASK    0xC0000084u
#define IA32_EFER_SCE (1ull << 0)

/*
 * STAR layout vs GDT
 * ------------------
 *   [47:32] = ROBO_GDT_KERNEL_CODE (0x08)
 *             SYSCALL CS = 0x08, SS = 0x08+8 = 0x10 (kernel data)
 *   [63:48] = ROBO_GDT_KERNEL_DATA (0x10)
 *             SYSRET  CS = 0x10+16 = 0x20 (user code), RPL 3 → 0x23
 *             SYSRET  SS = 0x10+8  = 0x18 (user data), RPL 3 → 0x1B
 *
 * These selectors must stay aligned with kernel/arch/x86_64/gdt.h.
 */

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

void robo_syscall_cpu_init(void)
{
    uint64_t efer, star, lstar;

    efer = rdmsr(IA32_EFER) | IA32_EFER_SCE;
    wrmsr(IA32_EFER, efer);
    star = ((uint64_t)ROBO_GDT_KERNEL_DATA << 48) |
           ((uint64_t)ROBO_GDT_KERNEL_CODE << 32);
    wrmsr(IA32_STAR, star);
    lstar = (uint64_t)(uintptr_t)robo_syscall_entry;
    wrmsr(IA32_LSTAR, lstar);
    wrmsr(IA32_FMASK, ROBO_RFLAGS_IF);
}

void robo_syscall_init(void)
{
    robo_syscall_cpu_init();
    robo_syscall_table_init();
}
