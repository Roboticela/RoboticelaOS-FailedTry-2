#ifndef ROBO_SCHEDULER_TASK_H
#define ROBO_SCHEDULER_TASK_H

#include <stddef.h>
#include <stdint.h>

struct robo_iframe;

/*
 * Task identifiers (TIDs)
 * -----------------------
 * IDs are scheduler-local, not a POSIX PID namespace.
 *
 *   0  bootstrap / kmain context (adopted, never reused while alive)
 *   1  idle task (deterministic, never reused while alive)
 *   2+ allocated monotonically from g_next_id
 *
 * An ID is unique among tasks that still exist (including terminated
 * tasks waiting to be reaped). After a task is reaped its ID may be
 * reused. Allocation walks the live list if the counter wraps.
 */

#define ROBO_TID_BOOTSTRAP 0ull
#define ROBO_TID_IDLE      1ull

#define ROBO_TASK_NAME_MAX 32
#define ROBO_KSTACK_SIZE   (16u * 1024u)
#define ROBO_KSTACK_PAGES  4u
#define ROBO_KSTACK_GUARD_PAGES 1u

/*
 * Kernel stacks live in a dedicated higher-half window, above the heap:
 *   HEAP_END  = 0xFFFF800010100000
 *   KSTACK    = 0xFFFF800020000000  (this window)
 *
 * Layout per task (addresses grow up the page):
 *   [ unmapped guard page ]
 *   [ 16 KiB RW NX kernel stack ]
 * Stack pointer starts at the high end. Overflow hits the guard page
 * and is reported by the existing page-fault diagnostic.
 *
 * Ownership
 * ---------
 *   TCB:        kmalloc in robo_task_create / adopt; kfree by reaper
 *   Stack pages: PMM alloc + VMM map in create; unmap+free by reaper
 *   Bootstrap:  owns the BSS kstack from start.S; never freed
 *
 * Never free the stack of the task that is currently executing.
 * Terminated tasks are queued and reaped only after the CPU has
 * switched to another stack.
 */

typedef enum {
    ROBO_TASK_NEW = 0,
    ROBO_TASK_READY,
    ROBO_TASK_RUNNING,
    ROBO_TASK_BLOCKED,
    ROBO_TASK_SLEEPING,
    ROBO_TASK_TERMINATED
} robo_task_state_t;

typedef void (*robo_task_entry_t)(void *arg);

typedef struct robo_task {
    uint64_t id;
    char name[ROBO_TASK_NAME_MAX];
    robo_task_state_t state;

    /* Saved interrupt frame sitting on this task's kernel stack. */
    struct robo_iframe *iframe;

    void *kernel_stack;
    size_t kernel_stack_size;
    uint64_t stack_top;
    uint64_t guard_page;
    int owns_stack;

    robo_task_entry_t entry;
    void *arg;

    uint64_t runtime_ns;
    uint64_t last_start_ns;
    uint64_t slice_end_ns;
    int sleep_timer;

    struct robo_task *next;
    struct robo_task *prev;
    struct robo_task *all_next;
    struct robo_task *reap_next;
} robo_task_t;

robo_task_t *robo_task_create(const char *name, robo_task_entry_t entry, void *arg);
void robo_task_exit(void);
void robo_task_yield(void);

/* Foundation for later wait queues. Not a full blocking API. */
void robo_task_block(void);
int robo_task_unblock(robo_task_t *task);

const char *robo_task_state_name(robo_task_state_t state);

#endif
