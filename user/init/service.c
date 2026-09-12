#include "service.h"
#include "config.h"
#include <robosys/syscall.h>

#define ROBO_SVC_MAX 12

static RoboService g_svc[ROBO_SVC_MAX];
static unsigned g_n;
static int g_selftest;

static int is_err(robo_long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

void robo_service_set_selftest(int on)
{
    g_selftest = on ? 1 : 0;
}

void robo_service_table_init(void)
{
    const RoboServiceConfig *cfg;
    unsigned n = 0;
    unsigned i;

    cfg = robo_service_config(&n);
    g_n = 0;
    for (i = 0; i < n && g_n < ROBO_SVC_MAX; i++) {
        if (!g_selftest && (cfg[i].flags & ROBO_SVC_SELFTEST))
            continue;
        g_svc[g_n].cfg = &cfg[i];
        g_svc[g_n].pid = 0;
        g_svc[g_n].state = ROBO_SVC_STOPPED;
        g_svc[g_n].restart_policy = cfg[i].restart_policy;
        g_svc[g_n].restart_count = 0;
        g_svc[g_n].restart_limit = cfg[i].restart_limit;
        g_svc[g_n].last_start_time = 0;
        g_svc[g_n].restart_after = 0;
        g_svc[g_n].exit_status = 0;
        g_svc[g_n].ready = 0;
        g_n++;
    }
}

unsigned robo_service_count(void)
{
    return g_n;
}

RoboService *robo_service_at(unsigned i)
{
    return i < g_n ? &g_svc[i] : 0;
}

RoboService *robo_service_find_pid(uint64_t pid)
{
    unsigned i;

    if (!pid)
        return 0;
    for (i = 0; i < g_n; i++) {
        if (g_svc[i].pid == pid)
            return &g_svc[i];
    }
    return 0;
}

int robo_service_start(RoboService *svc)
{
    robo_long r;

    if (!svc || !svc->cfg || !(svc->cfg->flags & ROBO_SVC_ENABLED))
        return 0;
    svc->state = ROBO_SVC_STARTING;
    svc->ready = 0;
    svc->pid = 0;
    if (g_selftest) {
        char *av[3];
        av[0] = (char *)svc->cfg->path;
        av[1] = "selftest";
        av[2] = 0;
        r = robo_process_spawn(svc->cfg->path, av, 2);
    } else {
        r = robo_process_create(svc->cfg->path);
    }
    if (is_err(r) || r <= 0) {
        svc->state = ROBO_SVC_FAILED;
        return 0;
    }
    svc->pid = (uint64_t)r;
    svc->last_start_time = robo_time();
    svc->restart_after = 0;
    svc->state = ROBO_SVC_RUNNING;
    /* Process created is not a ready notification. */
    return 1;
}

static uint64_t backoff_ms(uint32_t count)
{
    uint64_t ms = 20ull << (count > 8u ? 8u : count);
    if (ms > 100ull)
        ms = 100ull;
    return ms;
}

void robo_service_on_exit(RoboService *svc, int status)
{
    uint32_t policy;

    if (!svc)
        return;
    svc->exit_status = status;
    svc->pid = 0;
    svc->ready = 0;
    policy = svc->restart_policy;
    if (policy == ROBO_RESTART_ALWAYS ||
        (policy == ROBO_RESTART_ON_FAILURE && status != 0)) {
        if (svc->restart_count >= svc->restart_limit) {
            svc->state = ROBO_SVC_FAILED;
            svc->restart_after = 0;
            return;
        }
        svc->state = ROBO_SVC_FAILED;
        svc->restart_after = robo_time() + backoff_ms(svc->restart_count) *
                             1000000ull;
        svc->restart_count++;
        return;
    }
    svc->state = (status == 0) ? ROBO_SVC_EXITED : ROBO_SVC_FAILED;
    svc->restart_after = 0;
}

int robo_service_start_due(void)
{
    unsigned i;
    int started = 0;
    uint64_t now = robo_time();

    for (i = 0; i < g_n; i++) {
        RoboService *s = &g_svc[i];
        if (!s->restart_after)
            continue;
        if (now < s->restart_after)
            continue;
        s->restart_after = 0;
        if (robo_service_start(s))
            started = 1;
    }
    return started;
}

uint64_t robo_service_next_delay_ms(void)
{
    unsigned i;
    uint64_t now = robo_time();
    uint64_t best = 0;

    for (i = 0; i < g_n; i++) {
        uint64_t due = g_svc[i].restart_after;
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

int robo_service_live(void)
{
    unsigned i;

    for (i = 0; i < g_n; i++) {
        if (g_svc[i].pid)
            return 1;
        if (g_svc[i].restart_after)
            return 1;
    }
    return 0;
}
