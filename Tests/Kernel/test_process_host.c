#include "process/process.h"
#include "process/process_table.h"
#include "fs/backends/console.h"
#include "fs/file.h"
#include <robosys/syscallNumbers.h>

#include <stdio.h>
#include <string.h>

static int gPass;
static int gFails;

#define CHECK(cond, msg) do { \
    if (cond) { gPass++; } \
    else { gFails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } \
} while (0)

static void ZeroThread(RoboThread *t)
{
    memset(t, 0, sizeof(*t));
}

int main(void)
{
    RoboProcess *k, *a, *b, *c, *d;
    RoboThread t1, t2, t3;
    int fd;
    ProcessId gone;

    CHECK(ProcessSubsystemInit(), "subsystem init");
    k = KernelProcess();
    CHECK(k != 0, "kernel process");
    CHECK(GetProcessId(k) == RoboProcessIdKernel, "kernel pid 0");
    CHECK(ProcessLookup(k->id) == k, "kernel in table");
    CHECK(k->fdTable && k->ipc && k->environment && k->credentials, "kernel resources");
    CHECK(k->addressSpace != 0, "kernel aspace");

    a = ProcessCreateNamed("a");
    b = ProcessCreateNamed("b");
    c = ProcessCreateNamed("c");
    d = ProcessCreateNamed("d");
    CHECK(a && b && c && d, "create four");
    CHECK(a->id && b->id && c->id && d->id, "nonzero pids");
    CHECK(a->id != b->id && b->id != c->id && c->id != d->id && a->id != d->id,
          "unique pids");
    CHECK(ProcessLookup(a->id) == a, "lookup a");
    CHECK(ProcessLookup(b->id) == b, "lookup b");
    CHECK(a->parent == k && b->parent == k, "parent kernel");
    CHECK(a->fdTable != b->fdTable && a->ipc != b->ipc, "independent files/ipc");
    CHECK(a->environment != b->environment && a->credentials != b->credentials,
          "independent env/creds");
    CHECK(a->addressSpace != b->addressSpace, "independent aspace");
    CHECK(a->addressSpace->cr3 != b->addressSpace->cr3, "independent cr3 token");

    {
        RoboFile *tf = ConsoleOpen(RoboFflagWrite);
        RoboFile *got;
        CHECK(tf != 0, "fd file");
        fd = ProcessFdAlloc(a, tf);
        CHECK(fd >= 3, "fd alloc");
        got = ProcessFdGet(a, fd);
        CHECK(got == tf, "fd get");
        if (got)
            FilePut(got);
        CHECK(!ProcessFdInUse(b, fd), "fd not shared");
        CHECK(ProcessFdClose(a, fd), "fd close");
        CHECK(!ProcessFdInUse(a, fd), "fd closed");
        if (tf)
            FilePut(tf);
    }

    ZeroThread(&t1);
    ZeroThread(&t2);
    ZeroThread(&t3);
    t1.id = 10;
    t2.id = 11;
    t3.id = 12;
    CHECK(ProcessAddThread(a, &t1), "add t1");
    CHECK(ProcessAddThread(a, &t2), "add t2");
    CHECK(ProcessAddThread(a, &t3), "add t3");
    CHECK(a->threadCount == 3, "count 3");
    CHECK(t1.process == a && t2.process == a && t3.process == a, "owners");
    CHECK(ThreadProcess(&t1) == a, "thread_process");
    CHECK(!ProcessAddThread(b, &t1), "no steal");
    CHECK(ProcessRemoveThread(a, &t2), "remove t2");
    CHECK(a->threadCount == 2, "count 2");
    CHECK(ProcessRemoveThread(a, &t1), "remove t1");
    CHECK(ProcessRemoveThread(a, &t3), "remove t3");
    CHECK(a->threadCount == 0, "count 0");

    gone = b->id;
    CHECK(ProcessDestroy(b), "destroy b");
    CHECK(ProcessLookup(gone) == 0, "stale pid");
    CHECK(ProcessDestroy(c), "destroy c");
    CHECK(ProcessDestroy(d), "destroy d");
    {
        RoboProcess *z;
        int st = -1;
        ProcessId out = 0;
        ProcessId zid;
        z = ProcessCreateNamed("zombie");
        CHECK(z != 0, "zombie create");
        zid = z->id;
        z->state = RoboProcessTerminated;
        z->exitStatus = 7;
        CHECK(ProcessWait(0, RoboWaitNohang, &st, &out) == 0, "wait zombie");
        CHECK(out == zid && st == 7, "wait status");
        CHECK(ProcessLookup(out) == 0, "reaped");
    }
    CHECK(ProcessDestroy(a), "destroy a");
    CHECK(!ProcessDestroy(k), "kernel immortal");
    CHECK(ProcessLookup(k->id) == k, "kernel remains");
    CHECK(ProcessStatLive() == 1, "only kernel live");
    CHECK(ProcessStatCreated() >= 5, "created count");
    CHECK(ProcessStatDestroyed() >= 4, "destroyed count");
    CHECK(ProcessStatAspaceCreated() == ProcessStatAspaceDestroyed(),
          "aspace balanced");

    printf("Process host tests  %d passed  %d failed\n", gPass, gFails);
    return gFails ? 1 : 0;
}
