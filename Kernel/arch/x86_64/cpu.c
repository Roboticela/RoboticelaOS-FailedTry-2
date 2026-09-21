#include "arch/x86_64/cpu.h"
#include "arch/x86_64/tss.h"
#include "arch/x86_64/exception.h"
#include "arch/x86_64/syscall.h"
#include "kio.h"

static int gCpuReady;

int CpuInit(void)
{
    uint16_t cs, ds, ss, tr;

    if (gCpuReady)
        return 1;

    __asm__ volatile ("cli");
    TssInit();
    GdtInit(TssPtr(), TssSize());
    IdtInit();

    cs = CpuReadCs();
    ds = CpuReadDs();
    ss = CpuReadSs();
    tr = CpuReadTr();
    if (cs != RoboGdtKernelCode || ds != RoboGdtKernelData ||
        ss != RoboGdtKernelData || tr != RoboGdtTss) {
        kprintf("CPU  GDT reload failed  CS 0x%x  DS 0x%x  SS 0x%x  TR 0x%x\n",
                (unsigned)cs, (unsigned)ds, (unsigned)ss, (unsigned)tr);
        return 0;
    }
    if (!IdtGatePresent(RoboExcDe) ||
        !IdtGatePresent(RoboExcDb) ||
        !IdtGatePresent(RoboExcBp) ||
        !IdtGatePresent(RoboExcUd) ||
        !IdtGatePresent(RoboExcDf) ||
        !IdtGatePresent(RoboExcGp) ||
        !IdtGatePresent(RoboExcPf)) {
        kprintf("CPU  IDT missing required exception gates\n");
        return 0;
    }

    SyscallInit();

    gCpuReady = 1;
    return 1;
}

int CpuReady(void)
{
    return gCpuReady;
}

void CpuReport(void)
{
    if (!gCpuReady) {
        kprintf("CPU  not ready\n");
        return;
    }
    kprintf("CPU  GDT  CS 0x%x  DS 0x%x  TSS 0x%x\n",
            (unsigned)CpuReadCs(),
            (unsigned)CpuReadDs(),
            (unsigned)CpuReadTr());
    kprintf("CPU  IDT  %u gates  exceptions 0-31\n", (unsigned)RoboIdtEntries);
    kprintf("CPU  TSS  RSP0 0x%llx  IST1 0x%llx  (double fault)\n",
            (unsigned long long)TssRsp0(),
            (unsigned long long)TssIst1());
    kprintf("CPU  ready\n");
}

#ifndef CPU_HOST_TEST
void CpuFbReport(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    if (!gCpuReady)
        return;
    KfbPuts("CPU  GDT CS ", mute);
    KfbPuthex((unsigned long long)CpuReadCs(), white);
    KfbPuts("  TSS ", mute);
    KfbPuthex((unsigned long long)CpuReadTr(), white);
    KfbPuts("  IDT 256\n", mute);
}
#endif
