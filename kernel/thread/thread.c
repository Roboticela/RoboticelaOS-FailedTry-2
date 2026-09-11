#include "thread/thread.h"
#include "process/process.h"
#include "scheduler/sched_internal.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/exception.h"
#include "arch/x86_64/context/context_switch.h"
#include "mm/pmm.h"
#include "mm/vmm/vmm.h"
#include "mm/heap/heap.h"
#include "mm/page.h"
#include "time/time.h"
#include "time/convert.h"
#include "kio.h"

#define ROBO_KSTACK_SLOT_PAGES \
    (ROBO_THREAD_KSTACK_GUARD_PAGES + ROBO_THREAD_KSTACK_PAGES)

static uint64_t g_stack_slot;
static RoboAddressSpace g_kernel_aspace;
static int g_thread_ready;

static void bytes_zero(void *dst, size_t n)
{
    uint8_t *p = (uint8_t *)dst;
    while (n--)
        *p++ = 0;
}

RoboAddressSpace *robo_kernel_address_space(void)
{
    return &g_kernel_aspace;
}

int robo_thread_subsystem_init(void)
{
    g_kernel_aspace.cr3 = robo_vmm_get_cr3();
    g_kernel_aspace.cloned = 0;
    g_kernel_aspace.maps = 0;
    g_stack_slot = 0;
    g_thread_ready = 1;
    (void)g_thread_ready;
    kprintf("Thread Subsystem\n");
    kprintf("----------------\n");
    kprintf("[ OK ] Initialized\n");
    kprintf("[ OK ] Thread ID allocator\n");
    kprintf("[ OK ] Thread stacks\n");
    return 1;
}

void robo_thread_zero(RoboThread *thread)
{
    if (thread)
        bytes_zero(thread, sizeof(*thread));
}

void robo_thread_set_name(RoboThread *thread, const char *name)
{
    unsigned i;
    if (!thread)
        return;
    if (!name)
        name = "thread";
    for (i = 0; i + 1u < ROBO_THREAD_NAME_MAX && name[i]; i++)
        thread->name[i] = name[i];
    thread->name[i] = 0;
}

robo_thread_id_t robo_thread_id(const RoboThread *thread)
{
    return thread ? thread->id : (robo_thread_id_t)-1;
}

robo_thread_state_t robo_thread_state(const RoboThread *thread)
{
    return thread ? thread->state : ROBO_THREAD_TERMINATED;
}

uint64_t robo_thread_ip(const RoboThread *thread)
{
    return thread ? robo_thread_context_ip(thread->context) : 0;
}

uint64_t robo_thread_sp(const RoboThread *thread)
{
    if (!thread)
        return 0;
    if (thread->context)
        return robo_thread_context_sp(thread->context);
    return thread->stack_top;
}

int robo_thread_transition(RoboThread *thread, robo_thread_state_t next)
{
    if (!thread)
        return 0;
    if (!robo_thread_transition_allowed(thread->state, next))
        return 0;
    thread->state = next;
    return 1;
}

int robo_thread_is_valid(const RoboThread *thread)
{
    if (!thread)
        return 0;
    if (!robo_thread_state_valid(thread->state))
        return 0;
    if (thread->state == ROBO_THREAD_NEW)
        return 1;
    if (!thread->kernel_stack || !thread->kernel_stack_size)
        return 0;
    if (thread->state != ROBO_THREAD_TERMINATED && !thread->address_space)
        return 0;
    if (thread->id == ROBO_THREAD_ID_BOOTSTRAP)
        return 1;
    if (thread->state == ROBO_THREAD_READY || thread->state == ROBO_THREAD_RUNNING) {
        if (!thread->context)
            return 0;
    }
    return 1;
}

int robo_thread_stack_alloc(RoboThread *thread)
{
    unsigned i;
    virt_addr_t guard, base, va;
    phys_addr_t pa;

    if (!thread)
        return 0;

    guard = ROBO_VMM_KSTACK_BASE + g_stack_slot * ROBO_KSTACK_SLOT_PAGES * ROBO_PAGE_SIZE;
    g_stack_slot++;
    base = guard + ROBO_PAGE_SIZE;

    for (i = 0; i < ROBO_THREAD_KSTACK_PAGES; i++) {
        va = base + (virt_addr_t)i * ROBO_PAGE_SIZE;
        pa = robo_page_alloc();
        if (pa == ROBO_INVALID_PHYS_ADDR)
            goto fail;
        if (!robo_vmm_map(va, pa, ROBO_VM_READ | ROBO_VM_WRITE | ROBO_VM_KERNEL)) {
            robo_page_free(pa);
            goto fail;
        }
    }

    thread->guard_page = guard;
    thread->kernel_stack = (uint64_t)base;
    thread->kernel_stack_size = ROBO_THREAD_KSTACK_SIZE;
    thread->stack = (uint64_t)base;
    thread->stack_size = 0;
    thread->stack_top = (uint64_t)base + ROBO_THREAD_KSTACK_SIZE;
    thread->owns_stack = 1;
    return 1;

fail:
    while (i > 0) {
        phys_addr_t got = ROBO_INVALID_PHYS_ADDR;
        i--;
        va = base + (virt_addr_t)i * ROBO_PAGE_SIZE;
        if (robo_vmm_unmap(va, &got) && got != ROBO_INVALID_PHYS_ADDR)
            robo_page_free(got);
    }
    return 0;
}

void robo_thread_stack_setup_context(RoboThread *thread)
{
    robo_thread_context_t *f;
    uint8_t *top;

    if (!thread || !thread->stack_top)
        return;
    top = (uint8_t *)(uintptr_t)thread->stack_top;
    f = (robo_thread_context_t *)(top - sizeof(*f));
    bytes_zero(f, sizeof(*f));
    f->rip = (uint64_t)(uintptr_t)robo_thread_trampoline;
    f->cs = ROBO_GDT_KERNEL_CODE;
    f->rflags = 0x202ull;
    f->rsp = (uint64_t)(uintptr_t)f;
    f->ss = ROBO_GDT_KERNEL_DATA;
    thread->context = f;
}

void robo_thread_stack_free(RoboThread *thread)
{
    unsigned i;
    virt_addr_t va;
    phys_addr_t pa;

    if (!thread || !thread->owns_stack || !thread->kernel_stack)
        return;
    for (i = 0; i < ROBO_THREAD_KSTACK_PAGES; i++) {
        va = (virt_addr_t)thread->kernel_stack + (virt_addr_t)i * ROBO_PAGE_SIZE;
        pa = ROBO_INVALID_PHYS_ADDR;
        if (robo_vmm_unmap(va, &pa) && pa != ROBO_INVALID_PHYS_ADDR)
            robo_page_free(pa);
    }
    thread->kernel_stack = 0;
    thread->stack = 0;
    thread->owns_stack = 0;
    thread->context = 0;
}

void robo_thread_bootstrap(void)
{
    RoboThread *t = robo_current_thread();
    if (t && t->entry)
        t->entry(t->argument);
    robo_thread_exit();
    robo_sched_panic("thread_exit returned");
}

void robo_idle_entry(void *arg)
{
    (void)arg;
    for (;;) {
        robo_sched_reap();
        kfb_poll();
        __asm__ volatile ("hlt");
    }
}

RoboThread *robo_thread_adopt_bootstrap(void *stack_base, uint64_t stack_top,
                                        size_t stack_size)
{
    RoboThread *t;

    t = (RoboThread *)kmalloc(sizeof(*t));
    if (!t)
        return 0;
    robo_thread_zero(t);
    robo_thread_set_name(t, "bootstrap");
    t->id = ROBO_THREAD_ID_BOOTSTRAP;
    t->state = ROBO_THREAD_RUNNING;
    t->kernel_stack = (uint64_t)(uintptr_t)stack_base;
    t->kernel_stack_size = (uint64_t)stack_size;
    t->stack = t->kernel_stack;
    t->stack_size = 0;
    t->stack_top = stack_top;
    t->owns_stack = 0;
    t->address_space = robo_kernel_address_space();
    t->created_at = robo_time_now();
    t->last_start_time = t->created_at;
    t->slice_end_ns = t->last_start_time + robo_sched_quantum_ns();
    if (robo_kernel_process() &&
        !robo_process_add_thread(robo_kernel_process(), t)) {
        kfree(t);
        return 0;
    }
    return t;
}

RoboProcess *robo_thread_process(const RoboThread *thread)
{
    return thread ? thread->process : 0;
}

RoboThread *robo_thread_create(const char *name,
                               robo_thread_entry_t entry,
                               void *argument)
{
    RoboProcess *p;

    p = robo_current_process();
    if (!p)
        p = robo_kernel_process();
    return robo_thread_create_in_process(p, name, entry, argument);
}

RoboThread *robo_thread_create_in_process(RoboProcess *process,
                                          const char *name,
                                          robo_thread_entry_t entry,
                                          void *argument)
{
    RoboThread *t;
    uint64_t flags;

    if (g_sched_state == ROBO_SCHED_UNINIT || !entry || !process)
        return 0;

    t = (RoboThread *)kmalloc(sizeof(*t));
    if (!t)
        return 0;
    robo_thread_zero(t);
    robo_thread_set_name(t, name);
    t->entry = entry;
    t->argument = argument;
    t->state = ROBO_THREAD_NEW;
    t->address_space = process->address_space;
    t->created_at = robo_time_now();

    if (!robo_thread_stack_alloc(t)) {
        kfree(t);
        return 0;
    }
    robo_thread_stack_setup_context(t);
    if (!t->context) {
        robo_thread_stack_free(t);
        kfree(t);
        return 0;
    }

    flags = robo_sched_lock();
    t->id = robo_sched_alloc_id();
    if (!robo_thread_transition(t, ROBO_THREAD_READY)) {
        robo_sched_unlock(flags);
        robo_thread_stack_free(t);
        kfree(t);
        return 0;
    }
    if (!robo_process_add_thread(process, t)) {
        robo_sched_unlock(flags);
        robo_thread_stack_free(t);
        kfree(t);
        return 0;
    }
    robo_sched_all_add(t);
    robo_rq_enqueue(&g_ready, t);
    robo_sched_note_create();
    robo_sched_unlock(flags);
    return t;
}

RoboThread *robo_thread_create_user(RoboProcess *process,
                                    const char *name,
                                    uint64_t user_rip, uint64_t user_rsp,
                                    uint64_t user_rdi)
{
    RoboThread *t;
    robo_thread_context_t *f;
    uint64_t flags;

    if (g_sched_state == ROBO_SCHED_UNINIT || !process || !user_rip || !user_rsp)
        return 0;

    t = (RoboThread *)kmalloc(sizeof(*t));
    if (!t)
        return 0;
    robo_thread_zero(t);
    robo_thread_set_name(t, name);
    t->entry = 0;
    t->argument = 0;
    t->state = ROBO_THREAD_NEW;
    t->kind = ROBO_THREAD_USER;
    t->address_space = process->address_space;
    t->created_at = robo_time_now();

    if (!robo_thread_stack_alloc(t)) {
        kfree(t);
        return 0;
    }
    robo_thread_stack_setup_context(t);
    if (!t->context) {
        robo_thread_stack_free(t);
        kfree(t);
        return 0;
    }

    t->stack = user_rsp;
    t->stack_size = ROBO_USER_STACK_SIZE;
    f = t->context;
    f->rip = user_rip;
    f->cs = ROBO_GDT_USER_CODE_RPL;
    f->ss = ROBO_GDT_USER_DATA_RPL;
    f->rsp = user_rsp;
    f->rflags = 0x202ull;
    f->rdi = user_rdi;
    f->rsi = 0;
    f->rdx = 0;
    f->rcx = 0;
    f->rax = 0;
    f->vector = 0;
    f->error = 0;

    flags = robo_sched_lock();
    t->id = robo_sched_alloc_id();
    if (!robo_thread_transition(t, ROBO_THREAD_READY)) {
        robo_sched_unlock(flags);
        robo_thread_stack_free(t);
        kfree(t);
        return 0;
    }
    if (!robo_process_add_thread(process, t)) {
        robo_sched_unlock(flags);
        robo_thread_stack_free(t);
        kfree(t);
        return 0;
    }
    robo_sched_all_add(t);
    robo_rq_enqueue(&g_ready, t);
    robo_sched_note_create();
    robo_sched_unlock(flags);
    return t;
}

void robo_thread_kill_current(void)
{
    RoboThread *t;
    uint64_t flags;

    flags = robo_sched_lock();
    t = g_current;
    if (!t || t == g_idle) {
        robo_sched_unlock(flags);
        return;
    }
    if (t->sleep_timer)
        robo_timer_cancel(t->sleep_timer);
    t->sleep_timer = 0;
    t->wake_deadline = 0;
    if (!robo_thread_transition(t, ROBO_THREAD_TERMINATED))
        t->state = ROBO_THREAD_TERMINATED;
    if (t->process) {
        if (t->process->state != ROBO_PROCESS_TERMINATED &&
            t->process->exit_status == 0)
            t->process->exit_status = ROBO_PROCESS_EXIT_FAULT;
        robo_process_remove_thread(t->process, t);
    }
    robo_sched_note_term();
    robo_sched_unlock(flags);
}

void robo_thread_yield(void)
{
    if (g_sched_state != ROBO_SCHED_RUNNING)
        return;
    if (g_preempt_count)
        return;
    __asm__ volatile ("int $48" ::: "memory");
}

void robo_thread_exit(void)
{
    RoboThread *t;
    uint64_t flags;

    flags = robo_sched_lock();
    t = g_current;
    if (!t) {
        robo_sched_unlock(flags);
        robo_sched_panic("exit with no current thread");
    }
    if (t == g_idle) {
        robo_sched_unlock(flags);
        robo_sched_panic("idle thread attempted to exit");
    }
    if (t->sleep_timer)
        robo_timer_cancel(t->sleep_timer);
    t->sleep_timer = 0;
    t->wake_deadline = 0;
    if (!robo_thread_transition(t, ROBO_THREAD_TERMINATED))
        t->state = ROBO_THREAD_TERMINATED;
    if (t->process)
        robo_process_remove_thread(t->process, t);
    robo_sched_note_term();
    robo_sched_unlock(flags);
    __asm__ volatile ("int $48" ::: "memory");
    robo_sched_panic("terminated thread resumed");
}

void robo_thread_block(void)
{
    RoboThread *t;
    uint64_t flags;

    if (g_sched_state != ROBO_SCHED_RUNNING)
        return;
    flags = robo_sched_lock();
    t = g_current;
    if (!t || t == g_idle) {
        robo_sched_unlock(flags);
        return;
    }
    if (!robo_thread_transition(t, ROBO_THREAD_BLOCKED)) {
        robo_sched_unlock(flags);
        return;
    }
    robo_sched_unlock(flags);
    __asm__ volatile ("int $48" ::: "memory");
}

int robo_thread_wake(RoboThread *thread)
{
    uint64_t flags;
    if (!thread)
        return 0;
    flags = robo_sched_lock();
    if (thread->state != ROBO_THREAD_BLOCKED) {
        robo_sched_unlock(flags);
        return 0;
    }
    if (!robo_thread_transition(thread, ROBO_THREAD_READY)) {
        robo_sched_unlock(flags);
        return 0;
    }
    if (!robo_rq_contains(&g_ready, thread))
        robo_rq_enqueue(&g_ready, thread);
    robo_sched_unlock(flags);
    return 1;
}

static void sleep_wake(void *ctx)
{
    RoboThread *t = (RoboThread *)ctx;
    if (!t)
        return;
    t->sleep_timer = 0;
    t->wake_deadline = 0;
    if (t->state != ROBO_THREAD_SLEEPING)
        return;
    if (!robo_thread_transition(t, ROBO_THREAD_READY))
        return;
    if (!robo_rq_contains(&g_ready, t))
        robo_rq_enqueue(&g_ready, t);
}

int robo_thread_sleep_until(uint64_t deadline_ns)
{
    RoboThread *t;
    uint64_t flags;
    int handle;

    if (g_sched_state != ROBO_SCHED_RUNNING)
        return 0;
    t = g_current;
    if (!t || t == g_idle)
        return 0;

    flags = robo_sched_lock();
    if (!robo_thread_transition(t, ROBO_THREAD_SLEEPING)) {
        robo_sched_unlock(flags);
        return 0;
    }
    t->wake_deadline = deadline_ns;
    handle = robo_timer_add(deadline_ns, 0, sleep_wake, t);
    t->sleep_timer = handle;
    robo_sched_unlock(flags);
    if (!handle) {
        flags = robo_sched_lock();
        if (t->state == ROBO_THREAD_SLEEPING) {
            robo_thread_transition(t, ROBO_THREAD_READY);
            if (t == g_current)
                robo_thread_transition(t, ROBO_THREAD_RUNNING);
        }
        t->wake_deadline = 0;
        robo_sched_unlock(flags);
        return 0;
    }
    __asm__ volatile ("int $48" ::: "memory");
    return 1;
}

int robo_scheduler_sleep(uint64_t milliseconds)
{
    uint64_t deadline;
    if (g_sched_state != ROBO_SCHED_RUNNING || milliseconds == 0)
        return 0;
    deadline = robo_time_deadline(robo_time_now(), milliseconds * ROBO_NS_PER_MS);
    return robo_thread_sleep_until(deadline);
}

RoboThread *robo_current_thread(void)
{
    return g_current;
}

RoboThread *robo_thread_all(void)
{
    return g_all;
}

void robo_thread_dump(const RoboThread *thread)
{
    if (!thread) {
        kprintf("Thread  (null)\n");
        return;
    }
    kprintf("Thread\n");
    kprintf("------\n");
    kprintf("ID              : %llu\n", (unsigned long long)thread->id);
    kprintf("Name            : %s\n", thread->name);
    kprintf("State           : %s\n", robo_thread_state_name(thread->state));
    kprintf("RIP             : 0x%llx\n", (unsigned long long)robo_thread_ip(thread));
    kprintf("RSP             : 0x%llx\n", (unsigned long long)robo_thread_sp(thread));
    kprintf("Kernel Stack    : 0x%llx\n", (unsigned long long)thread->kernel_stack);
    kprintf("Address Space   : 0x%llx\n",
            (unsigned long long)(thread->address_space ? thread->address_space->cr3 : 0));
    kprintf("Process         : %llu\n",
            (unsigned long long)(thread->process ? thread->process->id : 0));
}

void robo_thread_report(void)
{
    RoboThread *t;

    kprintf("Threads\n");
    kprintf("-------\n");
    kprintf("ID    Name          State\n");
    kprintf("--------------------------\n");
    for (t = g_all; t; t = t->all_next) {
        kprintf("%llu    %s  %s\n",
                (unsigned long long)t->id,
                t->name,
                robo_thread_state_name(t->state));
    }
}

#ifndef THREAD_HOST_TEST
void robo_thread_fb_report(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    RoboThread *t = robo_current_thread();
    kfb_puts("Thread  ", mute);
    if (!t) {
        kfb_puts("none\n", mute);
        return;
    }
    kfb_putu((unsigned long long)t->id, white);
    kfb_puts(" ", mute);
    kfb_puts(t->name, white);
    kfb_puts("  ", mute);
    kfb_puts(robo_thread_state_name(t->state), white);
    kfb_puts("\n", mute);
}
#endif
