#include "arch/x86_64/cpu.h"
#include "arch/x86_64/exception.h"
#include "kio.h"

static int CpuFail(const char *tag)
{
    kprintf("CPU selftest: %s\n", tag);
    return 0;
}

int CpuSelftest(void)
{
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) idtr;
    unsigned before;
    unsigned vec;

    if (!CpuReady())
        return CpuFail("not ready");

    if (CpuReadCs() != RoboGdtKernelCode)
        return CpuFail("CS");
    if (CpuReadDs() != RoboGdtKernelData)
        return CpuFail("DS");
    if (CpuReadSs() != RoboGdtKernelData)
        return CpuFail("SS");
    if (CpuReadTr() != RoboGdtTss)
        return CpuFail("TR");

    __asm__ volatile ("sidt %0" : "=m"(idtr));
    if (idtr.limit != IdtLimit() || idtr.base != IdtBase())
        return CpuFail("IDTR");
    if (idtr.limit != (uint16_t)(RoboIdtEntries * 16u - 1u))
        return CpuFail("IDT size");

    for (vec = 0; vec < RoboIdtExceptions; vec++) {
        if (!IdtGatePresent(vec))
            return CpuFail("exception gate");
    }
    if (!IdtGatePresent(RoboExcDe) ||
        !IdtGatePresent(RoboExcDb) ||
        !IdtGatePresent(RoboExcBp) ||
        !IdtGatePresent(RoboExcUd) ||
        !IdtGatePresent(RoboExcDf) ||
        !IdtGatePresent(RoboExcGp) ||
        !IdtGatePresent(RoboExcPf))
        return CpuFail("required gates");

    before = ExceptionBreakpointCount();
    __asm__ volatile ("int3");
    if (ExceptionBreakpointCount() != before + 1u)
        return CpuFail("breakpoint");

    return 1;
}
