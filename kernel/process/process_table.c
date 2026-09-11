#include "process/process_table.h"

#ifdef PROCESS_HOST_TEST
static uint64_t robo_sched_lock(void) { return 0; }
static void robo_sched_unlock(uint64_t flags) { (void)flags; }
#else
#include "scheduler/sched_internal.h"
#endif

/*
 * Process table lock: robo_sched_lock (CLI on the bootstrap CPU).
 * Protects g_head, g_count, and g_next_id. Process thread lists use
 * the same lock in process.c so SMP later can split them cleanly.
 */

static RoboProcess *g_head;
static uint64_t g_count;
static robo_process_id_t g_next_id = 2;

void robo_ptable_init(void)
{
    g_head = 0;
    g_count = 0;
    g_next_id = 2;
}

robo_process_id_t robo_ptable_alloc_id(void)
{
    uint64_t flags;
    robo_process_id_t id;
    RoboProcess *p;
    int collision;

    flags = robo_sched_lock();
    for (;;) {
        id = g_next_id++;
        if (id == 0 || id == ROBO_PROCESS_ID_INIT) {
            g_next_id = 2;
            continue;
        }
        collision = 0;
        for (p = g_head; p; p = p->next) {
            if (p->id == id) {
                collision = 1;
                break;
            }
        }
        if (!collision) {
            robo_sched_unlock(flags);
            return id;
        }
        if (g_next_id == 0 || g_next_id == ROBO_PROCESS_ID_INIT)
            g_next_id = 2;
    }
}

int robo_ptable_insert(RoboProcess *process)
{
    uint64_t flags;
    RoboProcess *p;

    if (!process)
        return 0;

    flags = robo_sched_lock();
    for (p = g_head; p; p = p->next) {
        if (p == process || p->id == process->id) {
            robo_sched_unlock(flags);
            return 0;
        }
    }
    process->next = g_head;
    g_head = process;
    g_count++;
    robo_sched_unlock(flags);
    return 1;
}

void robo_ptable_remove(RoboProcess *process)
{
    uint64_t flags;
    RoboProcess **slot;

    if (!process)
        return;
    flags = robo_sched_lock();
    for (slot = &g_head; *slot; slot = &(*slot)->next) {
        if (*slot == process) {
            *slot = process->next;
            process->next = 0;
            if (g_count)
                g_count--;
            break;
        }
    }
    robo_sched_unlock(flags);
}

RoboProcess *robo_ptable_lookup(robo_process_id_t pid)
{
    uint64_t flags;
    RoboProcess *p;

    flags = robo_sched_lock();
    for (p = g_head; p; p = p->next) {
        if (p->id == pid) {
            robo_sched_unlock(flags);
            return p;
        }
    }
    robo_sched_unlock(flags);
    return 0;
}

uint64_t robo_ptable_count(void)
{
    return g_count;
}

RoboProcess *robo_ptable_head(void)
{
    return g_head;
}
