#include "process/process.h"
#include "thread/thread.h"
#include "arch/x86_64/mm/paging_cpu.h"
#include "kio.h"

static uint64_t gCr3Reloads;
static uint64_t gSame;
static uint64_t gCross;
static int gSwitchLog;

void ProcessSetSwitchLog(int on)
{
    gSwitchLog = on ? 1 : 0;
}

uint64_t ArchCr3Reloads(void)
{
    return gCr3Reloads;
}

uint64_t AspaceStatSame(void)
{
    return gSame;
}

uint64_t AspaceStatCross(void)
{
    return gCross;
}

void ArchSwitchAddressSpace(RoboAddressSpace *addressSpace)
{
    PhysAddr want;
    PhysAddr have;

    if (!addressSpace || !addressSpace->cr3)
        return;
    want = addressSpace->cr3;
    have = CpuReadCr3();
    if (have == want)
        return;
    CpuWriteCr3(want);
    gCr3Reloads++;
}

void ArchPrepareThreadSwitch(RoboThread *old, RoboThread *next)
{
    RoboProcess *fromP;
    RoboProcess *toP;
    PhysAddr expect;
    PhysAddr actual;
    int same;

    if (!next)
        return;
    toP = next->process;
    fromP = old ? old->process : 0;
    if (!toP)
        ProcessPanicRelation(next, "next thread has no process");
    if (old && !fromP)
        ProcessPanicRelation(old, "current thread has no process");

    same = (fromP == toP);
    if (same)
        gSame++;
    else
        gCross++;

    if (!same)
        ArchSwitchAddressSpace(next->addressSpace ?
                                       next->addressSpace :
                                       (toP ? toP->addressSpace : 0));

    expect = 0;
    if (next->addressSpace)
        expect = next->addressSpace->cr3;
    else if (toP && toP->addressSpace)
        expect = toP->addressSpace->cr3;
    actual = CpuReadCr3();
    if (expect && actual != expect)
        ProcessPanicAspace(toP, next, expect, actual);

    if (next->kind == RoboThreadUser)
        CpuWriteFsBase(next->userFsBase);
    else
        CpuWriteFsBase(0);

    if (!gSwitchLog)
        return;

    if (same) {
        kprintf("THREAD SWITCH\n\n");
        kprintf("PID : %llu\n",
                (unsigned long long)(toP ? toP->id : 0));
        kprintf("From TID : %llu\n",
                (unsigned long long)(old ? old->id : 0));
        kprintf("To TID   : %llu\n",
                (unsigned long long)next->id);
        kprintf("\nAddress Space : SAME\n");
        kprintf("CR3 Reload   : NO\n");
    } else {
        kprintf("PROCESS SWITCH\n\n");
        kprintf("From:\n");
        kprintf("  PID : %llu\n",
                (unsigned long long)(fromP ? fromP->id : 0));
        kprintf("  TID : %llu\n",
                (unsigned long long)(old ? old->id : 0));
        kprintf("  CR3 : 0x%llx\n",
                (unsigned long long)(old && old->addressSpace ?
                                     old->addressSpace->cr3 : 0));
        kprintf("\nTo:\n");
        kprintf("  PID : %llu\n",
                (unsigned long long)(toP ? toP->id : 0));
        kprintf("  TID : %llu\n",
                (unsigned long long)next->id);
        kprintf("  CR3 : 0x%llx\n",
                (unsigned long long)expect);
        kprintf("\nAddress Space : SWITCH\n");
    }
}
