#include "arch/x86_64/context/context_switch.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/tss.h"
#include "scheduler/scheduler.h"
#include "thread/thread.h"
#include "time/time.h"
#include "time/convert.h"
#include "kio.h"

#define CTX_AB_TARGET     100000ull
#define CTX_STRESS_N      10u
#define CTX_STRESS_TARGET 10000ull

static volatile uint64_t counter_a;
static volatile uint64_t counter_b;
static volatile uint64_t g_reg_ok;
static volatile uint64_t g_reg_bad;
static volatile uint64_t g_reg_partner;
static volatile uint64_t g_local_a;
static volatile uint64_t g_local_b;
static volatile uint64_t g_stack_a_addr;
static volatile uint64_t g_stack_b_addr;
static volatile int g_stack_fail;
static volatile int g_stage;
static volatile int g_entries;
static volatile int g_stage_back;
static volatile uint64_t g_stage_partner;
static volatile uint64_t g_stress[CTX_STRESS_N];
static volatile uint64_t g_yield_n;
static volatile int g_reg_done;
static volatile int g_regp_done;
static volatile int g_a_done;
static volatile int g_b_done;
static volatile int g_rip_done;

static int tfail(const char *tag)
{
    kprintf("Context selftest: %s\n", tag);
    return 0;
}

static void stretch(void)
{
    uint64_t t = robo_time_now();
    while (robo_time_now() - t < (ROBO_NS_PER_MS / 2ull))
        __asm__ volatile ("pause");
}

static void count_loop(volatile uint64_t *counter, uint64_t target)
{
    uint64_t i;
    for (i = 0; i < target; i++) {
        (*counter)++;
        if ((i & 255ull) == 0)
            stretch();
    }
}

static int wait_counters(volatile uint64_t *vals, const uint64_t *targets,
                         unsigned n, uint64_t timeout_ms)
{
    uint64_t start = robo_time_ms();
    unsigned i;
    for (;;) {
        int done = 1;
        for (i = 0; i < n; i++) {
            if (vals[i] < targets[i])
                done = 0;
        }
        if (done)
            return 1;
        if (robo_time_ms() - start > timeout_ms)
            return 0;
        robo_sleep(10);
    }
}

static void thread_a(void *arg)
{
    (void)arg;
    count_loop(&counter_a, CTX_AB_TARGET);
    g_a_done = 1;
}

static void thread_b(void *arg)
{
    (void)arg;
    count_loop(&counter_b, CTX_AB_TARGET);
    g_b_done = 1;
}

static void reg_thread(void *arg)
{
    unsigned n;
    (void)arg;
    for (n = 0; n < 40u; n++) {
        robo_reg_preserve_iters(&g_reg_ok, &g_reg_bad, 1024ull);
        stretch();
    }
    g_reg_done = 1;
}

static void reg_partner(void *arg)
{
    uint64_t i;
    (void)arg;
    for (i = 0; i < 40000ull; i++) {
        g_reg_partner++;
        if ((i & 255ull) == 0)
            stretch();
    }
    g_regp_done = 1;
}

static void stack_a(void *arg)
{
    uint64_t local = 0xA11AA11AA11AA11Aull;
    (void)arg;
    g_stack_a_addr = (uint64_t)(uintptr_t)&local;
    while (g_local_a < 30000ull) {
        if (local != 0xA11AA11AA11AA11Aull) {
            g_stack_fail = 1;
            return;
        }
        g_local_a++;
        if ((g_local_a & 255ull) == 0)
            stretch();
    }
}

static void stack_b(void *arg)
{
    uint64_t local = 0xB22BB22BB22BB22Bull;
    (void)arg;
    g_stack_b_addr = (uint64_t)(uintptr_t)&local;
    while (g_local_b < 30000ull) {
        if (local != 0xB22BB22BB22BB22Bull) {
            g_stack_fail = 1;
            return;
        }
        g_local_b++;
        if ((g_local_b & 255ull) == 0)
            stretch();
    }
}

static void busy_ms(uint64_t ms)
{
    uint64_t t = robo_time_now();
    while (robo_time_now() - t < ms * ROBO_NS_PER_MS)
        __asm__ volatile ("pause");
}

static void staged(void *arg)
{
    (void)arg;
    g_entries++;
    g_stage = 1;
    busy_ms(20);
    g_stage = 2;
    busy_ms(20);
    g_stage = 3;
    busy_ms(20);
    g_stage = 4;
    g_rip_done = 1;
}

static void stage_partner(void *arg)
{
    (void)arg;
    while (g_stage < 4) {
        g_stage_partner++;
        stretch();
    }
}

static void stress_entry(void *arg)
{
    uintptr_t i = (uintptr_t)arg;
    count_loop(&g_stress[i], CTX_STRESS_TARGET);
}

static void yielder(void *arg)
{
    unsigned i;
    (void)arg;
    for (i = 0; i < 16u; i++) {
        g_yield_n++;
        robo_thread_yield();
    }
}

static int wait_flag(volatile int *done, uint64_t timeout_ms)
{
    uint64_t start = robo_time_ms();
    while (!*done) {
        if (robo_time_ms() - start > timeout_ms)
            return 0;
        robo_sleep(10);
    }
    return 1;
}

int robo_context_selftest(void)
{
    RoboThread *tr, *tp, *sa, *sb, *st, *sp;
    RoboThread *stress[CTX_STRESS_N];
    RoboThread *ya, *yb;
    uint64_t sw0, prem0, y0;
    uint64_t sw1, prem1;
    uint64_t ab_sw, ab_prem;
    uint64_t targets[CTX_STRESS_N];
    unsigned i;
    int last_stage;
    int passes = 0;
    uint64_t ist1_before;

    kprintf("========================================\n");
    kprintf("      CONTEXT SWITCH TEST\n");
    kprintf("========================================\n\n");

    if (robo_scheduler_state() != ROBO_SCHED_RUNNING)
        return tfail("scheduler not running");

    ist1_before = robo_tss_ist1();
    if (!ist1_before)
        return tfail("IST1 missing");

    /* Register preservation */
    g_reg_ok = 0;
    g_reg_bad = 0;
    g_reg_partner = 0;
    g_reg_done = 0;
    g_regp_done = 0;
    tr = robo_thread_create("ctx-reg", reg_thread, 0);
    tp = robo_thread_create("ctx-regp", reg_partner, 0);
    if (!tr || !tp)
        return tfail("reg create");
    if (!wait_flag(&g_reg_done, 8000) || !wait_flag(&g_regp_done, 4000))
        return tfail("reg timeout");
    if (g_reg_bad)
        return tfail("register corruption");
    if (g_reg_ok < 100)
        return tfail("register loop did not run");
    robo_sched_reap();
    kprintf("[PASS] Register preservation\n");
    passes++;

    /* Stack isolation */
    g_local_a = 0;
    g_local_b = 0;
    g_stack_fail = 0;
    g_stack_a_addr = 0;
    g_stack_b_addr = 0;
    sa = robo_thread_create("ctx-sa", stack_a, 0);
    sb = robo_thread_create("ctx-sb", stack_b, 0);
    if (!sa || !sb)
        return tfail("stack create");
    {
        uint64_t start = robo_time_ms();
        while (g_local_a < 30000ull || g_local_b < 30000ull) {
            if (robo_time_ms() - start > 8000ull)
                return tfail("stack timeout");
            robo_sleep(10);
        }
    }
    if (g_stack_fail)
        return tfail("stack isolation");
    if (!g_stack_a_addr || !g_stack_b_addr || g_stack_a_addr == g_stack_b_addr)
        return tfail("shared stack");
    robo_sched_reap();
    kprintf("[PASS] Stack switching\n");
    passes++;

    /* RIP continues at the next stage, not thread_entry */
    g_stage = 0;
    g_entries = 0;
    g_stage_back = 0;
    g_stage_partner = 0;
    g_rip_done = 0;
    st = robo_thread_create("ctx-rip", staged, 0);
    sp = robo_thread_create("ctx-ripp", stage_partner, 0);
    if (!st || !sp)
        return tfail("rip create");
    last_stage = 0;
    {
        uint64_t start = robo_time_ms();
        while (g_stage < 4) {
            if (g_stage < last_stage) {
                g_stage_back = 1;
                break;
            }
            if (g_stage > last_stage)
                last_stage = g_stage;
            if (robo_time_ms() - start > 8000ull)
                return tfail("rip timeout");
            robo_sleep(5);
        }
    }
    if (g_stage_back)
        return tfail("rip restarted");
    if (g_stage != 4)
        return tfail("rip incomplete");
    if (g_entries != 1)
        return tfail("rip re-entered entry");
    if (!wait_flag(&g_rip_done, 2000))
        return tfail("rip term");
    robo_sched_reap();
    kprintf("[PASS] RIP restoration\n");
    passes++;

    /* Mandatory A/B counter test — no yield in the loop */
    counter_a = 0;
    counter_b = 0;
    g_a_done = 0;
    g_b_done = 0;
    sw0 = robo_sched_switches();
    prem0 = robo_sched_preemptions();
    y0 = robo_sched_yields();
    if (!robo_thread_create("ctx-a", thread_a, 0) ||
        !robo_thread_create("ctx-b", thread_b, 0))
        return tfail("A/B create");

    kprintf("Thread A target : %llu\n", (unsigned long long)CTX_AB_TARGET);
    kprintf("Thread B target : %llu\n", (unsigned long long)CTX_AB_TARGET);

    {
        uint64_t start = robo_time_ms();
        while (counter_a < CTX_AB_TARGET || counter_b < CTX_AB_TARGET) {
            if (robo_time_ms() - start > 20000ull)
                return tfail("A/B timeout");
            robo_sleep(10);
        }
    }
    if (!wait_flag(&g_a_done, 2000) || !wait_flag(&g_b_done, 2000))
        return tfail("A/B term");
    robo_sleep(20);

    sw1 = robo_sched_switches();
    prem1 = robo_sched_preemptions();
    ab_sw = sw1 - sw0;
    ab_prem = prem1 - prem0;

    if (counter_a != CTX_AB_TARGET)
        return tfail("A count");
    if (counter_b != CTX_AB_TARGET)
        return tfail("B count");
    if (ab_prem < 2)
        return tfail("no timer preemption");
    if (ab_sw < 4)
        return tfail("too few context switches");
    if (!g_a_done || !g_b_done)
        return tfail("A/B not terminated");
    if (robo_sched_yields() < y0)
        return tfail("yield counter wrapped");
    (void)y0;

    kprintf("\n");
    kprintf("Thread A result : %llu\n", (unsigned long long)counter_a);
    kprintf("Thread B result : %llu\n", (unsigned long long)counter_b);
    kprintf("\n");
    kprintf("Context switches : %llu\n", (unsigned long long)ab_sw);
    kprintf("Preemptions      : %llu\n", (unsigned long long)ab_prem);
    kprintf("\n");
    kprintf("A state          : TERMINATED\n");
    kprintf("B state          : TERMINATED\n");
    kprintf("\n");
    kprintf("[PASS] Context switching\n");
    kprintf("========================================\n");
    robo_sched_reap();
    passes++;

    /* Voluntary yield uses the same int $48 / iframe path */
    g_yield_n = 0;
    y0 = robo_sched_yields();
    ya = robo_thread_create("ctx-ya", yielder, 0);
    yb = robo_thread_create("ctx-yb", yielder, 0);
    if (!ya || !yb)
        return tfail("yield create");
    {
        uint64_t start = robo_time_ms();
        while (g_yield_n < 32ull) {
            if (robo_time_ms() - start > 4000ull)
                return tfail("yield timeout");
            robo_sleep(5);
        }
    }
    if (robo_sched_yields() <= y0)
        return tfail("voluntary switch");
    robo_sched_reap();
    kprintf("[PASS] Voluntary switching\n");
    passes++;

    /* 10-thread stress, twice */
    for (i = 0; i < 2u; i++) {
        unsigned t;
        for (t = 0; t < CTX_STRESS_N; t++)
            g_stress[t] = 0;
        for (t = 0; t < CTX_STRESS_N; t++) {
            stress[t] = robo_thread_create("ctx-n", stress_entry, (void *)(uintptr_t)t);
            if (!stress[t])
                return tfail("stress create");
            targets[t] = CTX_STRESS_TARGET;
        }
        if (!wait_counters(g_stress, targets, CTX_STRESS_N, 20000ull))
            return tfail("stress timeout");
        for (t = 0; t < CTX_STRESS_N; t++) {
            if (g_stress[t] != CTX_STRESS_TARGET)
                return tfail("stress count");
        }
        robo_sleep(20);
        robo_sched_reap();
    }
    kprintf("[PASS] Stress test\n");
    passes++;

    {
        if (robo_tss_ist1() != ist1_before)
            return tfail("IST1 mutated");
    }
    kprintf("[PASS] Double-fault IST preserved\n");
    passes++;

    robo_x86_context_report();
    kprintf("Context selftest %d checks\n", passes);
    (void)sp;
    return 1;
}
