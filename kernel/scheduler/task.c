#include "scheduler/sched_internal.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/exception.h"
#include "arch/x86_64/scheduler/context_switch.h"
#include "mm/pmm.h"
#include "mm/vmm/vmm.h"
#include "mm/heap/heap.h"
#include "mm/page.h"
#include "time/time.h"
#include "kio.h"

#define ROBO_KSTACK_SLOT_PAGES (ROBO_KSTACK_GUARD_PAGES + ROBO_KSTACK_PAGES)

static uint64_t g_stack_slot;

static void bytes_zero(void *dst, size_t n)
{
    uint8_t *p = (uint8_t *)dst;
    while (n--)
        *p++ = 0;
}

void robo_task_zero(robo_task_t *task)
{
    if (task)
        bytes_zero(task, sizeof(*task));
}

void robo_task_set_name(robo_task_t *task, const char *name)
{
    unsigned i;
    if (!task)
        return;
    if (!name)
        name = "task";
    for (i = 0; i + 1u < ROBO_TASK_NAME_MAX && name[i]; i++)
        task->name[i] = name[i];
    task->name[i] = 0;
}

const char *robo_task_state_name(robo_task_state_t state)
{
    switch (state) {
    case ROBO_TASK_NEW:        return "NEW";
    case ROBO_TASK_READY:      return "READY";
    case ROBO_TASK_RUNNING:    return "RUNNING";
    case ROBO_TASK_BLOCKED:    return "BLOCKED";
    case ROBO_TASK_SLEEPING:   return "SLEEPING";
    case ROBO_TASK_TERMINATED: return "TERMINATED";
    default:                   return "?";
    }
}

int robo_task_stack_alloc(robo_task_t *task)
{
    unsigned i;
    virt_addr_t guard, base, va;
    phys_addr_t pa;

    if (!task)
        return 0;

    guard = ROBO_VMM_KSTACK_BASE + g_stack_slot * ROBO_KSTACK_SLOT_PAGES * ROBO_PAGE_SIZE;
    g_stack_slot++;
    base = guard + ROBO_PAGE_SIZE;

    for (i = 0; i < ROBO_KSTACK_PAGES; i++) {
        va = base + (virt_addr_t)i * ROBO_PAGE_SIZE;
        pa = robo_page_alloc();
        if (pa == ROBO_INVALID_PHYS_ADDR)
            goto fail;
        if (!robo_vmm_map(va, pa, ROBO_VM_READ | ROBO_VM_WRITE | ROBO_VM_KERNEL)) {
            robo_page_free(pa);
            goto fail;
        }
    }

    task->guard_page = guard;
    task->kernel_stack = (void *)(uintptr_t)base;
    task->kernel_stack_size = ROBO_KSTACK_SIZE;
    task->stack_top = (uint64_t)base + ROBO_KSTACK_SIZE;
    task->owns_stack = 1;
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

void robo_task_stack_setup_iframe(robo_task_t *task)
{
    struct robo_iframe *f;
    uint8_t *top;

    if (!task || !task->stack_top)
        return;
    top = (uint8_t *)(uintptr_t)task->stack_top;
    f = (struct robo_iframe *)(top - sizeof(*f));
    bytes_zero(f, sizeof(*f));
    f->rip = (uint64_t)(uintptr_t)robo_task_trampoline;
    f->cs = ROBO_GDT_KERNEL_CODE;
    f->rflags = 0x202ull;
    f->rsp = (uint64_t)(uintptr_t)f;
    f->ss = ROBO_GDT_KERNEL_DATA;
    task->iframe = f;
}

void robo_task_stack_free(robo_task_t *task)
{
    unsigned i;
    virt_addr_t va;
    phys_addr_t pa;

    if (!task || !task->owns_stack || !task->kernel_stack)
        return;
    for (i = 0; i < ROBO_KSTACK_PAGES; i++) {
        va = (virt_addr_t)(uintptr_t)task->kernel_stack + (virt_addr_t)i * ROBO_PAGE_SIZE;
        pa = ROBO_INVALID_PHYS_ADDR;
        if (robo_vmm_unmap(va, &pa) && pa != ROBO_INVALID_PHYS_ADDR)
            robo_page_free(pa);
    }
    task->kernel_stack = 0;
    task->owns_stack = 0;
    task->iframe = 0;
}

void robo_task_bootstrap(void)
{
    robo_task_t *t = robo_current_task();
    if (t && t->entry)
        t->entry(t->arg);
    robo_task_exit();
    robo_sched_panic("task_exit returned");
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

robo_task_t *robo_task_create(const char *name, robo_task_entry_t entry, void *arg)
{
    robo_task_t *t;
    uint64_t flags;

    if (g_sched_state == ROBO_SCHED_UNINIT || !entry)
        return 0;

    t = (robo_task_t *)kmalloc(sizeof(*t));
    if (!t)
        return 0;
    robo_task_zero(t);
    robo_task_set_name(t, name);
    t->entry = entry;
    t->arg = arg;
    t->state = ROBO_TASK_NEW;

    if (!robo_task_stack_alloc(t)) {
        kfree(t);
        return 0;
    }
    robo_task_stack_setup_iframe(t);
    if (!t->iframe) {
        robo_task_stack_free(t);
        kfree(t);
        return 0;
    }

    flags = robo_sched_lock();
    t->id = robo_sched_alloc_id();
    t->state = ROBO_TASK_READY;
    robo_sched_all_add(t);
    robo_rq_enqueue(&g_ready, t);
    robo_sched_note_create();
    robo_sched_unlock(flags);
    return t;
}

void robo_task_yield(void)
{
    if (g_sched_state != ROBO_SCHED_RUNNING)
        return;
    if (g_preempt_count)
        return;
    __asm__ volatile ("int $48" ::: "memory");
}

void robo_task_exit(void)
{
    robo_task_t *t;
    uint64_t flags;

    flags = robo_sched_lock();
    t = g_current;
    if (!t) {
        robo_sched_unlock(flags);
        robo_sched_panic("exit with no current task");
    }
    if (t == g_idle) {
        robo_sched_unlock(flags);
        robo_sched_panic("idle task attempted to exit");
    }
    if (t->sleep_timer)
        robo_timer_cancel(t->sleep_timer);
    t->sleep_timer = 0;
    t->state = ROBO_TASK_TERMINATED;
    robo_sched_note_term();
    robo_sched_unlock(flags);
    __asm__ volatile ("int $48" ::: "memory");
    robo_sched_panic("terminated task resumed");
}

void robo_task_block(void)
{
    robo_task_t *t;
    uint64_t flags;

    if (g_sched_state != ROBO_SCHED_RUNNING)
        return;
    flags = robo_sched_lock();
    t = g_current;
    if (!t || t == g_idle) {
        robo_sched_unlock(flags);
        return;
    }
    t->state = ROBO_TASK_BLOCKED;
    robo_sched_unlock(flags);
    __asm__ volatile ("int $48" ::: "memory");
}

int robo_task_unblock(robo_task_t *task)
{
    uint64_t flags;
    if (!task)
        return 0;
    flags = robo_sched_lock();
    if (task->state != ROBO_TASK_BLOCKED) {
        robo_sched_unlock(flags);
        return 0;
    }
    task->state = ROBO_TASK_READY;
    if (!robo_rq_contains(&g_ready, task))
        robo_rq_enqueue(&g_ready, task);
    robo_sched_unlock(flags);
    return 1;
}
