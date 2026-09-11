#include "scheduler/sched_internal.h"
#include "arch/x86_64/exception.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/tss.h"
#include "arch/x86_64/gdt.h"
#include "mm/heap/heap.h"
#include "time/time.h"
#include "time/convert.h"
#include "kio.h"

extern void isr_48(void);
extern char kstack[];
extern char kstack_end[];

robo_task_t *g_current;
robo_task_t *g_idle;
robo_task_t *g_bootstrap;
robo_task_t *g_all;
robo_runqueue_t g_ready;
robo_sched_state_t g_sched_state;
unsigned g_preempt_count;

static uint64_t g_next_id = 2;
static int g_in_sched;
static robo_task_t *g_reap;
static uint64_t g_switches;
static uint64_t g_preemptions;
static uint64_t g_yields;
static uint64_t g_creates;
static uint64_t g_terminations;

#define ROBO_RR_LOG 64
static uint64_t g_rr[ROBO_RR_LOG];
static unsigned g_rr_i;
static unsigned g_rr_n;

static void sched_halt(void)
{
    for (;;)
        __asm__ volatile ("cli; hlt");
}

uint64_t robo_sched_lock(void)
{
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

void robo_sched_unlock(uint64_t flags)
{
    __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory");
}

static void sched_panic_init(const char *msg)
{
    kprintf("========================================\n");
    kprintf("          KERNEL PANIC\n");
    kprintf("========================================\n\n");
    kprintf("Scheduler initialization failed\n\n");
    kprintf("%s\n", msg ? msg : "unknown");
    kprintf("\n========================================\n");
    sched_halt();
}

void robo_sched_panic(const char *msg)
{
    kprintf("========================================\n");
    kprintf("          KERNEL PANIC\n");
    kprintf("========================================\n\n");
    kprintf("%s\n", msg ? msg : "scheduler panic");
    kprintf("\n========================================\n");
    sched_halt();
}

void robo_sched_panic_corrupt(const char *detail, robo_task_t *cur, robo_task_t *next)
{
    kprintf("========================================\n");
    kprintf("          KERNEL PANIC\n");
    kprintf("========================================\n\n");
    kprintf("Scheduler corruption\n\n");
    kprintf("Current Task : %llu %s\n",
            (unsigned long long)(cur ? cur->id : 0),
            cur ? cur->name : "?");
    kprintf("Next Task    : %llu %s\n",
            (unsigned long long)(next ? next->id : 0),
            next ? next->name : "?");
    kprintf("State        : %u\n", (unsigned)g_sched_state);
    kprintf("Detail       : %s\n", detail ? detail : "unknown");
    kprintf("\n========================================\n");
    sched_halt();
}

uint64_t robo_sched_quantum_ns(void)
{
    return (uint64_t)ROBO_SCHEDULER_QUANTUM_MS * ROBO_NS_PER_MS;
}

static int id_in_use(uint64_t id)
{
    robo_task_t *t;
    for (t = g_all; t; t = t->all_next) {
        if (t->id == id)
            return 1;
    }
    return 0;
}

uint64_t robo_sched_alloc_id(void)
{
    uint64_t start = g_next_id;
    uint64_t id;
    for (;;) {
        id = g_next_id++;
        if (g_next_id < 2)
            g_next_id = 2;
        if (id >= 2 && !id_in_use(id))
            return id;
        if (g_next_id == start)
            robo_sched_panic("task ID space exhausted");
    }
}

void robo_sched_all_add(robo_task_t *task)
{
    if (!task)
        return;
    task->all_next = g_all;
    g_all = task;
}

void robo_sched_note_create(void)
{
    g_creates++;
}

void robo_sched_note_term(void)
{
    g_terminations++;
}

void robo_sched_reap_later(robo_task_t *task)
{
    if (!task)
        return;
    task->reap_next = g_reap;
    g_reap = task;
}

static void all_remove(robo_task_t *task)
{
    robo_task_t **p;
    for (p = &g_all; *p; p = &(*p)->all_next) {
        if (*p == task) {
            *p = task->all_next;
            task->all_next = 0;
            return;
        }
    }
}

void robo_sched_reap(void)
{
    robo_task_t *t, *next;
    uint64_t flags;

    flags = robo_sched_lock();
    t = g_reap;
    g_reap = 0;
    robo_sched_unlock(flags);

    while (t) {
        next = t->reap_next;
        t->reap_next = 0;
        if (t == g_current || t == g_idle || t == g_bootstrap) {
            t = next;
            continue;
        }
        all_remove(t);
        robo_task_stack_free(t);
        kfree(t);
        t = next;
    }
}

static void account(robo_task_t *t, uint64_t now)
{
    if (!t || !t->last_start_ns)
        return;
    if (now >= t->last_start_ns)
        t->runtime_ns += now - t->last_start_ns;
}

static void rr_note(uint64_t id)
{
    g_rr[g_rr_i] = id;
    g_rr_i = (g_rr_i + 1u) % ROBO_RR_LOG;
    if (g_rr_n < ROBO_RR_LOG)
        g_rr_n++;
}

static struct robo_iframe *pick_and_switch(struct robo_iframe *frame, int force)
{
    robo_task_t *old;
    robo_task_t *next;
    uint64_t now;

    if (g_sched_state != ROBO_SCHED_RUNNING || !frame || !g_current)
        return frame;
    if (g_in_sched)
        return frame;
    if (g_preempt_count)
        return frame;

    g_in_sched = 1;
    old = g_current;
    old->iframe = frame;
    now = robo_time_now();
    account(old, now);

    if (!force && now < old->slice_end_ns) {
        g_in_sched = 0;
        return frame;
    }

    int was_running = (old->state == ROBO_TASK_RUNNING);

    if (old != g_idle && was_running) {
        old->state = ROBO_TASK_READY;
        robo_rq_enqueue(&g_ready, old);
    }

    next = robo_rq_dequeue(&g_ready);
    if (!next)
        next = g_idle;
    if (!next)
        robo_sched_panic_corrupt("no next task", old, 0);

    if (next == old) {
        old->state = ROBO_TASK_RUNNING;
        old->last_start_ns = now;
        old->slice_end_ns = now + robo_sched_quantum_ns();
        g_in_sched = 0;
        return frame;
    }

    if (!next->iframe)
        robo_sched_panic_corrupt("next iframe missing", old, next);

    g_switches++;
    if (force && was_running)
        g_yields++;
    else if (!force)
        g_preemptions++;
    rr_note(next->id);

    if (old->state == ROBO_TASK_TERMINATED)
        robo_sched_reap_later(old);

    g_current = next;
    next->state = ROBO_TASK_RUNNING;
    next->last_start_ns = now;
    next->slice_end_ns = now + robo_sched_quantum_ns();
    robo_tss_set_rsp0(next->stack_top);
    g_in_sched = 0;
    return next->iframe;
}

struct robo_iframe *robo_scheduler_tick(struct robo_iframe *frame)
{
    return pick_and_switch(frame, 0);
}

struct robo_iframe *robo_scheduler_resched(struct robo_iframe *frame)
{
    return pick_and_switch(frame, 1);
}

static void sleep_wake(void *ctx)
{
    robo_task_t *t = (robo_task_t *)ctx;
    if (!t)
        return;
    t->sleep_timer = 0;
    if (t->state != ROBO_TASK_SLEEPING)
        return;
    t->state = ROBO_TASK_READY;
    if (!robo_rq_contains(&g_ready, t))
        robo_rq_enqueue(&g_ready, t);
}

int robo_scheduler_sleep(uint64_t milliseconds)
{
    robo_task_t *t;
    uint64_t deadline;
    uint64_t flags;
    int handle;

    if (g_sched_state != ROBO_SCHED_RUNNING || milliseconds == 0)
        return 0;
    t = g_current;
    if (!t || t == g_idle)
        return 0;

    deadline = robo_time_deadline(robo_time_now(), milliseconds * ROBO_NS_PER_MS);
    flags = robo_sched_lock();
    t->state = ROBO_TASK_SLEEPING;
    handle = robo_timer_add(deadline, 0, sleep_wake, t);
    t->sleep_timer = handle;
    robo_sched_unlock(flags);
    if (!handle) {
        flags = robo_sched_lock();
        if (t->state == ROBO_TASK_SLEEPING)
            t->state = ROBO_TASK_RUNNING;
        robo_sched_unlock(flags);
        return 0;
    }
    __asm__ volatile ("int $48" ::: "memory");
    return 1;
}

robo_task_t *robo_current_task(void)
{
    return g_current;
}

robo_task_t *robo_idle_task(void)
{
    return g_idle;
}

robo_task_t *robo_bootstrap_task(void)
{
    return g_bootstrap;
}

robo_sched_state_t robo_scheduler_state(void)
{
    return g_sched_state;
}

int robo_scheduler_ready(void)
{
    return g_sched_state == ROBO_SCHED_INITIALIZED ||
           g_sched_state == ROBO_SCHED_RUNNING;
}

void robo_preempt_disable(void)
{
    uint64_t flags = robo_sched_lock();
    g_preempt_count++;
    robo_sched_unlock(flags);
}

void robo_preempt_enable(void)
{
    uint64_t flags = robo_sched_lock();
    if (g_preempt_count)
        g_preempt_count--;
    robo_sched_unlock(flags);
}

unsigned robo_preempt_depth(void)
{
    return g_preempt_count;
}

uint64_t robo_sched_switches(void) { return g_switches; }
uint64_t robo_sched_preemptions(void) { return g_preemptions; }
uint64_t robo_sched_yields(void) { return g_yields; }
uint64_t robo_sched_creates(void) { return g_creates; }
uint64_t robo_sched_terminations(void) { return g_terminations; }

unsigned robo_sched_runnable_count(void)
{
    unsigned n = robo_rq_count(&g_ready);
    if (g_current && g_current->state == ROBO_TASK_RUNNING && g_current != g_idle)
        n++;
    return n;
}

static int adopt_bootstrap(void)
{
    robo_task_t *t = (robo_task_t *)kmalloc(sizeof(*t));
    if (!t)
        return 0;
    robo_task_zero(t);
    robo_task_set_name(t, "bootstrap");
    t->id = ROBO_TID_BOOTSTRAP;
    t->state = ROBO_TASK_RUNNING;
    t->kernel_stack = kstack;
    t->kernel_stack_size = (size_t)((uintptr_t)kstack_end - (uintptr_t)kstack);
    t->stack_top = (uint64_t)(uintptr_t)kstack_end;
    t->owns_stack = 0;
    t->last_start_ns = robo_time_now();
    t->slice_end_ns = t->last_start_ns + robo_sched_quantum_ns();
    robo_sched_all_add(t);
    g_bootstrap = t;
    g_current = t;
    return 1;
}

int robo_scheduler_init(void)
{
    if (g_sched_state != ROBO_SCHED_UNINIT)
        sched_panic_init("scheduler_init called twice");
    if (!robo_timer_ready() || !robo_heap_ready())
        sched_panic_init("timer or heap not ready");

    robo_rq_init(&g_ready);
    g_next_id = 2;
    g_preempt_count = 0;
    g_in_sched = 0;
    g_reap = 0;
    g_all = 0;

    if (!adopt_bootstrap())
        sched_panic_init("bootstrap task allocation failed");

    g_sched_state = ROBO_SCHED_INITIALIZED;

    g_idle = robo_task_create("idle", robo_idle_entry, 0);
    if (!g_idle)
        sched_panic_init("idle task creation failed");

    /*
     * Idle must not sit in the round-robin queue. It runs only when
     * the ready queue is empty. Create placed it on the queue; remove it
     * and force TID 1.
     */
    {
        uint64_t flags = robo_sched_lock();
        robo_rq_remove(&g_ready, g_idle);
        g_idle->id = ROBO_TID_IDLE;
        g_idle->state = ROBO_TASK_READY;
        robo_sched_unlock(flags);
    }

    robo_idt_set(ROBO_VEC_RESCHED, isr_48, 0);
    if (!robo_idt_gate_present(ROBO_VEC_RESCHED))
        sched_panic_init("reschedule vector missing");

    g_sched_state = ROBO_SCHED_INITIALIZED;
    kprintf("Scheduler init  quantum %u ms  bootstrap 0 idle 1\n",
            (unsigned)ROBO_SCHEDULER_QUANTUM_MS);
    return 1;
}

void robo_scheduler_start(void)
{
    uint64_t flags;
    uint64_t now;

    if (g_sched_state == ROBO_SCHED_RUNNING)
        robo_sched_panic("scheduler_start called twice");
    if (g_sched_state != ROBO_SCHED_INITIALIZED)
        robo_sched_panic("scheduler_start before init");
    if (!g_current || !g_idle)
        robo_sched_panic("scheduler_start missing tasks");

    flags = robo_sched_lock();
    now = robo_time_now();
    g_current->last_start_ns = now;
    g_current->slice_end_ns = now + robo_sched_quantum_ns();
    g_current->state = ROBO_TASK_RUNNING;
    g_sched_state = ROBO_SCHED_RUNNING;
    robo_tss_set_rsp0(g_current->stack_top);
    robo_sched_unlock(flags);
    kprintf("Scheduler start  current %llu %s\n",
            (unsigned long long)g_current->id, g_current->name);
}

void robo_scheduler_report(void)
{
    robo_task_t *t;
    const char *st = "UNINIT";
    if (g_sched_state == ROBO_SCHED_INITIALIZED)
        st = "INITIALIZED";
    else if (g_sched_state == ROBO_SCHED_RUNNING)
        st = "RUNNING";
    else if (g_sched_state == ROBO_SCHED_STOPPED)
        st = "STOPPED";

    kprintf("Scheduler\n");
    kprintf("---------\n");
    kprintf("State       : %s\n", st);
    kprintf("Current     : Task %llu %s\n",
            (unsigned long long)(g_current ? g_current->id : 0),
            g_current ? g_current->name : "-");
    kprintf("Runnable    : %u\n", robo_sched_runnable_count());
    kprintf("Quantum     : %u ms\n", (unsigned)ROBO_SCHEDULER_QUANTUM_MS);
    kprintf("Switches    : %llu\n", (unsigned long long)g_switches);
    kprintf("ID    Name       State      Runtime\n");
    kprintf("-----------------------------------\n");
    for (t = g_all; t; t = t->all_next) {
        kprintf("%llu    %s  %s  %llu\n",
                (unsigned long long)t->id,
                t->name,
                robo_task_state_name(t->state),
                (unsigned long long)t->runtime_ns);
    }
}

#ifndef SCHED_HOST_TEST
void robo_scheduler_fb_report(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    kfb_puts("Sched  q ", mute);
    kfb_putu((unsigned long long)ROBO_SCHEDULER_QUANTUM_MS, white);
    kfb_puts(" ms  sw ", mute);
    kfb_putu((unsigned long long)g_switches, white);
    kfb_puts("  prem ", mute);
    kfb_putu((unsigned long long)g_preemptions, white);
    kfb_puts("\n", mute);
}
#endif

unsigned robo_sched_rr_log(uint64_t *out, unsigned max)
{
    unsigned i, n;
    if (!out || !max)
        return 0;
    n = g_rr_n < max ? g_rr_n : max;
    for (i = 0; i < n; i++) {
        unsigned idx;
        if (g_rr_n < ROBO_RR_LOG)
            idx = i;
        else
            idx = (g_rr_i + i) % ROBO_RR_LOG;
        out[i] = g_rr[idx];
    }
    return n;
}
