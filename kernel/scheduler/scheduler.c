#include "scheduler/sched_internal.h"
#include "arch/x86_64/exception.h"
#include "arch/x86_64/context/context_switch.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/tss.h"
#include "arch/x86_64/gdt.h"
#include "mm/heap/heap.h"
#include "mm/vmm/vmm.h"
#include "time/time.h"
#include "time/convert.h"
#include "kio.h"
#include "thread/thread.h"
#include "process/process.h"

extern void isr_48(void);
extern char kstack[];
extern char kstack_end[];

RoboThread *g_current;
RoboThread *g_idle;
RoboThread *g_bootstrap;
RoboThread *g_all;
robo_runqueue_t g_ready;
robo_sched_state_t g_sched_state;
unsigned g_preempt_count;

static uint64_t g_next_id = 2;
static int g_in_sched;
static RoboThread *g_reap;
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

void robo_sched_panic_corrupt(const char *detail, RoboThread *cur, RoboThread *next)
{
    kprintf("========================================\n");
    kprintf("          KERNEL PANIC\n");
    kprintf("========================================\n\n");
    kprintf("Scheduler corruption\n\n");
    kprintf("Current Thread : %llu %s\n",
            (unsigned long long)(cur ? cur->id : 0),
            cur ? cur->name : "?");
    kprintf("Next Thread    : %llu %s\n",
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
    RoboThread *t;
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
            robo_sched_panic("thread ID space exhausted");
    }
}

void robo_sched_all_add(RoboThread *thread)
{
    if (!thread)
        return;
    thread->all_next = g_all;
    g_all = thread;
}

void robo_sched_note_create(void)
{
    g_creates++;
}

void robo_sched_note_term(void)
{
    g_terminations++;
}

void robo_sched_reap_later(RoboThread *thread)
{
    if (!thread)
        return;
    thread->reap_next = g_reap;
    g_reap = thread;
}

static void all_remove(RoboThread *thread)
{
    RoboThread **p;
    for (p = &g_all; *p; p = &(*p)->all_next) {
        if (*p == thread) {
            *p = thread->all_next;
            thread->all_next = 0;
            return;
        }
    }
}

void robo_sched_reap(void)
{
    RoboThread *t, *next;
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
        robo_thread_stack_free(t);
        kfree(t);
        t = next;
    }
}

static void account(RoboThread *t, uint64_t now)
{
    if (!t || !t->last_start_time)
        return;
    if (now >= t->last_start_time)
        t->runtime += now - t->last_start_time;
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
    RoboThread *old;
    RoboThread *next;
    uint64_t now;
    int was_running;

    if (g_sched_state != ROBO_SCHED_RUNNING || !frame || !g_current)
        return frame;
    if (g_in_sched)
        return frame;
    if (g_preempt_count)
        return frame;

    g_in_sched = 1;
    old = g_current;
    old->context = frame;
    now = robo_time_now();
    account(old, now);

    if (!force && now < old->slice_end_ns) {
        g_in_sched = 0;
        return frame;
    }

    was_running = (old->state == ROBO_THREAD_RUNNING);

    if (was_running) {
        if (!robo_thread_transition(old, ROBO_THREAD_READY))
            robo_sched_panic_corrupt("RUNNING -> READY", old, 0);
        if (old != g_idle)
            robo_rq_enqueue(&g_ready, old);
    }

    next = robo_rq_dequeue(&g_ready);
    if (!next)
        next = g_idle;
    if (!next)
        robo_sched_panic_corrupt("no next thread", old, 0);

    if (next == old) {
        if (!robo_thread_transition(old, ROBO_THREAD_RUNNING))
            old->state = ROBO_THREAD_RUNNING;
        old->last_start_time = now;
        old->slice_end_ns = now + robo_sched_quantum_ns();
        g_in_sched = 0;
        return frame;
    }

    if (!next->context || !robo_x86_iframe_valid(next->context))
        robo_x86_context_fail(old->context, next->context,
                              old->id, next->id,
                              robo_thread_state_name(old->state),
                              robo_thread_state_name(next->state));
    if (next->state != ROBO_THREAD_READY && next != g_idle)
        robo_sched_panic_corrupt("next not READY", old, next);

    g_switches++;
    if (force && was_running)
        g_yields++;
    else if (!force)
        g_preemptions++;
    rr_note(next->id);

    if (old->state == ROBO_THREAD_TERMINATED)
        robo_sched_reap_later(old);

    robo_arch_prepare_thread_switch(old, next);

    g_current = next;
    if (robo_current_process() != next->process)
        robo_process_panic_relation(next,
            "Thread is executing with an inconsistent process association.");
    if (!robo_thread_transition(next, ROBO_THREAD_RUNNING)) {
        if (next == g_idle && next->state == ROBO_THREAD_READY)
            next->state = ROBO_THREAD_RUNNING;
        else
            robo_sched_panic_corrupt("READY -> RUNNING", old, next);
    }
    next->last_start_time = now;
    next->slice_end_ns = now + robo_sched_quantum_ns();
    /* Kernel stacks only. IST1 / double-fault stack is not touched. */
    robo_tss_set_rsp0(next->stack_top);
    g_in_sched = 0;
    /*
     * g_current is already next, so a restored thread sees itself.
     * The old iframe was stored in old->context before this switch.
     */
    return robo_x86_context_switch(old->context, next->context);
}

struct robo_iframe *robo_scheduler_tick(struct robo_iframe *frame)
{
    return pick_and_switch(frame, 0);
}

struct robo_iframe *robo_scheduler_resched(struct robo_iframe *frame)
{
    return pick_and_switch(frame, 1);
}

RoboThread *robo_current_task(void)
{
    return g_current;
}

RoboThread *robo_idle_task(void)
{
    return g_idle;
}

RoboThread *robo_bootstrap_task(void)
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
    if (g_current && g_current->state == ROBO_THREAD_RUNNING && g_current != g_idle)
        n++;
    return n;
}

static int adopt_bootstrap(void)
{
    RoboThread *t;

    t = robo_thread_adopt_bootstrap(kstack, (uint64_t)(uintptr_t)kstack_end,
                                    (size_t)((uintptr_t)kstack_end - (uintptr_t)kstack));
    if (!t)
        return 0;
    robo_sched_all_add(t);
    g_bootstrap = t;
    g_current = t;
    return 1;
}

int robo_scheduler_init(void)
{
    if (g_sched_state != ROBO_SCHED_UNINIT)
        sched_panic_init("scheduler_init called twice");
    if (!robo_timer_ready() || !robo_heap_ready() || !robo_vmm_ready())
        sched_panic_init("timer, heap, or VMM not ready");

    if (!robo_thread_subsystem_init())
        sched_panic_init("thread subsystem failed");
    if (!robo_process_subsystem_init())
        sched_panic_init("process subsystem failed");

    robo_rq_init(&g_ready);
    g_next_id = 2;
    g_preempt_count = 0;
    g_in_sched = 0;
    g_reap = 0;
    g_all = 0;

    if (!adopt_bootstrap())
        sched_panic_init("bootstrap thread allocation failed");
    kprintf("[ OK ] Bootstrap thread\n");

    g_sched_state = ROBO_SCHED_INITIALIZED;

    g_idle = robo_thread_create("idle", robo_idle_entry, 0);
    if (!g_idle)
        sched_panic_init("idle thread creation failed");

    {
        uint64_t flags = robo_sched_lock();
        robo_rq_remove(&g_ready, g_idle);
        g_idle->id = ROBO_THREAD_ID_IDLE;
        if (g_idle->state != ROBO_THREAD_READY)
            g_idle->state = ROBO_THREAD_READY;
        robo_sched_unlock(flags);
    }
    kprintf("[ OK ] Idle thread\n");
    kprintf("[ OK ] Scheduler integration\n");

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
        robo_sched_panic("scheduler_start missing threads");

    flags = robo_sched_lock();
    now = robo_time_now();
    g_current->last_start_time = now;
    g_current->slice_end_ns = now + robo_sched_quantum_ns();
    if (g_current->state != ROBO_THREAD_RUNNING)
        g_current->state = ROBO_THREAD_RUNNING;
    g_sched_state = ROBO_SCHED_RUNNING;
    robo_tss_set_rsp0(g_current->stack_top);
    robo_sched_unlock(flags);
    kprintf("Scheduler start  current %llu %s\n",
            (unsigned long long)g_current->id, g_current->name);
}

void robo_scheduler_report(void)
{
    RoboThread *t;
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
    kprintf("Current     : Thread %llu %s\n",
            (unsigned long long)(g_current ? g_current->id : 0),
            g_current ? g_current->name : "-");
    kprintf("Runnable    : %u\n", robo_sched_runnable_count());
    kprintf("Quantum     : %u ms\n", (unsigned)ROBO_SCHEDULER_QUANTUM_MS);
    kprintf("Switches    : %llu\n", (unsigned long long)g_switches);
    robo_thread_report();
    kprintf("ID    Name       State      Runtime\n");
    kprintf("-----------------------------------\n");
    for (t = g_all; t; t = t->all_next) {
        kprintf("%llu    %s  %s  %llu\n",
                (unsigned long long)t->id,
                t->name,
                robo_thread_state_name(t->state),
                (unsigned long long)t->runtime);
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
