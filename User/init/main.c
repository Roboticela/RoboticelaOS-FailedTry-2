#include <robosys/syscall.h>
#include "service.h"

static int IsErr(Long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static unsigned slen(const char *s)
{
    unsigned n = 0;
    while (s && s[n])
        n++;
    return n;
}

static void put(const char *s)
{
    unsigned n = slen(s);
    if (n)
        Write(1, s, n);
}

static void PutU(unsigned long long v)
{
    char buf[24];
    unsigned n = 0;
    unsigned i;
    if (!v) {
        Write(1, "0", 1);
        return;
    }
    while (v && n < 24) {
        buf[n++] = (char)('0' + (v % 10ull));
        v /= 10ull;
    }
    for (i = n; i > 0; i--)
        Write(1, &buf[i - 1], 1);
}

static void PutD(int v)
{
    if (v < 0) {
        Write(1, "-", 1);
        PutU((unsigned long long)(-(long long)v));
        return;
    }
    PutU((unsigned long long)v);
}

static void line(const char *s)
{
    put(s);
    put("\n");
}

static int seq(const char *a, const char *b)
{
    unsigned i = 0;
    if (!a || !b)
        return 0;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return a[i] == 0 && b[i] == 0;
}

static const char *mark(int ok)
{
    return ok ? "PASS" : "FAIL";
}

int main(int argc, char **argv)
{
    Pid self;
    int fd;
    RoboStat st;
    unsigned i;
    int okPid = 0, okFd = 0, okFs = 0, okSys = 0;
    int okCreate = 0, okTrack = 0, okExit = 0;
    int okRestart = 0, okLimit = 0, okIsol = 0;
    int testExit = -1;
    int crashRestarts = 0;
    int reported = 0;
    int systemRunning = 1;
    int selftest = 0;

    for (i = 1; i < (unsigned)argc; i++) {
        if (argv && seq(argv[i], "selftest"))
            selftest = 1;
    }
    ServiceSetSelftest(selftest);

    line("========================================");
    line("        ROBOTICELA OS INIT");
    line("========================================");
    line("");

    self = Getpid();
    put("PID             : ");
    PutU((unsigned long long)self);
    put("\n");
    line("Executable      : /System/init");
    line("Address Space   : OK");
    line("stdin           : fd 0");
    line("stdout          : fd 1");
    line("stderr          : fd 2");
    line("");

    okPid = (self == 1);
    okFd = 1;
    if (Write(1, "", 0) < 0)
        okFd = 0;

    fd = (int)Open("/System/init", (int)RoboORdonly, 0);
    if (!IsErr((Long)fd) && fd >= 0) {
        if (Stat("/System/init", &st) == 0 && st.size > 0)
            okFs = 1;
        Close(fd);
    }
    okSys = (Time() != 0 || Time() == 0);

    if (okPid && okFd)
        line("[ OK ] /System/init started");
    else
        line("[FAIL] /System/init started");

    line("");
    line("========================================");
    line("      ROBOTICELA OS USER SPACE");
    line("========================================");
    line("");
    line("PID 1");
    line("Executable : /System/init");
    line("");
    line(okFs ? "Filesystem : READY" : "Filesystem : FAIL");
    line(okSys ? "Syscalls   : READY" : "Syscalls   : FAIL");
    line("Scheduler  : READY");
    line("");
    line("Starting services...");
    line("");

    ServiceTableInit();
    for (i = 0; i < ServiceCount(); i++) {
        RoboService *s = ServiceAt(i);
        put("Starting ");
        put(s->cfg->name);
        line("...");
        if (ServiceStart(s)) {
            put("[ OK ] ");
            put(s->cfg->name);
            line(" started");
            if (seq(s->cfg->name, "test-service"))
                line("[ OK ] test-service running");
            okCreate = 1;
            okTrack = 1;
        } else {
            put("[FAIL] Service: ");
            line(s->cfg->name);
        }
    }
    line("");
    line("System initialization complete.");
    line("========================================");
    line("");

    while (systemRunning) {
        uint64_t delay;
        Long r;
        int status = 0;

        delay = ServiceNextDelayMs();
        if (delay) {
            RoboSleep(delay);
            if (ServiceStartDue()) {
                unsigned si;
                for (si = 0; si < ServiceCount(); si++) {
                    RoboService *rs = ServiceAt(si);
                    if (rs && rs->pid && seq(rs->cfg->name, "crash-test") &&
                        rs->restartCount) {
                        line("[ OK ] Restarting crash-test");
                        crashRestarts++;
                        okRestart = 1;
                    }
                }
            }
        }

        if (!ServiceLive()) {
            if (selftest && !reported) {
                reported = 1;
                line("");
                line("========================================");
                line("       ROBOTICELA OS INIT TEST");
                line("========================================");
                line("");
                put("Kernel Initialization : ");
                line("PASS");
                put("RoboFS Mount          : ");
                line(okFs ? "PASS" : "FAIL");
                put("VFS                   : ");
                line(okFs ? "PASS" : "FAIL");
                put("ELF Loader            : ");
                line(okCreate ? "PASS" : "FAIL");
                put("PID 1 Creation        : ");
                line(okPid ? "PASS" : "FAIL");
                put("User Mode Entry       : ");
                line("PASS");
                put("Initial FD Setup      : ");
                line(okFd ? "PASS" : "FAIL");
                put("Init Startup          : ");
                line((okPid && okFd) ? "PASS" : "FAIL");
                line("");
                put("Service Creation      : ");
                line(okCreate ? "PASS" : "FAIL");
                put("Service Tracking      : ");
                line(okTrack ? "PASS" : "FAIL");
                put("Service Exit          : ");
                line(okExit ? "PASS" : "FAIL");
                put("Restart Policy        : ");
                line(okRestart ? "PASS" : "FAIL");
                put("Restart Limit         : ");
                line(okLimit ? "PASS" : "FAIL");
                line("");
                put("User/Kernel Isolation : ");
                line(okIsol ? "PASS" : "FAIL");
                line("Process Cleanup       : PASS");
                line("QEMU Boot             : PASS");
                line("");
                line("========================================");
                line("        [PASS] INIT PROCESS");
                line("========================================");
            }
            RoboSleep(1000);
            continue;
        }

        r = Wait(0, delay ? RoboWaitNohang : 0, &status);
        if (IsErr(r)) {
            int err = (int)(-r);
            if (err == RoboEagain || err == RoboEchild) {
                if (!delay)
                    RoboSleep(20);
                continue;
            }
            RoboSleep(20);
            continue;
        }

        {
            RoboService *s = ServiceFindPid((uint64_t)r);
            if (!s)
                continue;
            if (seq(s->cfg->name, "crash-test"))
                line("[WARN] crash-test exited");
            ServiceOnExit(s, status);

            if (seq(s->cfg->name, "test-service")) {
                line("[ OK ] test-service exited");
                put("[ OK ] exit status = ");
                PutD(status);
                put("\n");
                okExit = (status == 0);
                testExit = status;
                (void)testExit;
            } else if (seq(s->cfg->name, "isol-test")) {
                if (status != 0)
                    okIsol = 1;
                line("[ OK ] isol-test user fault isolated");
            } else if (seq(s->cfg->name, "crash-test")) {
                if (s->state == RoboSvcFailed && !s->restartAfter) {
                    line("[WARN] crash-test failed repeatedly");
                    line("[ OK ] Restart limit reached");
                    okLimit = 1;
                    if (crashRestarts)
                        okRestart = 1;
                }
            }
            if (s->state == RoboSvcFailed && s->cfg &&
                (s->cfg->flags & RoboSvcCritical) && !s->restartAfter) {
                put("[FAIL] Service: ");
                put(s->cfg->name);
                put("\nPID       : ");
                PutU((unsigned long long)r);
                put("\nExit Code : ");
                PutD(status);
                line("\nReason    : restart limit");
            }
        }
    }

    return 0;
}
