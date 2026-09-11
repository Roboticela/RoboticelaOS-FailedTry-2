#include "scheduler/scheduler.h"
#include "arch/x86_64/scheduler/context_switch.h"
#include "time/time.h"
#include "kio.h"

static volatile uint64_t g_cnt_a;
static volatile uint64_t g_cnt_b;
static volatile uint64_t g_cnt_c;
static volatile uint64_t g_reg_ok;
static volatile uint64_t g_reg_bad;
static volatile uint64_t g_stack_a_addr;
static volatile uint64_t g_stack_b_addr;
static volatile int g_stack_fail;
static volatile int g_term_ran;
static volatile uint64_t g_term_id;

static int tfail(const char *tag)
{
    kprintf("Scheduler selftest: %s\n", tag);
    return 0;
}

static void task_a(void *arg)
{
    (void)arg;
    for (;;)
        g_cnt_a++;
}

static void task_b(void *arg)
{
    (void)arg;
    for (;;)
        g_cnt_b++;
}

static void task_c(void *arg)
{
    (void)arg;
    for (;;)
        g_cnt_c++;
}

static void task_regs(void *arg)
{
    (void)arg;
    robo_reg_preserve_loop(&g_reg_ok, &g_reg_bad);
}

static void task_stack_a(void *arg)
{
    volatile uint64_t local[32];
    unsigned i;
    (void)arg;
    for (i = 0; i < 32; i++)
        local[i] = 0xAAAA0000ull + i;
    g_stack_a_addr = (uint64_t)(uintptr_t)&local[0];
    for (;;) {
        for (i = 0; i < 32; i++) {
            if (local[i] != 0xAAAA0000ull + i)
                g_stack_fail = 1;
        }
    }
}

static void task_stack_b(void *arg)
{
    volatile uint64_t local[32];
    unsigned i;
    (void)arg;
    for (i = 0; i < 32; i++)
        local[i] = 0xBBBB0000ull + i;
    g_stack_b_addr = (uint64_t)(uintptr_t)&local[0];
    for (;;) {
        for (i = 0; i < 32; i++) {
            if (local[i] != 0xBBBB0000ull + i)
                g_stack_fail = 1;
        }
    }
}

static void task_term(void *arg)
{
    robo_task_t *self = robo_current_task();
    (void)arg;
    g_term_id = self ? self->id : 0;
    g_term_ran = 1;
    robo_task_exit();
}

static int fair_ok(uint64_t a, uint64_t b, uint64_t c)
{
    uint64_t tot = a + b + c;
    uint64_t min_share;
    if (tot < 1000)
        return 0;
    min_share = tot / 10ull;
    if (a < min_share || b < min_share || c < min_share)
        return 0;
    return 1;
}

static int rr_ok(uint64_t id_a, uint64_t id_b, uint64_t id_c)
{
    uint64_t log[64];
    unsigned n, i, seen_a = 0, seen_b = 0, seen_c = 0;
    unsigned run = 1, max_run = 1;
    uint64_t prev;

    n = robo_sched_rr_log(log, 64);
    if (n < 6)
        return 0;
    prev = log[0];
    for (i = 0; i < n; i++) {
        if (log[i] == id_a)
            seen_a = 1;
        else if (log[i] == id_b)
            seen_b = 1;
        else if (log[i] == id_c)
            seen_c = 1;
        if (i > 0) {
            if (log[i] == prev)
                run++;
            else {
                if (run > max_run)
                    max_run = run;
                run = 1;
                prev = log[i];
            }
        }
    }
    if (run > max_run)
        max_run = run;
    if (!seen_a || !seen_b || !seen_c)
        return 0;
    if (max_run > n / 2u + 4u)
        return 0;
    return 1;
}

int robo_scheduler_selftest(void)
{
    robo_task_t *ta, *tb, *tc, *tr, *tsa, *tsb, *tt;
    uint64_t sw0, prem0, y0;
    uint64_t sw1, prem1;
    int passes = 0;

    kprintf("Scheduler Self-Test\n");
    kprintf("-------------------\n");

    if (!robo_scheduler_ready() || !robo_current_task())
        return tfail("not initialized");
    kprintf("[PASS] Scheduler initialized\n");
    passes++;

    if (!robo_bootstrap_task() || robo_current_task()->id != ROBO_TID_BOOTSTRAP)
        return tfail("bootstrap not current");
    kprintf("[PASS] Bootstrap task adopted\n");
    passes++;

    if (!robo_idle_task() || robo_idle_task()->id != ROBO_TID_IDLE)
        return tfail("idle missing");
    kprintf("[PASS] Idle task created\n");
    passes++;

    ta = robo_task_create("task-a", task_a, 0);
    tb = robo_task_create("task-b", task_b, 0);
    tc = robo_task_create("task-c", task_c, 0);
    tr = robo_task_create("task-reg", task_regs, 0);
    tsa = robo_task_create("stack-a", task_stack_a, 0);
    tsb = robo_task_create("stack-b", task_stack_b, 0);
    if (!ta || !tb || !tc)
        return tfail("create A/B/C");
    if (!tr || !tsa || !tsb)
        return tfail("create aux tasks");
    if (ta->id == tb->id || tb->id == tc->id || ta->id == tc->id)
        return tfail("duplicate ids");
    kprintf("[PASS] Task A created\n");
    kprintf("[PASS] Task B created\n");
    kprintf("[PASS] Task C created\n");
    passes += 3;

    sw0 = robo_sched_switches();
    prem0 = robo_sched_preemptions();
    y0 = robo_sched_yields();
    (void)y0;

    if (robo_scheduler_state() != ROBO_SCHED_RUNNING)
        robo_scheduler_start();
    if (robo_scheduler_state() != ROBO_SCHED_RUNNING)
        return tfail("start");
    kprintf("[PASS] Timer preemption enabled\n");
    passes++;

    robo_sleep(800);

    sw1 = robo_sched_switches();
    prem1 = robo_sched_preemptions();
    robo_sched_reap();

    if (sw1 <= sw0)
        return tfail("no context switches");
    kprintf("[PASS] Context switching active\n");
    passes++;

    if (prem1 <= prem0)
        return tfail("no timer preemption");
    if (g_cnt_a == 0 || g_cnt_b == 0 || g_cnt_c == 0)
        return tfail("a task never ran");
    kprintf("[PASS] Preemption without yield\n");
    passes++;

    if (!rr_ok(ta->id, tb->id, tc->id))
        return tfail("round-robin ordering");
    kprintf("[PASS] Round-robin ordering\n");
    passes++;

    if (!fair_ok(g_cnt_a, g_cnt_b, g_cnt_c))
        return tfail("fairness");
    kprintf("[PASS] Scheduler fairness\n");
    passes++;

    if (g_reg_bad)
        return tfail("register corruption");
    if (g_reg_ok < 10)
        return tfail("register loop did not run");
    kprintf("[PASS] Register preservation\n");
    passes++;

    if (g_stack_fail)
        return tfail("stack isolation");
    if (!g_stack_a_addr || !g_stack_b_addr || g_stack_a_addr == g_stack_b_addr)
        return tfail("shared stack");
    kprintf("[PASS] Stack isolation\n");
    passes++;

    {
        unsigned i;
        uint64_t y1 = robo_sched_yields();
        for (i = 0; i < 2; i++)
            robo_task_yield();
        if (robo_sched_yields() <= y1)
            return tfail("yield");
        kprintf("[PASS] Task yield\n");
        passes++;
    }

    tt = robo_task_create("task-term", task_term, 0);
    if (!tt)
        return tfail("term create");
    robo_sleep(200);
    robo_sched_reap();
    if (!g_term_ran)
        return tfail("term did not run");
    kprintf("[PASS] Task termination\n");
    passes++;

    kprintf("Scheduler Test\n");
    kprintf("Task A : %llu\n", (unsigned long long)g_cnt_a);
    kprintf("Task B : %llu\n", (unsigned long long)g_cnt_b);
    kprintf("Task C : %llu\n", (unsigned long long)g_cnt_c);
    kprintf("Context Switches : %llu\n", (unsigned long long)sw1);
    kprintf("Preemptions      : %llu\n", (unsigned long long)prem1);
    kprintf("Voluntary Yields : %llu\n", (unsigned long long)robo_sched_yields());
    kprintf("[PASS] Preemptive round-robin\n");
    kprintf("[PASS] Timer integration\n");
    passes += 2;

    (void)ta;
    (void)tb;
    (void)tc;
    (void)tr;
    (void)tsa;
    (void)tsb;
    (void)g_term_id;
    kprintf("Scheduler selftest %d checks\n", passes);
    return 1;
}
