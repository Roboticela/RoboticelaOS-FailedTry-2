#include "process/process.h"
#include "process/process_table.h"
#include "mm/vmm/paging.h"
#include "syscall/syscall_errors.h"
#include "syscall/syscall_numbers.h"

#ifdef PROCESS_HOST_TEST
#include <stdlib.h>
#include <stdio.h>
#define kmalloc malloc
#define kfree free
#define kprintf printf
static uint64_t SchedLock(void) { return 0; }
static void SchedUnlock(uint64_t flags) { (void)flags; }
static uint64_t TimeNow(void) { return 0; }
RoboAddressSpace *KernelAddressSpace(void)
{
    static RoboAddressSpace k;
    if (!k.cr3)
        k.cr3 = 0x1000;
    return &k;
}
static int HostAspaceClone(RoboAddressSpace *as)
{
    static PhysAddr next = 0x2000;
    if (!as)
        return 0;
    as->cr3 = next;
    next += 0x1000;
    as->cloned = 1;
    as->tablePages = 1;
    as->maps = 0;
    return 1;
}
static void HostAspaceDestroy(RoboAddressSpace *as)
{
    if (!as)
        return;
    as->cr3 = 0;
    as->cloned = 0;
}
RoboThread *CurrentThread(void);
static RoboThread *gHostCurrent;
RoboThread *CurrentThread(void) { return gHostCurrent; }
void HostSetCurrentThread(RoboThread *t) { gHostCurrent = t; }
RoboProcess *ThreadProcess(const RoboThread *thread)
{
    return thread ? thread->process : 0;
}
#else
#include "scheduler/sched_internal.h"
#include "mm/heap/heap.h"
#include "mm/vmm/vmm.h"
#include "time/time.h"
#include "kio.h"
#include "ipc/ipc.h"
#include "display/display.h"
#endif

static RoboProcess *gKernel;
static RoboProcess *gInit;
static uint64_t gCreated;
static uint64_t gDestroyed;
static uint64_t gLive;
static uint64_t gLiveMax;
static uint64_t gAspaceCreated;
static uint64_t gAspaceDestroyed;
static uint64_t gFiles;
static uint64_t gIpc;
static uint64_t gEnv;
static uint64_t gCreds;
static int gProcessReady;

static void ProcZero(void *dst, unsigned n)
{
    unsigned char *p = (unsigned char *)dst;
    while (n--)
        *p++ = 0;
}

static void ProcSetName(RoboProcess *p, const char *name)
{
    unsigned i;
    if (!name)
        name = "process";
    for (i = 0; i + 1u < RoboProcessNameMax && name[i]; i++)
        p->name[i] = name[i];
    p->name[i] = 0;
}

static int ProcSetState(RoboProcess *p, ProcessState next)
{
    if (!p)
        return 0;
    if (!ProcessTransitionAllowed(p->state, next))
        return 0;
    p->state = next;
    return 1;
}

static void LinkChild(RoboProcess *parent, RoboProcess *child)
{
    if (!parent || !child)
        return;
    child->parent = parent;
    child->siblingNext = parent->firstChild;
    parent->firstChild = child;
}

static void UnlinkChild(RoboProcess *parent, RoboProcess *child)
{
    RoboProcess **slot;
    if (!parent || !child)
        return;
    for (slot = &parent->firstChild; *slot; slot = &(*slot)->siblingNext) {
        if (*slot == child) {
            *slot = child->siblingNext;
            child->siblingNext = 0;
            child->parent = 0;
            return;
        }
    }
}

static void FreeResources(RoboProcess *p)
{
    if (!p)
        return;
    if (p->fdTable) {
        FdTableDestroy(p->fdTable);
        p->fdTable = 0;
        if (gFiles)
            gFiles--;
    }
    if (p->ipc) {
        IpcDestroy(p->ipc);
        p->ipc = 0;
        if (gIpc)
            gIpc--;
    }
    if (p->environment) {
        EnvDestroy(p->environment);
        p->environment = 0;
        if (gEnv)
            gEnv--;
    }
    if (p->credentials) {
        CredsDestroy(p->credentials);
        p->credentials = 0;
        if (gCreds)
            gCreds--;
    }
}

static void FreeAspace(RoboProcess *p)
{
    if (!p || !p->ownsAspace || !p->addressSpace)
        return;
#ifdef PROCESS_HOST_TEST
    HostAspaceDestroy(p->addressSpace);
#else
    VmmAspaceDestroy(p->addressSpace);
#endif
    kfree(p->addressSpace);
    p->addressSpace = 0;
    p->ownsAspace = 0;
    gAspaceDestroyed++;
}

static void ProcessFreePartial(RoboProcess *p)
{
    if (!p)
        return;
    FreeResources(p);
    FreeAspace(p);
    kfree(p);
}

static RoboProcess *ProcessCreateInternal(const char *name, int clone,
                                            int haveId, ProcessId reserved)
{
    RoboProcess *p;
    RoboProcess *parent;

    p = (RoboProcess *)kmalloc(sizeof(*p));
    if (!p)
        return 0;
    ProcZero(p, (unsigned)sizeof(*p));
    ProcSetName(p, name);
    p->state = RoboProcessNew;
    if (haveId) {
        p->id = reserved;
        if (PtableLookup(p->id))
            goto failProcess;
    } else {
        p->id = PtableAllocId();
        if (p->id == 0 || p->id == RoboProcessIdInit)
            goto failProcess;
    }

    if (clone) {
        p->addressSpace = (RoboAddressSpace *)kmalloc(sizeof(*p->addressSpace));
        if (!p->addressSpace)
            goto failProcess;
        ProcZero(p->addressSpace, (unsigned)sizeof(*p->addressSpace));
#ifdef PROCESS_HOST_TEST
        if (!HostAspaceClone(p->addressSpace))
            goto failAddressSpace;
#else
        if (!VmmAspaceClone(p->addressSpace))
            goto failAddressSpace;
#endif
        p->ownsAspace = 1;
        p->mmapNext = RoboUserMmapBase;
        gAspaceCreated++;
    } else {
        p->addressSpace = KernelAddressSpace();
        p->ownsAspace = 0;
    }

    p->fdTable = FdTableCreate();
    if (!p->fdTable)
        goto failAddressSpace;
    gFiles++;

    p->ipc = IpcCreate();
    if (!p->ipc)
        goto failFiles;
    gIpc++;

    p->environment = EnvCreate();
    if (!p->environment)
        goto failIpc;
    gEnv++;

    p->credentials = CredsCreate();
    if (!p->credentials)
        goto failEnvironment;
    gCreds++;

    p->createdAt = TimeNow();

    if (!PtableInsert(p))
        goto failCredentials;

    parent = CurrentProcess();
    if (parent && parent != p)
        LinkChild(parent, p);

    gCreated++;
    gLive++;
    if (gLive > gLiveMax)
        gLiveMax = gLive;
    return p;

failCredentials:
    CredsDestroy(p->credentials);
    p->credentials = 0;
    if (gCreds)
        gCreds--;
failEnvironment:
    EnvDestroy(p->environment);
    p->environment = 0;
    if (gEnv)
        gEnv--;
failIpc:
    IpcDestroy(p->ipc);
    p->ipc = 0;
    if (gIpc)
        gIpc--;
failFiles:
    FdTableDestroy(p->fdTable);
    p->fdTable = 0;
    if (gFiles)
        gFiles--;
failAddressSpace:
    if (p->addressSpace && p->addressSpace != KernelAddressSpace()) {
        if (p->ownsAspace) {
#ifdef PROCESS_HOST_TEST
            HostAspaceDestroy(p->addressSpace);
#else
            VmmAspaceDestroy(p->addressSpace);
#endif
            if (gAspaceCreated)
                gAspaceCreated--;
        }
        kfree(p->addressSpace);
        p->addressSpace = 0;
        p->ownsAspace = 0;
    }
failProcess:
    kfree(p);
    return 0;
}

int ProcessSubsystemInit(void)
{
    if (gProcessReady)
        return 1;
    PtableInit();
    gKernel = ProcessCreateInternal("kernel", 0, 1, RoboProcessIdKernel);
    if (!gKernel)
        return 0;
    if (gKernel->id != RoboProcessIdKernel)
        return 0;
    if (!ProcSetState(gKernel, RoboProcessRunning))
        gKernel->state = RoboProcessRunning;
    gProcessReady = 1;
#ifndef PROCESS_HOST_TEST
    kprintf("Process Subsystem\n");
    kprintf("-----------------\n");
    kprintf("[ OK ] Kernel process  PID %llu\n",
            (unsigned long long)gKernel->id);
    kprintf("[ OK ] Kernel address space  CR3 0x%llx\n",
            (unsigned long long)(gKernel->addressSpace ?
                                 gKernel->addressSpace->cr3 : 0));
    kprintf("[ OK ] Process table\n");
#endif
    return 1;
}

RoboProcess *KernelProcess(void)
{
    return gKernel;
}

RoboProcess *InitProcess(void)
{
    return gInit;
}

void ProcessSetInit(RoboProcess *process)
{
    gInit = process;
}

RoboProcess *ProcessCreateInit(void)
{
    RoboProcess *p;

    if (!gProcessReady || gInit)
        return 0;
    p = ProcessCreateInternal("init", 1, 1, RoboProcessIdInit);
    if (!p)
        return 0;
    if (!ProcSetState(p, RoboProcessRunning))
        p->state = RoboProcessRunning;
    return p;
}

RoboProcess *ProcessCreate(void)
{
    return ProcessCreateNamed("process");
}

RoboProcess *ProcessCreateNamed(const char *name)
{
    RoboProcess *p;

    if (!gProcessReady)
        return 0;
    p = ProcessCreateInternal(name, 1, 0, 0);
    if (!p)
        return 0;
    if (!ProcSetState(p, RoboProcessRunning))
        p->state = RoboProcessRunning;
    return p;
}

int ProcessAddThread(RoboProcess *process, RoboThread *thread)
{
    uint64_t flags;

    if (!process || !thread)
        return 0;
    if (process->state == RoboProcessTerminated)
        return 0;
    flags = SchedLock();
    if (thread->process && thread->process != process) {
        SchedUnlock(flags);
        return 0;
    }
    if (thread->process == process) {
        SchedUnlock(flags);
        return 1;
    }
    thread->process = process;
    thread->addressSpace = process->addressSpace;
    thread->procPrev = 0;
    thread->procNext = process->threads;
    if (process->threads)
        process->threads->procPrev = thread;
    process->threads = thread;
    process->threadCount++;
    if (process->state == RoboProcessNew)
        ProcSetState(process, RoboProcessRunning);
    SchedUnlock(flags);
    return 1;
}

int ProcessRemoveThread(RoboProcess *process, RoboThread *thread)
{
    uint64_t flags;
    RoboThread *toWake = 0;
    int last = 0;

    if (!process || !thread)
        return 0;
    flags = SchedLock();
    if (thread->process != process) {
        SchedUnlock(flags);
        return 0;
    }
    if (thread->procPrev)
        thread->procPrev->procNext = thread->procNext;
    else if (process->threads == thread)
        process->threads = thread->procNext;
    if (thread->procNext)
        thread->procNext->procPrev = thread->procPrev;
    thread->procNext = 0;
    thread->procPrev = 0;
    if (process->threadCount)
        process->threadCount--;
    if (process != gKernel &&
        process->threadCount == 0 &&
        process->state != RoboProcessTerminated) {
        if (!ProcSetState(process, RoboProcessTerminated))
            process->state = RoboProcessTerminated;
        process->exitTime = TimeNow();
    }
    if (process != gKernel && process->threadCount == 0 &&
        process->parent) {
        process->parent->waitGen++;
        if (process->parent->waiter) {
            toWake = process->parent->waiter;
            process->parent->waiter = 0;
        }
    }
    last = (process != gKernel && process->threadCount == 0);
    SchedUnlock(flags);
#ifndef PROCESS_HOST_TEST
    if (toWake)
        ThreadWake(toWake);
    if (last) {
        IpcOnProcessExit(process->id);
        DisplayOnProcessExit(process->id);
    }
#else
    (void)toWake;
#endif
    if (last)
        InitOnTerminated(process);
    return 1;
}

int ProcessDestroy(RoboProcess *process)
{
    RoboProcess *child, *next;
    RoboProcess *parent;

    if (!process || process == gKernel || process == gInit)
        return 0;
    if (process->threadCount != 0)
        return 0;

#ifndef PROCESS_HOST_TEST
    {
        RoboThread *t = ThreadAll();
        while (t) {
            RoboThread *n = t->allNext;
            if (t->process == process && t->state == RoboThreadTerminated)
                SchedReapLater(t);
            t = n;
        }
    }
#endif

    parent = process->parent;
    UnlinkChild(parent, process);

    child = process->firstChild;
    process->firstChild = 0;
    while (child) {
        next = child->siblingNext;
        child->siblingNext = 0;
        child->parent = 0;
        if (gInit && gInit != process && gInit != child)
            LinkChild(gInit, child);
        else if (parent)
            LinkChild(parent, child);
        else if (gKernel && gKernel != process)
            LinkChild(gKernel, child);
        child = next;
    }

    PtableRemove(process);
    FreeResources(process);
    FreeAspace(process);
    kfree(process);
    gDestroyed++;
    if (gLive)
        gLive--;
    return 1;
}

void ProcessExit(int status)
{
    RoboProcess *p;
#ifndef PROCESS_HOST_TEST
    RoboThread *t, *n, *self;
    uint64_t flags;
#endif

    p = CurrentProcess();
    if (!p || p == gKernel)
        return;
    p->exitStatus = status;
    if (p == gInit) {
        if (p->state != RoboProcessTerminated) {
            if (!ProcSetState(p, RoboProcessTerminated))
                p->state = RoboProcessTerminated;
        }
        p->exitTime = TimeNow();
        InitOnTerminated(p);
        return;
    }
    if (p->state != RoboProcessTerminated) {
        if (!ProcSetState(p, RoboProcessTerminated))
            p->state = RoboProcessTerminated;
    }
    p->exitTime = TimeNow();

#ifndef PROCESS_HOST_TEST
    flags = SchedLock();
    self = gCurrent;
    t = p->threads;
    while (t) {
        n = t->procNext;
        if (t != self) {
            if (t->sleepTimer)
                TimerCancel(t->sleepTimer);
            t->sleepTimer = 0;
            t->wakeDeadline = 0;
            if (t->state == RoboThreadReady)
                RqRemove(&gReady, t);
            t->state = RoboThreadTerminated;
            ProcessRemoveThread(p, t);
            SchedNoteTerm();
            SchedReapLater(t);
        }
        t = n;
    }
    SchedUnlock(flags);
    ThreadExit();
#endif
}

void InitOnTerminated(RoboProcess *process)
{
    if (!process || process != gInit)
        return;
#ifndef PROCESS_HOST_TEST
    kprintf("========================================\n");
    kprintf("          KERNEL PANIC\n");
    kprintf("========================================\n\n");
    kprintf("INIT PROCESS EXITED\n\n");
    kprintf("PID 1 terminated unexpectedly.\n");
    kprintf("Status  : %d\n", process->exitStatus);
    kprintf("\n========================================\n");
    for (;;)
        __asm__ volatile ("cli; hlt");
#endif
}

static RoboProcess *WaitMatch(RoboProcess *parent, ProcessId pid)
{
    RoboProcess *c;

    if (!parent)
        return 0;
    for (c = parent->firstChild; c; c = c->siblingNext) {
        if (c->state != RoboProcessTerminated || c->threadCount != 0)
            continue;
        if (pid == 0 || c->id == pid)
            return c;
    }
    return 0;
}

static int WaitHasChild(RoboProcess *parent, ProcessId pid)
{
    RoboProcess *c;

    if (!parent)
        return 0;
    for (c = parent->firstChild; c; c = c->siblingNext) {
        if (pid == 0 || c->id == pid)
            return 1;
    }
    return 0;
}

int ProcessWait(ProcessId pid, uint32_t flags, int *status,
                      ProcessId *outPid)
{
    RoboProcess *parent;
    RoboProcess *child;
#ifndef PROCESS_HOST_TEST
    RoboThread *self;
#endif

    parent = CurrentProcess();
    if (!parent)
        return -RoboEperm;
    if (flags & ~RoboWaitNohang)
        return -RoboEinval;

    for (;;) {
        uint64_t gen;

        child = WaitMatch(parent, pid);
        if (child) {
            if (status)
                *status = child->exitStatus;
            if (outPid)
                *outPid = child->id;
            if (!ProcessDestroy(child))
                return -RoboEbusy;
            return 0;
        }
        if (!WaitHasChild(parent, pid))
            return -RoboEchild;
        if (flags & RoboWaitNohang)
            return -RoboEagain;
#ifdef PROCESS_HOST_TEST
        return -RoboEagain;
#else
        self = CurrentThread();
        if (!self)
            return -RoboEbusy;
        gen = parent->waitGen;
        parent->waiter = self;
        if (parent->waitGen != gen || WaitMatch(parent, pid)) {
            parent->waiter = 0;
            continue;
        }
        ThreadBlock();
        parent->waiter = 0;
#endif
    }
}

RoboProcess *CurrentProcess(void)
{
    RoboThread *t = CurrentThread();
    if (t && t->process)
        return t->process;
    return gKernel;
}

ProcessId GetProcessId(const RoboProcess *process)
{
    return process ? process->id : 0;
}

RoboProcess *ProcessLookup(ProcessId pid)
{
    return PtableLookup(pid);
}

int ProcessFdAlloc(RoboProcess *process, RoboFile *file)
{
    return (int)FdAlloc(process, file, 0);
}

int ProcessFdClose(RoboProcess *process, int fd)
{
    return FdClose(process, (Fd)fd) == 0;
}

RoboFile *ProcessFdGet(RoboProcess *process, int fd)
{
    return FdGet(process, (Fd)fd);
}

int ProcessFdInUse(RoboProcess *process, int fd)
{
    return FdInUse(process, (Fd)fd);
}

uint64_t ProcessStatCreated(void) { return gCreated; }
uint64_t ProcessStatDestroyed(void) { return gDestroyed; }
uint64_t ProcessStatLive(void) { return gLive; }
uint64_t ProcessStatLiveMax(void) { return gLiveMax; }
uint64_t ProcessStatAspaceCreated(void) { return gAspaceCreated; }
uint64_t ProcessStatAspaceDestroyed(void) { return gAspaceDestroyed; }
uint64_t ProcessStatFiles(void) { return gFiles; }
uint64_t ProcessStatIpc(void) { return gIpc; }
uint64_t ProcessStatEnv(void) { return gEnv; }
uint64_t ProcessStatCreds(void) { return gCreds; }
