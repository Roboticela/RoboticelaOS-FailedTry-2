#include "service.h"
#include "config.h"
#include <robosys/syscall.h>

#define RoboSvcMax 12

static RoboService gSvc[RoboSvcMax];
static unsigned gN;
static int gSelftest;
static int gCheckpoint;

static int IsErr(Long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

void ServiceSetSelftest(int on)
{
    gSelftest = on ? 1 : 0;
}

void ServiceSetCheckpoint(int on)
{
    gCheckpoint = on ? 1 : 0;
}

void ServiceTableInit(void)
{
    const RoboServiceConfig *cfg;
    unsigned n = 0;
    unsigned i;

    cfg = ServiceConfig(&n);
    gN = 0;
    for (i = 0; i < n && gN < RoboSvcMax; i++) {
        if (!gSelftest && (cfg[i].flags & RoboSvcSelftest))
            continue;
        gSvc[gN].cfg = &cfg[i];
        gSvc[gN].pid = 0;
        gSvc[gN].state = RoboSvcStopped;
        gSvc[gN].restartPolicy = cfg[i].restartPolicy;
        gSvc[gN].restartCount = 0;
        gSvc[gN].restartLimit = cfg[i].restartLimit;
        gSvc[gN].lastStartTime = 0;
        gSvc[gN].restartAfter = 0;
        gSvc[gN].exitStatus = 0;
        gSvc[gN].ready = 0;
        gN++;
    }
}

unsigned ServiceCount(void)
{
    return gN;
}

RoboService *ServiceAt(unsigned i)
{
    return i < gN ? &gSvc[i] : 0;
}

RoboService *ServiceFindPid(uint64_t pid)
{
    unsigned i;

    if (!pid)
        return 0;
    for (i = 0; i < gN; i++) {
        if (gSvc[i].pid == pid)
            return &gSvc[i];
    }
    return 0;
}

static int NameIs(const char *a, const char *b)
{
    unsigned i = 0;

    if (!a || !b)
        return 0;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return a[i] == 0 && b[i] == 0;
}

int ServiceStart(RoboService *svc)
{
    Long r;

    if (!svc || !svc->cfg || !(svc->cfg->flags & RoboSvcEnabled))
        return 0;
    svc->state = RoboSvcStarting;
    svc->ready = 0;
    svc->pid = 0;
    if (gSelftest) {
        char *av[3];
        av[0] = (char *)svc->cfg->path;
        av[1] = "selftest";
        av[2] = 0;
        r = ProcessSpawn(svc->cfg->path, av, 2);
    } else if (gCheckpoint && NameIs(svc->cfg->name, "robo-runtime")) {
        /* Desktop checkpoint for QEMU display tests: only robo-runtime. */
        char *av[3];
        av[0] = (char *)svc->cfg->path;
        av[1] = "checkpoint";
        av[2] = 0;
        r = ProcessSpawn(svc->cfg->path, av, 2);
    } else {
        r = ProcessCreate(svc->cfg->path);
    }
    if (IsErr(r) || r <= 0) {
        svc->state = RoboSvcFailed;
        return 0;
    }
    svc->pid = (uint64_t)r;
    svc->lastStartTime = Time();
    svc->restartAfter = 0;
    svc->state = RoboSvcRunning;
    /* Process created is not a ready notification. */
    return 1;
}

static uint64_t BackoffMs(uint32_t count)
{
    uint64_t ms = 20ull << (count > 8u ? 8u : count);
    if (ms > 100ull)
        ms = 100ull;
    return ms;
}

void ServiceOnExit(RoboService *svc, int status)
{
    uint32_t policy;

    if (!svc)
        return;
    svc->exitStatus = status;
    svc->pid = 0;
    svc->ready = 0;
    policy = svc->restartPolicy;
    if (policy == RoboRestartAlways ||
        (policy == RoboRestartOnFailure && status != 0)) {
        if (svc->restartCount >= svc->restartLimit) {
            svc->state = RoboSvcFailed;
            svc->restartAfter = 0;
            return;
        }
        svc->state = RoboSvcFailed;
        svc->restartAfter = Time() + BackoffMs(svc->restartCount) *
                             1000000ull;
        svc->restartCount++;
        return;
    }
    svc->state = (status == 0) ? RoboSvcExited : RoboSvcFailed;
    svc->restartAfter = 0;
}

int ServiceStartDue(void)
{
    unsigned i;
    int started = 0;
    uint64_t now = Time();

    for (i = 0; i < gN; i++) {
        RoboService *s = &gSvc[i];
        if (!s->restartAfter)
            continue;
        if (now < s->restartAfter)
            continue;
        s->restartAfter = 0;
        if (ServiceStart(s))
            started = 1;
    }
    return started;
}

uint64_t ServiceNextDelayMs(void)
{
    unsigned i;
    uint64_t now = Time();
    uint64_t best = 0;

    for (i = 0; i < gN; i++) {
        uint64_t due = gSvc[i].restartAfter;
        uint64_t ms;
        if (!due)
            continue;
        if (due <= now)
            return 1;
        ms = (due - now) / 1000000ull;
        if (!ms)
            ms = 1;
        if (!best || ms < best)
            best = ms;
    }
    return best;
}

int ServiceLive(void)
{
    unsigned i;

    for (i = 0; i < gN; i++) {
        if (gSvc[i].pid)
            return 1;
        if (gSvc[i].restartAfter)
            return 1;
    }
    return 0;
}
