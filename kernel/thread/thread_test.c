#include "thread/thread.h"
#include "scheduler/scheduler.h"
#include "arch/x86_64/scheduler/context_switch.h"
#include "mm/vmm/vmm.h"
#include "time/time.h"
#include "kio.h"

static volatile uint64_t g_pa, g_pb;
static volatile uint64_t g_spin;
static volatile int g_slept;
static volatile uint64_t g_sleep_runtime;
static volatile int g_blocked_flag;
static volatile int g_woke_flag;
static volatile int g_term_ret;
static RoboThread *g_blocker;
static RoboThread *g_sleeper;

static int tfail(const char *tag)
{
    kprintf("Thread selftest: %s\n", tag);
    return 0;
}

static void worker_a(void *arg)
{
    (void)arg;
    for (;;)
        g_pa++;
}

static void worker_b(void *arg)
{
    (void)arg;
    for (;;)
        g_pb++;
}

static void spinner(void *arg)
{
    (void)arg;
    for (;;)
        g_spin++;
}

static void sleeper(void *arg)
{
    RoboThread *self = robo_current_thread();
    (void)arg;
    g_sleeper = self;
    robo_sleep(100);
    if (self)
        g_sleep_runtime = self->runtime;
    g_slept = 1;
    for (;;)
        robo_thread_yield();
}

static void blocker(void *arg)
{
    (void)arg;
    g_blocker = robo_current_thread();
    g_blocked_flag = 1;
    robo_thread_block();
    g_woke_flag = 1;
    for (;;)
        robo_thread_yield();
}

static void term_return(void *arg)
{
    (void)arg;
    g_term_ret = 1;
}

static int test_transitions(void)
{
    RoboThread t;

    robo_thread_zero(&t);
    t.state = ROBO_THREAD_NEW;
    if (!robo_thread_transition(&t, ROBO_THREAD_READY) || t.state != ROBO_THREAD_READY)
        return tfail("NEW -> READY");
    kprintf("[PASS] NEW -> READY\n");

    if (!robo_thread_transition(&t, ROBO_THREAD_RUNNING) || t.state != ROBO_THREAD_RUNNING)
        return tfail("READY -> RUNNING");
    kprintf("[PASS] READY -> RUNNING\n");

    if (!robo_thread_transition(&t, ROBO_THREAD_READY) || t.state != ROBO_THREAD_READY)
        return tfail("RUNNING -> READY");
    kprintf("[PASS] RUNNING -> READY\n");

    if (!robo_thread_transition(&t, ROBO_THREAD_RUNNING))
        return tfail("READY -> RUNNING (2)");
    if (!robo_thread_transition(&t, ROBO_THREAD_BLOCKED) || t.state != ROBO_THREAD_BLOCKED)
        return tfail("RUNNING -> BLOCKED");
    kprintf("[PASS] RUNNING -> BLOCKED\n");

    if (!robo_thread_transition(&t, ROBO_THREAD_READY) || t.state != ROBO_THREAD_READY)
        return tfail("BLOCKED -> READY");
    kprintf("[PASS] BLOCKED -> READY\n");

    if (!robo_thread_transition(&t, ROBO_THREAD_RUNNING))
        return tfail("READY -> RUNNING (3)");
    if (!robo_thread_transition(&t, ROBO_THREAD_SLEEPING) || t.state != ROBO_THREAD_SLEEPING)
        return tfail("RUNNING -> SLEEPING");
    kprintf("[PASS] RUNNING -> SLEEPING\n");

    if (!robo_thread_transition(&t, ROBO_THREAD_READY) || t.state != ROBO_THREAD_READY)
        return tfail("SLEEPING -> READY");
    kprintf("[PASS] SLEEPING -> READY\n");

    if (!robo_thread_transition(&t, ROBO_THREAD_RUNNING))
        return tfail("READY -> RUNNING (4)");
    if (!robo_thread_transition(&t, ROBO_THREAD_TERMINATED) ||
        t.state != ROBO_THREAD_TERMINATED)
        return tfail("RUNNING -> TERMINATED");
    kprintf("[PASS] RUNNING -> TERMINATED\n");

    if (robo_thread_transition(&t, ROBO_THREAD_READY) ||
        robo_thread_transition(&t, ROBO_THREAD_RUNNING))
        return tfail("rejected TERMINATED transition");
    kprintf("[PASS] Invalid TERMINATED transitions rejected\n");
    return 1;
}

int robo_thread_selftest(void)
{
    RoboThread *wa, *wb, *sp, *sl, *bl, *tr;
    RoboThread *cur, *boot, *idle;
    phys_addr_t kcr3;
    uint64_t spin0;
    int passes = 0;

    kprintf("Thread Self-Test\n");
    kprintf("----------------\n");

    if (robo_scheduler_state() != ROBO_SCHED_RUNNING)
        return tfail("scheduler not running");

    cur = robo_current_thread();
    boot = robo_bootstrap_task();
    idle = robo_idle_task();
    if (!cur || cur != boot || robo_thread_id(boot) != ROBO_THREAD_ID_BOOTSTRAP)
        return tfail("current thread");
    kprintf("[PASS] Current thread\n");
    passes++;

    if (!idle || robo_thread_id(idle) != ROBO_THREAD_ID_IDLE)
        return tfail("idle id");
    kprintf("[PASS] Idle thread identity\n");
    passes++;

    if (!test_transitions())
        return 0;
    passes++;

    wa = robo_thread_create("worker-a", worker_a, 0);
    if (!wa)
        return tfail("create worker-a");
    kprintf("[PASS] Thread object created\n");
    if (robo_thread_id(wa) == ROBO_THREAD_ID_BOOTSTRAP ||
        robo_thread_id(wa) == ROBO_THREAD_ID_IDLE)
        return tfail("id assigned");
    kprintf("[PASS] Thread ID assigned\n");
    if (robo_thread_state(wa) != ROBO_THREAD_READY)
        return tfail("initial state");
    kprintf("[PASS] Initial state correct\n");
    if (!wa->kernel_stack || !wa->kernel_stack_size)
        return tfail("stack allocated");
    kprintf("[PASS] Stack allocated\n");
    if (!wa->context || !robo_thread_ip(wa))
        return tfail("cpu context");
    kprintf("[PASS] CPU context initialized\n");
    if (wa->address_space != robo_kernel_address_space())
        return tfail("address space");
    kprintf("[PASS] Address space bound\n");
    if (!robo_thread_is_valid(wa))
        return tfail("valid");
    passes += 6;

    wb = robo_thread_create("worker-b", worker_b, 0);
    if (!wb)
        return tfail("create worker-b");

    robo_thread_dump(wa);
    g_pa = 0;
    g_pb = 0;
    robo_sleep(200);
    if (g_pa == 0 || g_pb == 0)
        return tfail("preemption");
    kprintf("[PASS] Timer preemption\n");
    kprintf("Thread A counter: %llu\n", (unsigned long long)g_pa);
    kprintf("Thread B counter: %llu\n", (unsigned long long)g_pb);
    passes++;

    kcr3 = robo_vmm_get_cr3();
    if (!wa->address_space || !wb->address_space || !boot->address_space)
        return tfail("aspace ptr");
    if (wa->address_space->cr3 != kcr3 ||
        wb->address_space->cr3 != kcr3 ||
        boot->address_space->cr3 != kcr3)
        return tfail("shared kernel aspace");
    if (wa->address_space != wb->address_space)
        return tfail("aspace identity");
    kprintf("[PASS] Shared kernel address space\n");
    passes++;

    if (wa->kernel_stack == wb->kernel_stack ||
        wa->stack_top == wb->stack_top)
        return tfail("shared kernel stack");
    kprintf("[PASS] Stack isolation\n");
    passes++;

    g_spin = 0;
    g_slept = 0;
    sl = robo_thread_create("sleeper", sleeper, 0);
    sp = robo_thread_create("spinner", spinner, 0);
    if (!sl || !sp)
        return tfail("sleep create");
    spin0 = g_spin;
    robo_sleep(250);
    if (!g_slept)
        return tfail("sleeper did not wake");
    if (g_spin <= spin0)
        return tfail("spinner stalled during sleep");
    kprintf("[PASS] Sleeping\n");
    kprintf("[PASS] Wake-up\n");
    passes += 2;

    g_blocked_flag = 0;
    g_woke_flag = 0;
    bl = robo_thread_create("blocker", blocker, 0);
    if (!bl)
        return tfail("block create");
    robo_sleep(250);
    if (!g_blocked_flag || !g_blocker)
        return tfail("blocker never ran");
    if (robo_thread_state(g_blocker) != ROBO_THREAD_BLOCKED)
        return tfail("not blocked");
    if (!robo_thread_wake(g_blocker))
        return tfail("wake");
    robo_sleep(250);
    if (!g_woke_flag)
        return tfail("block wake");
    kprintf("[PASS] Blocking\n");
    kprintf("[PASS] Blocked thread wake\n");
    passes += 2;

    g_term_ret = 0;
    tr = robo_thread_create("term-ret", term_return, 0);
    if (!tr)
        return tfail("term create");
    robo_sleep(250);
    robo_sched_reap();
    if (!g_term_ret)
        return tfail("term return");
    kprintf("[PASS] Termination\n");
    passes++;

    (void)sp;
    (void)sl;
    (void)bl;
    (void)tr;
    kprintf("Thread selftest %d checks\n", passes);
    return 1;
}
