#include "process/process.h"
#include "process/process_table.h"
#include "mm/vmm/paging.h"

#ifdef PROCESS_HOST_TEST
#include <stdlib.h>
#include <stdio.h>
#define kmalloc malloc
#define kfree free
#define kprintf printf
static uint64_t robo_sched_lock(void) { return 0; }
static void robo_sched_unlock(uint64_t flags) { (void)flags; }
static uint64_t robo_time_now(void) { return 0; }
RoboAddressSpace *robo_kernel_address_space(void)
{
    static RoboAddressSpace k;
    if (!k.cr3)
        k.cr3 = 0x1000;
    return &k;
}
static int host_aspace_clone(RoboAddressSpace *as)
{
    static phys_addr_t next = 0x2000;
    if (!as)
        return 0;
    as->cr3 = next;
    next += 0x1000;
    as->cloned = 1;
    as->table_pages = 1;
    as->maps = 0;
    return 1;
}
static void host_aspace_destroy(RoboAddressSpace *as)
{
    if (!as)
        return;
    as->cr3 = 0;
    as->cloned = 0;
}
RoboThread *robo_current_thread(void);
static RoboThread *g_host_current;
RoboThread *robo_current_thread(void) { return g_host_current; }
void robo_host_set_current_thread(RoboThread *t) { g_host_current = t; }
RoboProcess *robo_thread_process(const RoboThread *thread)
{
    return thread ? thread->process : 0;
}
#else
#include "scheduler/sched_internal.h"
#include "mm/heap/heap.h"
#include "mm/vmm/vmm.h"
#include "time/time.h"
#include "kio.h"
#endif

static RoboProcess *g_kernel;
static uint64_t g_created;
static uint64_t g_destroyed;
static uint64_t g_live;
static uint64_t g_live_max;
static uint64_t g_aspace_created;
static uint64_t g_aspace_destroyed;
static uint64_t g_files;
static uint64_t g_ipc;
static uint64_t g_env;
static uint64_t g_creds;
static int g_process_ready;

static void proc_zero(void *dst, unsigned n)
{
    unsigned char *p = (unsigned char *)dst;
    while (n--)
        *p++ = 0;
}

static void proc_set_name(RoboProcess *p, const char *name)
{
    unsigned i;
    if (!name)
        name = "process";
    for (i = 0; i + 1u < ROBO_PROCESS_NAME_MAX && name[i]; i++)
        p->name[i] = name[i];
    p->name[i] = 0;
}

static int proc_set_state(RoboProcess *p, robo_process_state_t next)
{
    if (!p)
        return 0;
    if (!robo_process_transition_allowed(p->state, next))
        return 0;
    p->state = next;
    return 1;
}

static void link_child(RoboProcess *parent, RoboProcess *child)
{
    if (!parent || !child)
        return;
    child->parent = parent;
    child->sibling_next = parent->first_child;
    parent->first_child = child;
}

static void unlink_child(RoboProcess *parent, RoboProcess *child)
{
    RoboProcess **slot;
    if (!parent || !child)
        return;
    for (slot = &parent->first_child; *slot; slot = &(*slot)->sibling_next) {
        if (*slot == child) {
            *slot = child->sibling_next;
            child->sibling_next = 0;
            child->parent = 0;
            return;
        }
    }
}

static void free_resources(RoboProcess *p)
{
    if (!p)
        return;
    if (p->fd_table) {
        robo_fd_table_destroy(p->fd_table);
        p->fd_table = 0;
        if (g_files)
            g_files--;
    }
    if (p->ipc) {
        robo_ipc_destroy(p->ipc);
        p->ipc = 0;
        if (g_ipc)
            g_ipc--;
    }
    if (p->environment) {
        robo_env_destroy(p->environment);
        p->environment = 0;
        if (g_env)
            g_env--;
    }
    if (p->credentials) {
        robo_creds_destroy(p->credentials);
        p->credentials = 0;
        if (g_creds)
            g_creds--;
    }
}

static void free_aspace(RoboProcess *p)
{
    if (!p || !p->owns_aspace || !p->address_space)
        return;
#ifdef PROCESS_HOST_TEST
    host_aspace_destroy(p->address_space);
#else
    robo_vmm_aspace_destroy(p->address_space);
#endif
    kfree(p->address_space);
    p->address_space = 0;
    p->owns_aspace = 0;
    g_aspace_destroyed++;
}

static void process_free_partial(RoboProcess *p)
{
    if (!p)
        return;
    free_resources(p);
    free_aspace(p);
    kfree(p);
}

static RoboProcess *process_create_internal(const char *name, int clone)
{
    RoboProcess *p;
    RoboProcess *parent;

    p = (RoboProcess *)kmalloc(sizeof(*p));
    if (!p)
        return 0;
    proc_zero(p, (unsigned)sizeof(*p));
    proc_set_name(p, name);
    p->state = ROBO_PROCESS_NEW;
    p->id = robo_ptable_alloc_id();
    if (p->id == 0)
        goto fail_process;

    if (clone) {
        p->address_space = (RoboAddressSpace *)kmalloc(sizeof(*p->address_space));
        if (!p->address_space)
            goto fail_process;
        proc_zero(p->address_space, (unsigned)sizeof(*p->address_space));
#ifdef PROCESS_HOST_TEST
        if (!host_aspace_clone(p->address_space))
            goto fail_address_space;
#else
        if (!robo_vmm_aspace_clone(p->address_space))
            goto fail_address_space;
#endif
        p->owns_aspace = 1;
        p->mmap_next = ROBO_USER_MMAP_BASE;
        g_aspace_created++;
    } else {
        p->address_space = robo_kernel_address_space();
        p->owns_aspace = 0;
    }

    p->fd_table = robo_fd_table_create();
    if (!p->fd_table)
        goto fail_address_space;
    g_files++;

    p->ipc = robo_ipc_create();
    if (!p->ipc)
        goto fail_files;
    g_ipc++;

    p->environment = robo_env_create();
    if (!p->environment)
        goto fail_ipc;
    g_env++;

    p->credentials = robo_creds_create();
    if (!p->credentials)
        goto fail_environment;
    g_creds++;

    p->created_at = robo_time_now();

    if (!robo_ptable_insert(p))
        goto fail_credentials;

    parent = robo_current_process();
    if (parent && parent != p)
        link_child(parent, p);

    g_created++;
    g_live++;
    if (g_live > g_live_max)
        g_live_max = g_live;
    return p;

fail_credentials:
    robo_creds_destroy(p->credentials);
    p->credentials = 0;
    if (g_creds)
        g_creds--;
fail_environment:
    robo_env_destroy(p->environment);
    p->environment = 0;
    if (g_env)
        g_env--;
fail_ipc:
    robo_ipc_destroy(p->ipc);
    p->ipc = 0;
    if (g_ipc)
        g_ipc--;
fail_files:
    robo_fd_table_destroy(p->fd_table);
    p->fd_table = 0;
    if (g_files)
        g_files--;
fail_address_space:
    if (p->address_space && p->address_space != robo_kernel_address_space()) {
        if (p->owns_aspace) {
#ifdef PROCESS_HOST_TEST
            host_aspace_destroy(p->address_space);
#else
            robo_vmm_aspace_destroy(p->address_space);
#endif
            if (g_aspace_created)
                g_aspace_created--;
        }
        kfree(p->address_space);
        p->address_space = 0;
        p->owns_aspace = 0;
    }
fail_process:
    kfree(p);
    return 0;
}

int robo_process_subsystem_init(void)
{
    if (g_process_ready)
        return 1;
    robo_ptable_init();
    g_kernel = process_create_internal("kernel", 0);
    if (!g_kernel)
        return 0;
    if (g_kernel->id != ROBO_PROCESS_ID_KERNEL)
        return 0;
    if (!proc_set_state(g_kernel, ROBO_PROCESS_RUNNING))
        g_kernel->state = ROBO_PROCESS_RUNNING;
    g_process_ready = 1;
#ifndef PROCESS_HOST_TEST
    kprintf("Process Subsystem\n");
    kprintf("-----------------\n");
    kprintf("[ OK ] Kernel process  PID %llu\n",
            (unsigned long long)g_kernel->id);
    kprintf("[ OK ] Kernel address space  CR3 0x%llx\n",
            (unsigned long long)(g_kernel->address_space ?
                                 g_kernel->address_space->cr3 : 0));
    kprintf("[ OK ] Process table\n");
#endif
    return 1;
}

RoboProcess *robo_kernel_process(void)
{
    return g_kernel;
}

RoboProcess *robo_process_create(void)
{
    return robo_process_create_named("process");
}

RoboProcess *robo_process_create_named(const char *name)
{
    RoboProcess *p;

    if (!g_process_ready)
        return 0;
    p = process_create_internal(name, 1);
    if (!p)
        return 0;
    if (!proc_set_state(p, ROBO_PROCESS_RUNNING))
        p->state = ROBO_PROCESS_RUNNING;
    return p;
}

int robo_process_add_thread(RoboProcess *process, RoboThread *thread)
{
    uint64_t flags;

    if (!process || !thread)
        return 0;
    if (process->state == ROBO_PROCESS_TERMINATED)
        return 0;
    flags = robo_sched_lock();
    if (thread->process && thread->process != process) {
        robo_sched_unlock(flags);
        return 0;
    }
    if (thread->process == process) {
        robo_sched_unlock(flags);
        return 1;
    }
    thread->process = process;
    thread->address_space = process->address_space;
    thread->proc_prev = 0;
    thread->proc_next = process->threads;
    if (process->threads)
        process->threads->proc_prev = thread;
    process->threads = thread;
    process->thread_count++;
    if (process->state == ROBO_PROCESS_NEW)
        proc_set_state(process, ROBO_PROCESS_RUNNING);
    robo_sched_unlock(flags);
    return 1;
}

int robo_process_remove_thread(RoboProcess *process, RoboThread *thread)
{
    uint64_t flags;

    if (!process || !thread)
        return 0;
    flags = robo_sched_lock();
    if (thread->process != process) {
        robo_sched_unlock(flags);
        return 0;
    }
    if (thread->proc_prev)
        thread->proc_prev->proc_next = thread->proc_next;
    else if (process->threads == thread)
        process->threads = thread->proc_next;
    if (thread->proc_next)
        thread->proc_next->proc_prev = thread->proc_prev;
    thread->proc_next = 0;
    thread->proc_prev = 0;
    if (process->thread_count)
        process->thread_count--;
    if (process != g_kernel &&
        process->thread_count == 0 &&
        process->state != ROBO_PROCESS_TERMINATED) {
        if (!proc_set_state(process, ROBO_PROCESS_TERMINATED))
            process->state = ROBO_PROCESS_TERMINATED;
        process->exit_time = robo_time_now();
    }
    robo_sched_unlock(flags);
    return 1;
}

int robo_process_destroy(RoboProcess *process)
{
    RoboProcess *child, *next;
    RoboProcess *parent;

    if (!process || process == g_kernel)
        return 0;
    if (process->thread_count != 0)
        return 0;

    parent = process->parent;
    unlink_child(parent, process);

    child = process->first_child;
    process->first_child = 0;
    while (child) {
        next = child->sibling_next;
        child->sibling_next = 0;
        child->parent = 0;
        if (parent)
            link_child(parent, child);
        else if (g_kernel && g_kernel != process)
            link_child(g_kernel, child);
        child = next;
    }

    robo_ptable_remove(process);
    free_resources(process);
    free_aspace(process);
    kfree(process);
    g_destroyed++;
    if (g_live)
        g_live--;
    return 1;
}

void robo_process_exit(int status)
{
    RoboProcess *p;
#ifndef PROCESS_HOST_TEST
    RoboThread *t, *n, *self;
    uint64_t flags;
#endif

    p = robo_current_process();
    if (!p || p == g_kernel)
        return;
    p->exit_status = status;
    if (p->state != ROBO_PROCESS_TERMINATED) {
        if (!proc_set_state(p, ROBO_PROCESS_TERMINATED))
            p->state = ROBO_PROCESS_TERMINATED;
    }
    p->exit_time = robo_time_now();

#ifndef PROCESS_HOST_TEST
    flags = robo_sched_lock();
    self = g_current;
    t = p->threads;
    while (t) {
        n = t->proc_next;
        if (t != self) {
            if (t->sleep_timer)
                robo_timer_cancel(t->sleep_timer);
            t->sleep_timer = 0;
            t->wake_deadline = 0;
            if (t->state == ROBO_THREAD_READY)
                robo_rq_remove(&g_ready, t);
            t->state = ROBO_THREAD_TERMINATED;
            robo_process_remove_thread(p, t);
            robo_sched_note_term();
            robo_sched_reap_later(t);
        }
        t = n;
    }
    robo_sched_unlock(flags);
    robo_thread_exit();
#endif
}

RoboProcess *robo_current_process(void)
{
    RoboThread *t = robo_current_thread();
    if (t && t->process)
        return t->process;
    return g_kernel;
}

robo_process_id_t robo_process_id(const RoboProcess *process)
{
    return process ? process->id : 0;
}

RoboProcess *robo_process_lookup(robo_process_id_t pid)
{
    return robo_ptable_lookup(pid);
}

int robo_process_fd_alloc(RoboProcess *process, RoboFile *file)
{
    return (int)robo_fd_alloc(process, file, 0);
}

int robo_process_fd_close(RoboProcess *process, int fd)
{
    return robo_fd_close(process, (robo_fd_t)fd) == 0;
}

RoboFile *robo_process_fd_get(RoboProcess *process, int fd)
{
    return robo_fd_get(process, (robo_fd_t)fd);
}

int robo_process_fd_in_use(RoboProcess *process, int fd)
{
    return robo_fd_in_use(process, (robo_fd_t)fd);
}

uint64_t robo_process_stat_created(void) { return g_created; }
uint64_t robo_process_stat_destroyed(void) { return g_destroyed; }
uint64_t robo_process_stat_live(void) { return g_live; }
uint64_t robo_process_stat_live_max(void) { return g_live_max; }
uint64_t robo_process_stat_aspace_created(void) { return g_aspace_created; }
uint64_t robo_process_stat_aspace_destroyed(void) { return g_aspace_destroyed; }
uint64_t robo_process_stat_files(void) { return g_files; }
uint64_t robo_process_stat_ipc(void) { return g_ipc; }
uint64_t robo_process_stat_env(void) { return g_env; }
uint64_t robo_process_stat_creds(void) { return g_creds; }
