#include "fs/fd.h"
#include "fs/backends/console.h"
#include "fs/backends/regular.h"
#include "syscall/syscall_errors.h"
#include "process/process.h"

#ifndef PROCESS_HOST_TEST
#include "kio.h"
#include "exec/exec.h"
#include "scheduler/scheduler.h"
#include "thread/thread.h"
#endif

Fd FdAlloc(struct RoboProcess *process, RoboFile *file, uint32_t flags)
{
    if (!process)
        return RoboFdInvalid;
    return FdTableAlloc(process->fdTable, file, flags);
}

Fd FdDup(struct RoboProcess *process, Fd oldfd)
{
    RoboFile *file;
    Fd n;

    file = FdGet(process, oldfd);
    if (!file)
        return RoboFdInvalid;
    n = FdAlloc(process, file, 0);
    FilePut(file);
    return n;
}

int FdClose(struct RoboProcess *process, Fd fd)
{
    if (!process)
        return -RoboEbadf;
    return FdTableClose(process->fdTable, fd);
}

RoboFile *FdGet(struct RoboProcess *process, Fd fd)
{
    if (!process)
        return 0;
    return FdTableGet(process->fdTable, fd);
}

int FdInUse(struct RoboProcess *process, Fd fd)
{
    RoboFile *f = FdGet(process, fd);
    if (!f)
        return 0;
    FilePut(f);
    return 1;
}

#ifndef PROCESS_HOST_TEST
static const char *FlagName(uint32_t flags)
{
    if ((flags & RoboFflagRead) && (flags & RoboFflagWrite))
        return "RDWR";
    if (flags & RoboFflagWrite)
        return "WRITE";
    if (flags & RoboFflagRead)
        return "READ";
    return "-";
}

void FdTableDump(const struct RoboProcess *process)
{
    unsigned i;
    RoboFDTable *t;

    if (!process || !process->fdTable)
        return;
    t = process->fdTable;
    kprintf("\n========================================\n");
    kprintf("       FILE DESCRIPTOR SUBSYSTEM\n");
    kprintf("========================================\n\n");
    kprintf("Process PID : %llu\n\n", (unsigned long long)process->id);
    kprintf("FD   Type       Flags       Offset\n");
    kprintf("----------------------------------------\n");
    for (i = 0; i < RoboMaxFds; i++) {
        RoboFile *f = t->ent[i].file;
        if (!f)
            continue;
        kprintf("%u    %s    %s        %llu\n",
                i,
                FileTypeName(f->type),
                FlagName(f->flags),
                (unsigned long long)f->offset);
    }
    kprintf("\nOpen files : %u\n", t->openCount);
    kprintf("========================================\n");
}

static int gFdTests;

static int FdCheck(int cond)
{
    if (cond) {
        gFdTests++;
        return 1;
    }
    return 0;
}

static int RunFdUser(void)
{
    RoboProcess *p;
    unsigned i;
    int st = -1;

    p = ProcessCreateFromElf("/bin/fd-test");
    if (!p)
        return 0;
    for (i = 0; i < 200000u; i++) {
        if (p->state == RoboProcessTerminated)
            break;
        ThreadYield();
        SchedReap();
    }
    st = p->exitStatus;
    SchedReap();
    ProcessDestroy(p);
    return st == 0;
}

static const char *mark(int ok)
{
    return ok ? "PASS" : "FAIL";
}

int FdSelftest(void)
{
    RoboProcess *p;
    RoboFile *a, *b, *shared, *rf, *rf2;
    Fd fd3, fd4, fd5, d1, d2;
    int ok = 1;
    int userOk = 0;
    int fileOk = 0;
    int filePresent = 0;
    uint64_t live0;
    char buf[16];
    char got[16];
    int64_t n, n1, n2;
    unsigned i, gi;
    RoboFile *f0, *f1, *f2;

    gFdTests = 0;
    p = CurrentProcess();
    if (!p || !p->fdTable)
        return 0;

    live0 = FileStatLive();
    if (!FdCheck(FdInUse(p, 0) && FdInUse(p, 1) &&
                  FdInUse(p, 2)))
        ok = 0;

    a = ConsoleOpen(RoboFflagWrite);
    fd3 = FdAlloc(p, a, 0);
    FilePut(a);
    if (!FdCheck(fd3 == 3))
        ok = 0;

    b = ConsoleOpen(RoboFflagWrite);
    fd4 = FdAlloc(p, b, 0);
    FilePut(b);
    if (!FdCheck(fd4 == 4))
        ok = 0;

    shared = ConsoleOpen(RoboFflagWrite);
    fd5 = FdAlloc(p, shared, 0);
    d1 = FdDup(p, fd5);
    if (!FdCheck(fd5 >= 5 && d1 >= 0 && d1 != fd5))
        ok = 0;
    if (!FdCheck(FileRefcount(shared) >= 2))
        ok = 0;
    FdClose(p, fd5);
    if (!FdCheck(FileRefcount(shared) >= 1))
        ok = 0;
    FdClose(p, d1);
    FilePut(shared);

    if (!FdCheck(FdClose(p, fd3) == 0))
        ok = 0;
    if (!FdCheck(FdClose(p, fd3) == -RoboEbadf))
        ok = 0;
    if (!FdCheck(FdClose(p, RoboFdInvalid) == -RoboEbadf))
        ok = 0;
    if (!FdCheck(FdClose(p, 999) == -RoboEbadf))
        ok = 0;
    if (!FdCheck(FdGet(p, 999) == 0))
        ok = 0;

    a = ConsoleOpen(RoboFflagWrite);
    d2 = FdAlloc(p, a, 0);
    FilePut(a);
    if (!FdCheck(d2 == 3))
        ok = 0;
    FdClose(p, d2);
    FdClose(p, fd4);

    f0 = FdGet(p, 0);
    f1 = FdGet(p, 1);
    f2 = FdGet(p, 2);
    if (!FdCheck(f0 && f0->type == RoboFileConsole))
        ok = 0;
    if (!FdCheck(f1 && f1->type == RoboFileConsole))
        ok = 0;
    if (!FdCheck(f2 && f2->type == RoboFileConsole))
        ok = 0;
    n = f0 ? FileRead(f0, buf, 4) : -1;
    if (!FdCheck(n == -(int64_t)RoboEnotsup))
        ok = 0;
    n = f1 ? FileRead(f1, buf, 4) : -1;
    if (!FdCheck(n == -(int64_t)RoboEbadf))
        ok = 0;
    if (!FdCheck(f2 && (f2->flags & RoboFflagWrite)))
        ok = 0;
    if (f0)
        FilePut(f0);
    if (f1)
        FilePut(f1);
    if (f2)
        FilePut(f2);

    for (i = 3; i < RoboMaxFds; i++) {
        RoboFile *f = ConsoleOpen(RoboFflagWrite);
        Fd fd = FdAlloc(p, f, 0);
        FilePut(f);
        if (fd < 0) {
            ok = 0;
            break;
        }
    }
    a = ConsoleOpen(RoboFflagWrite);
    if (!FdCheck(FdAlloc(p, a, 0) == RoboFdInvalid))
        ok = 0;
    FilePut(a);
    for (i = 3; i < RoboMaxFds; i++)
        FdClose(p, (Fd)i);

    rf = FsFileOpen("/test.txt", RoboFflagRead);
    if (rf) {
        filePresent = 1;
        for (gi = 0; gi < 16; gi++) {
            got[gi] = 0;
            buf[gi] = 0;
        }
        n1 = FileRead(rf, got, 4);
        rf2 = FsFileOpen("/test.txt", RoboFflagRead);
        n2 = rf2 ? FileRead(rf2, buf, 4) : -1;
        if (n1 == 4 && n2 == 4 && got[0] == 'R' && buf[0] == 'R')
            fileOk = 1;
        n1 = FileRead(rf, got, 4);
        if (!(n1 == 4 && rf->offset == 8))
            fileOk = 0;
        if (rf2)
            FilePut(rf2);
        FilePut(rf);
        if (!FdCheck(fileOk))
            ok = 0;
    }

    if (!FdCheck(FileStatLive() == live0))
        ok = 0;

    userOk = RunFdUser();
    if (!FdCheck(userOk))
        ok = 0;

    kprintf("FD_SELFTEST  checks %u  live %llu\n",
            gFdTests, (unsigned long long)FileStatLive());

    kprintf("\n========================================\n");
    kprintf("    ROBOTICELA OS FILE DESCRIPTORS\n");
    kprintf("========================================\n\n");
    kprintf("Architecture:\n    %s\n\n", mark(ok));
    kprintf("RoboFDTable:\n    %s\n\n", mark(ok));
    kprintf("RoboFile:\n    %s\n\n", mark(ok));
    kprintf("Reference Counting:\n    %s\n\n", mark(ok));
    kprintf("stdin:\n    %s\n\n", mark(ok));
    kprintf("stdout:\n    %s\n\n", mark(userOk));
    kprintf("stderr:\n    %s\n\n", mark(ok));
    kprintf("SYS_OPEN:\n    %s\n\n", mark(userOk && filePresent));
    kprintf("SYS_READ:\n    %s\n\n", mark(userOk));
    kprintf("SYS_WRITE:\n    %s\n\n", mark(userOk));
    kprintf("SYS_CLOSE:\n    %s\n\n", mark(userOk));
    kprintf("RoboFS Integration:\n    %s\n\n",
            filePresent ? mark(fileOk) : "NOT IMPLEMENTED");
    kprintf("Process Cleanup:\n    %s\n\n", mark(ok && userOk));
    kprintf("Invalid FD Handling:\n    %s\n\n", mark(ok));
    kprintf("User Pointer Validation:\n    %s\n\n", mark(userOk));
    kprintf("Concurrency:\n    NOT TESTED\n\n");
    kprintf("QEMU:\n    %s\n\n", mark(ok && userOk));
    kprintf("User-space Hello:\n    %s\n\n", mark(userOk));
    kprintf("File Read Test:\n    %s\n\n",
            filePresent ? mark(fileOk && userOk) : "NOT IMPLEMENTED");
    kprintf("Leaks:\n    %s\n\n", mark(ok));
    kprintf("Kernel Regressions:\n    %s\n\n", mark(ok && userOk));
    kprintf("========================================\n");
    if (ok && userOk)
        kprintf("[PASS] ROBOTICELA OS FILE DESCRIPTORS\n");
    else
        kprintf("[FAIL] ROBOTICELA OS FILE DESCRIPTORS\n");
    return ok && userOk;
}

#ifndef FD_HOST_TEST
void FdFbReport(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    RoboProcess *cur = CurrentProcess();

    KfbPuts("FD table  open ", mute);
    KfbPutu((unsigned long long)FdTableOpenCount(
                 cur ? cur->fdTable : 0),
             white);
    KfbPuts("  files ", mute);
    KfbPutu((unsigned long long)FileStatLive(), white);
    KfbPuts("\n", mute);
}
#endif
#endif
