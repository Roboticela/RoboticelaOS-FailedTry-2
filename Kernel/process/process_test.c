#include "process/process.h"
#include "scheduler/scheduler.h"
#include "thread/thread.h"
#include "mm/vmm/vmm.h"
#include "mm/pmm.h"
#include "mm/heap/heap.h"
#include "mm/page.h"
#include "arch/x86_64/mm/paging_cpu.h"
#include "time/time.h"
#include "time/convert.h"
#include "kio.h"
#include "fs/backends/console.h"
#include "fs/file.h"

#define PROC_AB_TARGET     10000ull
#define PROC_STRESS_PROCS  10u
#define PROC_STRESS_EACH   2u
#define PROC_STRESS_TARGET 2000ull
#define PROC_SHARED_VALUE  12345ull
#define PROC_ISOL_A        0xA1A1A1A1A1A1A1A1ull
#define PROC_ISOL_B        0xB2B2B2B2B2B2B2B2ull

static volatile uint64_t gA1, gA2, gB1, gB2;
static volatile int gA1Done, gA2Done, gB1Done, gB2Done;
static volatile uint64_t gA1Pid, gA2Pid, gB1Pid, gB2Pid;
static volatile uint64_t gA1Cr3, gA2Cr3, gB1Cr3, gB2Cr3;
static volatile int gIdentFail;
static volatile int gCr3Fail;
static volatile uint64_t gSharedRead;
static volatile int gSharedWrote;
static volatile int gSharedDoneA, gSharedDoneB;
static volatile uint64_t gIsolA, gIsolB;
static volatile uint64_t gIsolCr3A, gIsolCr3B;
static volatile int gIsolDoneA, gIsolDoneB;
static volatile int gHold[4];
static volatile int gHoldDone[4];
static volatile uint64_t gStress[PROC_STRESS_PROCS * PROC_STRESS_EACH];
static volatile int gStressDone[PROC_STRESS_PROCS * PROC_STRESS_EACH];

static int tfail(const char *tag)
{
    kprintf("Process selftest: %s\n", tag);
    return 0;
}

static void stretch(void)
{
    uint64_t t = TimeNow();
    while (TimeNow() - t < (RoboNsPerMs / 4ull))
        __asm__ volatile ("pause");
}

static void CountLoop(volatile uint64_t *counter, uint64_t target)
{
    uint64_t i;
    for (i = 0; i < target; i++) {
        (*counter)++;
        if ((i & 127ull) == 0)
            stretch();
    }
}

static void NoteIdentity(void)
{
    RoboThread *t = CurrentThread();
    RoboProcess *p = CurrentProcess();

    if (!t || !p || p != t->process || p != ThreadProcess(t))
        gIdentFail++;
    if (!p->addressSpace || CpuReadCr3() != p->addressSpace->cr3)
        gCr3Fail++;
}

static int WaitFlag(volatile int *flag, uint64_t timeoutMs)
{
    uint64_t start = TimeMs();
    while (!*flag) {
        if (TimeMs() - start > timeoutMs)
            return 0;
        RoboSleep(10);
    }
    return 1;
}

static int WaitFlags(volatile int *flags, unsigned n, uint64_t timeoutMs)
{
    uint64_t start = TimeMs();
    unsigned i;
    for (;;) {
        int done = 1;
        for (i = 0; i < n; i++) {
            if (!flags[i])
                done = 0;
        }
        if (done)
            return 1;
        if (TimeMs() - start > timeoutMs)
            return 0;
        RoboSleep(10);
    }
}

static int MapPrivate(RoboProcess *p, uint64_t value)
{
    PhysAddr pa;

    if (!p || !p->addressSpace)
        return 0;
    pa = PageAlloc();
    if (pa == RoboInvalidPhysAddr)
        return 0;
    *(volatile uint64_t *)(uintptr_t)pa = value;
    if (!VmmAspaceMap(p->addressSpace, RoboVmmProcBase, pa,
                             RoboVmRead | RoboVmWrite | RoboVmKernel)) {
        PageFree(pa);
        return 0;
    }
    return 1;
}

static void WorkerA1(void *arg)
{
    (void)arg;
    NoteIdentity();
    gA1Pid = GetProcessId(CurrentProcess());
    gA1Cr3 = CpuReadCr3();
    CountLoop(&gA1, PROC_AB_TARGET);
    gA1Done = 1;
}

static void WorkerA2(void *arg)
{
    (void)arg;
    NoteIdentity();
    gA2Pid = GetProcessId(CurrentProcess());
    gA2Cr3 = CpuReadCr3();
    CountLoop(&gA2, PROC_AB_TARGET);
    gA2Done = 1;
}

static void WorkerB1(void *arg)
{
    (void)arg;
    NoteIdentity();
    gB1Pid = GetProcessId(CurrentProcess());
    gB1Cr3 = CpuReadCr3();
    CountLoop(&gB1, PROC_AB_TARGET);
    gB1Done = 1;
}

static void WorkerB2(void *arg)
{
    (void)arg;
    NoteIdentity();
    gB2Pid = GetProcessId(CurrentProcess());
    gB2Cr3 = CpuReadCr3();
    CountLoop(&gB2, PROC_AB_TARGET);
    gB2Done = 1;
}

static void SharedA1(void *arg)
{
    volatile uint64_t *cell = (volatile uint64_t *)RoboVmmProcBase;
    (void)arg;
    NoteIdentity();
    *cell = PROC_SHARED_VALUE;
    gSharedWrote = 1;
    gSharedDoneA = 1;
}

static void SharedA2(void *arg)
{
    volatile uint64_t *cell = (volatile uint64_t *)RoboVmmProcBase;
    uint64_t start;
    (void)arg;
    NoteIdentity();
    start = TimeMs();
    while (!gSharedWrote) {
        if (TimeMs() - start > 4000ull)
            break;
        stretch();
    }
    gSharedRead = *cell;
    gSharedDoneB = 1;
}

static void IsolA(void *arg)
{
    volatile uint64_t *cell = (volatile uint64_t *)RoboVmmProcBase;
    (void)arg;
    NoteIdentity();
    gIsolA = *cell;
    gIsolCr3A = CpuReadCr3();
    gIsolDoneA = 1;
}

static void IsolB(void *arg)
{
    volatile uint64_t *cell = (volatile uint64_t *)RoboVmmProcBase;
    (void)arg;
    NoteIdentity();
    gIsolB = *cell;
    gIsolCr3B = CpuReadCr3();
    gIsolDoneB = 1;
}

static void holder(void *arg)
{
    unsigned which = (unsigned)(uintptr_t)arg;
    NoteIdentity();
    while (gHold[which])
        stretch();
    gHoldDone[which] = 1;
}

static void StressEntry(void *arg)
{
    unsigned idx = (unsigned)(uintptr_t)arg;
    NoteIdentity();
    CountLoop(&gStress[idx], PROC_STRESS_TARGET);
    gStressDone[idx] = 1;
}

static int ResourcesIndependent(RoboProcess *a, RoboProcess *b)
{
    if (!a || !b)
        return 0;
    if (a->fdTable == b->fdTable || a->ipc == b->ipc)
        return 0;
    if (a->environment == b->environment || a->credentials == b->credentials)
        return 0;
    if (a->addressSpace == b->addressSpace)
        return 0;
    return 1;
}

int ProcessSelftest(void)
{
    RoboProcess *a, *b, *p[4], *own;
    RoboThread *ta1, *ta2, *tb1, *tb2;
    RoboThread *h[3];
    RoboProcess *stP[PROC_STRESS_PROCS];
    PhysAddr paA, paB, paA2, paB2;
    uint64_t pidA, pidB, tidA1, tidA2, tidB1, tidB2;
    uint64_t cr3A, cr3B;
    uint64_t sw0, sw1, same0, same1, cross0, cross1, rel0, rel1;
    uint64_t heap0, heap1;
    size_t free0, free1;
    unsigned i, round, idx;
    int fd;
    int passCreate = 0, passOwn = 0, passShare = 0, passIsol = 0;
    int passSameCr3 = 0, passCrossCr3 = 0, passLife = 0, passStress = 0;
    int leaks = 0;

    kprintf("========================================\n");
    kprintf("        PROCESS TEST\n");
    kprintf("========================================\n\n");

    if (!KernelProcess())
        return tfail("kernel process");
    if (CurrentProcess() != CurrentThread()->process)
        return tfail("current identity");

    heap0 = (uint64_t)HeapUsedBytes();
    free0 = FreePageCount();

    /* --- creation / unique PID / independent resources --- */
    for (i = 0; i < 4; i++) {
        p[i] = ProcessCreateNamed("probe");
        if (!p[i] || p[i]->id == 0)
            return tfail("create probe");
        if (!p[i]->addressSpace || !p[i]->fdTable || !p[i]->ipc ||
            !p[i]->environment || !p[i]->credentials)
            return tfail("resources");
        if (!ProcessLookup(p[i]->id))
            return tfail("table insert");
        if (p[i]->parent != KernelProcess())
            return tfail("parent");
    }
    for (i = 0; i < 4; i++) {
        unsigned j;
        for (j = i + 1; j < 4; j++) {
            if (p[i]->id == p[j]->id)
                return tfail("duplicate pid");
            if (!ResourcesIndependent(p[i], p[j]))
                return tfail("shared resources");
        }
    }
    {
        RoboFile *tf = ConsoleOpen(RoboFflagWrite);
        RoboFile *got;
        if (!tf)
            return tfail("fd file");
        fd = ProcessFdAlloc(p[0], tf);
        if (fd < 3)
            return tfail("fd alloc");
        got = ProcessFdGet(p[0], fd);
        if (got != tf)
            return tfail("fd get");
        FilePut(got);
        if (!ProcessFdClose(p[0], fd))
            return tfail("fd close");
        FilePut(tf);
    }
    for (i = 0; i < 4; i++) {
        ProcessId gone = p[i]->id;
        if (!ProcessDestroy(p[i]))
            return tfail("destroy probe");
        if (ProcessLookup(gone))
            return tfail("stale pid");
    }
    passCreate = 1;
    kprintf("[PASS] Process creation\n");

    /* --- thread ownership --- */
    own = ProcessCreateNamed("owner");
    if (!own)
        return tfail("owner create");
    gHold[0] = gHold[1] = gHold[2] = 1;
    gHoldDone[0] = gHoldDone[1] = gHoldDone[2] = 0;
    h[0] = ThreadCreateInProcess(own, "own-0", holder, (void *)0);
    h[1] = ThreadCreateInProcess(own, "own-1", holder, (void *)1);
    h[2] = ThreadCreateInProcess(own, "own-2", holder, (void *)2);
    if (!h[0] || !h[1] || !h[2])
        return tfail("owner threads");
    RoboSleep(50);
    if (own->threadCount != 3)
        return tfail("thread_count 3");
    if (ThreadProcess(h[0]) != own ||
        ThreadProcess(h[1]) != own ||
        ThreadProcess(h[2]) != own)
        return tfail("thread owner");
    gHold[2] = 0;
    if (!WaitFlag(&gHoldDone[2], 4000))
        return tfail("owner thread 2 exit");
    RoboSleep(20);
    SchedReap();
    if (own->threadCount != 2)
        return tfail("thread_count 2");
    gHold[0] = 0;
    gHold[1] = 0;
    if (!WaitFlag(&gHoldDone[0], 4000) || !WaitFlag(&gHoldDone[1], 4000))
        return tfail("owner drain");
    RoboSleep(20);
    SchedReap();
    if (own->threadCount != 0)
        return tfail("thread_count 0");
    if (!ProcessDestroy(own))
        return tfail("owner destroy");
    passOwn = 1;
    kprintf("[PASS] Thread ownership\n");

    /* --- shared address space --- */
    a = ProcessCreateNamed("share-a");
    if (!a || !MapPrivate(a, 0))
        return tfail("share map");
    gSharedWrote = 0;
    gSharedRead = 0;
    gSharedDoneA = 0;
    gSharedDoneB = 0;
    if (!ThreadCreateInProcess(a, "sh-a1", SharedA1, 0) ||
        !ThreadCreateInProcess(a, "sh-a2", SharedA2, 0))
        return tfail("share threads");
    if (!WaitFlag(&gSharedDoneA, 4000) || !WaitFlag(&gSharedDoneB, 4000))
        return tfail("share wait");
    kprintf("A1 writes shared_value = %llu\n", (unsigned long long)PROC_SHARED_VALUE);
    kprintf("A2 reads shared_value = %llu\n", (unsigned long long)gSharedRead);
    if (gSharedRead != PROC_SHARED_VALUE)
        return tfail("shared value");
    RoboSleep(20);
    SchedReap();
    if (!ProcessDestroy(a))
        return tfail("share destroy");
    passShare = 1;
    kprintf("[PASS] Shared address space\n");

    /* --- isolation + CR3 --- */
    a = ProcessCreateNamed("isol-a");
    b = ProcessCreateNamed("isol-b");
    if (!a || !b || !MapPrivate(a, PROC_ISOL_A) || !MapPrivate(b, PROC_ISOL_B))
        return tfail("isol map");
    if (!VmmAspaceTranslate(a->addressSpace, RoboVmmProcBase, &paA) ||
        !VmmAspaceTranslate(b->addressSpace, RoboVmmProcBase, &paB))
        return tfail("isol translate");
    if (paA == paB)
        return tfail("isol same phys");
    if (a->addressSpace->cr3 == b->addressSpace->cr3)
        return tfail("isol same cr3");
    cr3A = a->addressSpace->cr3;
    cr3B = b->addressSpace->cr3;
    gIsolDoneA = 0;
    gIsolDoneB = 0;
    if (!ThreadCreateInProcess(a, "isol-ta", IsolA, 0) ||
        !ThreadCreateInProcess(b, "isol-tb", IsolB, 0))
        return tfail("isol threads");
    if (!WaitFlag(&gIsolDoneA, 4000) || !WaitFlag(&gIsolDoneB, 4000))
        return tfail("isol wait");
    if (gIsolA != PROC_ISOL_A || gIsolB != PROC_ISOL_B)
        return tfail("isol values");
    if (gIsolCr3A != cr3A || gIsolCr3B != cr3B)
        return tfail("isol thread cr3");
    if (!VmmAspaceTranslate(a->addressSpace, RoboVmmProcBase, &paA2) ||
        !VmmAspaceTranslate(b->addressSpace, RoboVmmProcBase, &paB2))
        return tfail("isol retranslate");
    if (paA2 != paA || paB2 != paB)
        return tfail("isol pte drift");
    kprintf("A mapping != B mapping\n");
    kprintf("A page table != B page table\n");
    kprintf("A CR3 : 0x%llx\n", (unsigned long long)cr3A);
    kprintf("B CR3 : 0x%llx\n", (unsigned long long)cr3B);
    RoboSleep(20);
    SchedReap();
    if (!ProcessDestroy(a) || !ProcessDestroy(b))
        return tfail("isol destroy");
    passIsol = 1;
    kprintf("[PASS] Address isolation\n");
    kprintf("Limitation: ring-3 protection is not tested (no user mode yet).\n");
    kprintf("Isolation is demonstrated by distinct CR3 roots, distinct PTEs\n");
    kprintf("for the same VA, and each thread reading only its own mapping.\n");

    /* --- mandatory four-thread scheduler test --- */
    a = ProcessCreateNamed("proc-a");
    b = ProcessCreateNamed("proc-b");
    if (!a || !b)
        return tfail("ab create");
    pidA = a->id;
    pidB = b->id;
    cr3A = a->addressSpace->cr3;
    cr3B = b->addressSpace->cr3;
    gA1 = gA2 = gB1 = gB2 = 0;
    gA1Done = gA2Done = gB1Done = gB2Done = 0;
    same0 = AspaceStatSame();
    cross0 = AspaceStatCross();
    rel0 = ArchCr3Reloads();
    sw0 = SchedSwitches();
    ta1 = ThreadCreateInProcess(a, "A1", WorkerA1, 0);
    ta2 = ThreadCreateInProcess(a, "A2", WorkerA2, 0);
    tb1 = ThreadCreateInProcess(b, "B1", WorkerB1, 0);
    tb2 = ThreadCreateInProcess(b, "B2", WorkerB2, 0);
    if (!ta1 || !ta2 || !tb1 || !tb2)
        return tfail("ab threads");
    tidA1 = ta1->id;
    tidA2 = ta2->id;
    tidB1 = tb1->id;
    tidB2 = tb2->id;
    {
        uint64_t start = TimeMs();
        while (gA1 < PROC_AB_TARGET || gA2 < PROC_AB_TARGET ||
               gB1 < PROC_AB_TARGET || gB2 < PROC_AB_TARGET) {
            if (TimeMs() - start > 20000ull)
                return tfail("ab timeout");
            RoboSleep(10);
        }
    }
    if (!WaitFlag(&gA1Done, 4000) || !WaitFlag(&gA2Done, 4000) ||
        !WaitFlag(&gB1Done, 4000) || !WaitFlag(&gB2Done, 4000))
        return tfail("ab term");
    RoboSleep(20);
    sw1 = SchedSwitches();
    same1 = AspaceStatSame();
    cross1 = AspaceStatCross();
    rel1 = ArchCr3Reloads();

    kprintf("Process A PID : %llu\n", (unsigned long long)pidA);
    kprintf("Process B PID : %llu\n", (unsigned long long)pidB);
    kprintf("\nA1 TID : %llu\n", (unsigned long long)tidA1);
    kprintf("A2 TID : %llu\n", (unsigned long long)tidA2);
    kprintf("\nB1 TID : %llu\n", (unsigned long long)tidB1);
    kprintf("B2 TID : %llu\n", (unsigned long long)tidB2);
    kprintf("\nA1 result : %llu\n", (unsigned long long)gA1);
    kprintf("A2 result : %llu\n", (unsigned long long)gA2);
    kprintf("B1 result : %llu\n", (unsigned long long)gB1);
    kprintf("B2 result : %llu\n", (unsigned long long)gB2);
    kprintf("\nA1 process : %llu\n", (unsigned long long)gA1Pid);
    kprintf("A2 process : %llu\n", (unsigned long long)gA2Pid);
    kprintf("B1 process : %llu\n", (unsigned long long)gB1Pid);
    kprintf("B2 process : %llu\n", (unsigned long long)gB2Pid);
    kprintf("\nA address space : 0x%llx\n", (unsigned long long)cr3A);
    kprintf("B address space : 0x%llx\n", (unsigned long long)cr3B);
    kprintf("\nSame-process address space : %s\n",
            (gA1Cr3 == gA2Cr3 && gA1Cr3 == cr3A) ? "YES" : "NO");
    kprintf("Cross-process address space : %s\n",
            (cr3A != cr3B && gA1Cr3 != gB1Cr3) ? "NO" : "YES");
    kprintf("\nContext switches : %llu\n", (unsigned long long)(sw1 - sw0));
    kprintf("Address-space switches : %llu\n",
            (unsigned long long)(cross1 - cross0));

    if (gA1 != PROC_AB_TARGET || gA2 != PROC_AB_TARGET ||
        gB1 != PROC_AB_TARGET || gB2 != PROC_AB_TARGET)
        return tfail("ab counts");
    if (gA1Pid != pidA || gA2Pid != pidA ||
        gB1Pid != pidB || gB2Pid != pidB)
        return tfail("ab pids");
    if (gA1Cr3 != cr3A || gA2Cr3 != cr3A ||
        gB1Cr3 != cr3B || gB2Cr3 != cr3B)
        return tfail("ab cr3");
    if (cr3A == cr3B)
        return tfail("ab aspace equal");
    if ((same1 - same0) == 0)
        return tfail("no same-process switch");
    if ((cross1 - cross0) == 0)
        return tfail("no cross-process switch");
    if ((rel1 - rel0) != (cross1 - cross0))
        return tfail("unexpected cr3 reload");
    if ((rel1 - rel0) == 0)
        return tfail("no cr3 reload");

    kprintf("\nAddress Space : SAME (A1 -> A2)\n");
    kprintf("CR3 Reload    : NO\n");
    kprintf("Address Space : CHANGED (A -> B)\n");
    kprintf("CR3 Reload    : YES\n");
    passSameCr3 = 1;
    passCrossCr3 = 1;
    kprintf("[PASS] Same-process CR3\n");
    kprintf("[PASS] Cross-process CR3\n");

    ProcessDebugDump(a);
    SchedReap();
    if (!ProcessDestroy(a) || !ProcessDestroy(b))
        return tfail("ab destroy");
    kprintf("[PASS] Process subsystem\n");
    kprintf("========================================\n");

    /* --- stress --- */
    for (round = 0; round < 2u; round++) {
        for (i = 0; i < PROC_STRESS_PROCS; i++) {
            stP[i] = ProcessCreateNamed("stress");
            if (!stP[i])
                return tfail("stress process");
            for (idx = 0; idx < PROC_STRESS_EACH; idx++) {
                unsigned slot = i * PROC_STRESS_EACH + idx;
                gStress[slot] = 0;
                gStressDone[slot] = 0;
                if (!ThreadCreateInProcess(stP[i], "st",
                                                   StressEntry,
                                                   (void *)(uintptr_t)slot))
                    return tfail("stress thread");
            }
        }
        if (!WaitFlags(gStressDone, PROC_STRESS_PROCS * PROC_STRESS_EACH, 20000ull))
            return tfail("stress wait");
        for (i = 0; i < PROC_STRESS_PROCS * PROC_STRESS_EACH; i++) {
            if (gStress[i] != PROC_STRESS_TARGET)
                return tfail("stress count");
        }
        RoboSleep(20);
        SchedReap();
        for (i = 0; i < PROC_STRESS_PROCS; i++) {
            if (stP[i]->threadCount != 0)
                return tfail("stress live threads");
            if (!ProcessDestroy(stP[i]))
                return tfail("stress destroy");
        }
    }
    passStress = 1;
    kprintf("[PASS] Stress test\n");

    heap1 = (uint64_t)HeapUsedBytes();
    free1 = FreePageCount();
    (void)free0;
    (void)free1;
    if (heap1 > heap0)
        leaks = 1;
    if (ProcessStatAspaceCreated() != ProcessStatAspaceDestroyed())
        leaks = 1;
    if (ProcessStatLive() != 1)
        return tfail("live != kernel");
    passLife = !leaks;
    if (!passLife)
        return tfail("lifecycle leak");
    kprintf("[PASS] Lifecycle\n");

    if (gIdentFail)
        return tfail("identity");
    if (gCr3Fail)
        return tfail("running cr3");

    kprintf("\n========================================\n");
    kprintf("     ROBOTICELA OS PHASE 9\n");
    kprintf("       PROCESS VERIFICATION\n");
    kprintf("========================================\n\n");
    kprintf("Architecture : x86_64\n\n");
    kprintf("Processes\n");
    kprintf("---------\n");
    kprintf("Created              : %llu\n",
            (unsigned long long)ProcessStatCreated());
    kprintf("Destroyed            : %llu\n",
            (unsigned long long)ProcessStatDestroyed());
    kprintf("Maximum Concurrent   : %llu\n\n",
            (unsigned long long)ProcessStatLiveMax());
    kprintf("Threads\n");
    kprintf("-------\n");
    kprintf("Created              : %llu\n",
            (unsigned long long)SchedCreates());
    kprintf("Terminated           : %llu\n",
            (unsigned long long)SchedTerminations());
    kprintf("Maximum Concurrent   : NOT TESTED — REASON no peak tracker\n\n");
    kprintf("Address Spaces\n");
    kprintf("--------------\n");
    kprintf("Created              : %llu\n",
            (unsigned long long)ProcessStatAspaceCreated());
    kprintf("Destroyed            : %llu\n",
            (unsigned long long)ProcessStatAspaceDestroyed());
    kprintf("Switches             : %llu\n",
            (unsigned long long)(AspaceStatSame() + AspaceStatCross()));
    kprintf("Same-Process Switches: %llu\n",
            (unsigned long long)AspaceStatSame());
    kprintf("Cross-Process Switches: %llu\n\n",
            (unsigned long long)AspaceStatCross());
    kprintf("CR3\n");
    kprintf("---\n");
    kprintf("Reloads              : %llu\n\n",
            (unsigned long long)ArchCr3Reloads());
    kprintf("Resources\n");
    kprintf("---------\n");
    kprintf("File Tables           : %llu\n",
            (unsigned long long)ProcessStatFiles());
    kprintf("IPC Containers        : %llu\n",
            (unsigned long long)ProcessStatIpc());
    kprintf("Environments          : %llu\n",
            (unsigned long long)ProcessStatEnv());
    kprintf("Credentials           : %llu\n\n",
            (unsigned long long)ProcessStatCreds());
    kprintf("Memory\n");
    kprintf("------\n");
    kprintf("Process Objects       : %llu live\n",
            (unsigned long long)ProcessStatLive());
    kprintf("Address-Space Pages   : cloned %llu / destroyed %llu\n",
            (unsigned long long)ProcessStatAspaceCreated(),
            (unsigned long long)ProcessStatAspaceDestroyed());
    kprintf("Kernel Stacks         : reaped with threads\n");
    kprintf("Leaks Detected        : %s\n\n", leaks ? "YES" : "NO");
    kprintf("Tests\n");
    kprintf("-----\n");
    kprintf("Process Creation      : %s\n", passCreate ? "PASS" : "FAIL");
    kprintf("Thread Ownership      : %s\n", passOwn ? "PASS" : "FAIL");
    kprintf("Shared Address Space  : %s\n", passShare ? "PASS" : "FAIL");
    kprintf("Address Isolation     : %s\n", passIsol ? "PASS" : "FAIL");
    kprintf("Same-Process CR3      : %s\n", passSameCr3 ? "PASS" : "FAIL");
    kprintf("Cross-Process CR3     : %s\n", passCrossCr3 ? "PASS" : "FAIL");
    kprintf("Lifecycle             : %s\n", passLife ? "PASS" : "FAIL");
    kprintf("Stress Test           : %s\n", passStress ? "PASS" : "FAIL");
    kprintf("\nBuild\n");
    kprintf("-----\n");
    kprintf("Clean Build           : NOT TESTED — REASON reported by host build\n");
    kprintf("QEMU Boot             : NOT TESTED — REASON reported by QEMU script\n");
    kprintf("\n========================================\n");
    kprintf("[PASS] ROBOTICELA OS PHASE 9\n");
    kprintf("========================================\n");
    return 1;
}
