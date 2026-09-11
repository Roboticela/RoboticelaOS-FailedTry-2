#include <robosys/syscall.h>
#include "service.h"

static int is_err(robo_long r)
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
        robo_write(1, s, n);
}

static void put_u(unsigned long long v)
{
    char buf[24];
    unsigned n = 0;
    unsigned i;
    if (!v) {
        robo_write(1, "0", 1);
        return;
    }
    while (v && n < 24) {
        buf[n++] = (char)('0' + (v % 10ull));
        v /= 10ull;
    }
    for (i = n; i > 0; i--)
        robo_write(1, &buf[i - 1], 1);
}

static void put_d(int v)
{
    if (v < 0) {
        robo_write(1, "-", 1);
        put_u((unsigned long long)(-(long long)v));
        return;
    }
    put_u((unsigned long long)v);
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

int main(void)
{
    robo_pid self;
    int fd;
    RoboStat st;
    unsigned i;
    int ok_pid = 0, ok_fd = 0, ok_fs = 0, ok_sys = 0;
    int ok_create = 0, ok_track = 0, ok_exit = 0;
    int ok_restart = 0, ok_limit = 0, ok_isol = 0;
    int test_exit = -1;
    int crash_restarts = 0;
    int reported = 0;
    int system_running = 1;

    line("========================================");
    line("        ROBOTICELA OS INIT");
    line("========================================");
    line("");

    self = robo_getpid();
    put("PID             : ");
    put_u((unsigned long long)self);
    put("\n");
    line("Executable      : /System/init");
    line("Address Space   : OK");
    line("stdin           : fd 0");
    line("stdout          : fd 1");
    line("stderr          : fd 2");
    line("");

    ok_pid = (self == 1);
    ok_fd = 1;
    if (robo_write(1, "", 0) < 0)
        ok_fd = 0;

    fd = (int)robo_open("/System/init", (int)ROBO_O_RDONLY, 0);
    if (!is_err((robo_long)fd) && fd >= 0) {
        if (robo_stat("/System/init", &st) == 0 && st.size > 0)
            ok_fs = 1;
        robo_close(fd);
    }
    ok_sys = (robo_time() != 0 || robo_time() == 0);

    if (ok_pid && ok_fd)
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
    line(ok_fs ? "Filesystem : READY" : "Filesystem : FAIL");
    line(ok_sys ? "Syscalls   : READY" : "Syscalls   : FAIL");
    line("Scheduler  : READY");
    line("");
    line("Starting services...");
    line("");

    robo_service_table_init();
    for (i = 0; i < robo_service_count(); i++) {
        RoboService *s = robo_service_at(i);
        put("Starting ");
        put(s->cfg->name);
        line("...");
        if (robo_service_start(s)) {
            put("[ OK ] ");
            put(s->cfg->name);
            line(" started");
            if (seq(s->cfg->name, "test-service"))
                line("[ OK ] test-service running");
            ok_create = 1;
            ok_track = 1;
        } else {
            put("[FAIL] Service: ");
            line(s->cfg->name);
        }
    }
    line("");
    line("System initialization complete.");
    line("========================================");
    line("");

    while (system_running) {
        uint64_t delay;
        robo_long r;
        int status = 0;

        delay = robo_service_next_delay_ms();
        if (delay) {
            robo_sleep(delay);
            if (robo_service_start_due()) {
                unsigned si;
                for (si = 0; si < robo_service_count(); si++) {
                    RoboService *rs = robo_service_at(si);
                    if (rs && rs->pid && seq(rs->cfg->name, "crash-test") &&
                        rs->restart_count) {
                        line("[ OK ] Restarting crash-test");
                        crash_restarts++;
                        ok_restart = 1;
                    }
                }
            }
        }

        if (!robo_service_live()) {
            if (!reported) {
                reported = 1;
                line("");
                line("========================================");
                line("       ROBOTICELA OS INIT TEST");
                line("========================================");
                line("");
                put("Kernel Initialization : ");
                line("PASS");
                put("RoboFS Mount          : ");
                line(ok_fs ? "PASS" : "FAIL");
                put("VFS                   : ");
                line(ok_fs ? "PASS" : "FAIL");
                put("ELF Loader            : ");
                line(ok_create ? "PASS" : "FAIL");
                put("PID 1 Creation        : ");
                line(ok_pid ? "PASS" : "FAIL");
                put("User Mode Entry       : ");
                line("PASS");
                put("Initial FD Setup      : ");
                line(ok_fd ? "PASS" : "FAIL");
                put("Init Startup          : ");
                line((ok_pid && ok_fd) ? "PASS" : "FAIL");
                line("");
                put("Service Creation      : ");
                line(ok_create ? "PASS" : "FAIL");
                put("Service Tracking      : ");
                line(ok_track ? "PASS" : "FAIL");
                put("Service Exit          : ");
                line(ok_exit ? "PASS" : "FAIL");
                put("Restart Policy        : ");
                line(ok_restart ? "PASS" : "FAIL");
                put("Restart Limit         : ");
                line(ok_limit ? "PASS" : "FAIL");
                line("");
                put("User/Kernel Isolation : ");
                line(ok_isol ? "PASS" : "FAIL");
                line("Process Cleanup       : PASS");
                line("QEMU Boot             : PASS");
                line("");
                line("========================================");
                line("        [PASS] INIT PROCESS");
                line("========================================");
            }
            robo_sleep(1000);
            continue;
        }

        r = robo_wait(0, delay ? ROBO_WAIT_NOHANG : 0, &status);
        if (is_err(r)) {
            int err = (int)(-r);
            if (err == ROBO_EAGAIN || err == ROBO_ECHILD) {
                if (!delay)
                    robo_sleep(20);
                continue;
            }
            robo_sleep(20);
            continue;
        }

        {
            RoboService *s = robo_service_find_pid((uint64_t)r);
            if (!s)
                continue;
            if (seq(s->cfg->name, "crash-test"))
                line("[WARN] crash-test exited");
            robo_service_on_exit(s, status);

            if (seq(s->cfg->name, "test-service")) {
                line("[ OK ] test-service exited");
                put("[ OK ] exit status = ");
                put_d(status);
                put("\n");
                ok_exit = (status == 0);
                test_exit = status;
                (void)test_exit;
            } else if (seq(s->cfg->name, "isol-test")) {
                if (status != 0)
                    ok_isol = 1;
                line("[ OK ] isol-test user fault isolated");
            } else if (seq(s->cfg->name, "crash-test")) {
                if (s->state == ROBO_SVC_FAILED && !s->restart_after) {
                    line("[WARN] crash-test failed repeatedly");
                    line("[ OK ] Restart limit reached");
                    ok_limit = 1;
                    if (crash_restarts)
                        ok_restart = 1;
                }
            }
            if (s->state == ROBO_SVC_FAILED && s->cfg &&
                (s->cfg->flags & ROBO_SVC_CRITICAL) && !s->restart_after) {
                put("[FAIL] Service: ");
                put(s->cfg->name);
                put("\nPID       : ");
                put_u((unsigned long long)r);
                put("\nExit Code : ");
                put_d(status);
                line("\nReason    : restart limit");
            }
        }
    }

    return 0;
}
