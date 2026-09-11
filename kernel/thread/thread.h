#ifndef ROBO_THREAD_H
#define ROBO_THREAD_H

#include <stddef.h>
#include <stdint.h>
#include "thread/thread_state.h"
#include "thread/thread_context.h"
#include "mm/page.h"
#include "mm/vmm/aspace.h"

/*
 * RoboThread — the authoritative schedulable execution object (Phase 9).
 *
 * The Phase 8 scheduler operates on RoboThread. There is no second
 * independent task object. `robo_task_t` is a compatibility alias.
 *
 * Identity
 * --------
 *   0  bootstrap (kmain), never reused while alive
 *   1  idle, never reused while alive
 *   2+ monotonic; unique among live threads including unreaped zombies.
 *      After reap, an ID may be reused.
 *
 * Stacks
 * ------
 *   kernel_stack / kernel_stack_size
 *       Dedicated kernel stack (PMM + VMM, guard page below, NX if available).
 *   stack / stack_size
 *       Current execution stack. For kernel threads this matches the kernel
 *       stack. Future user threads will set `stack` to the user stack.
 *
 *   The thread object owns its kernel stack. Never free that stack while
 *   the CPU is still executing on it; reap after the switch.
 *
 * Address space
 * -------------
 *   Each thread caches the owning process address space. Threads of
 *   one process share that object. CR3 is reloaded only when the next
 *   thread belongs to a different process.
 *
 * Instruction pointer
 * -------------------
 *   robo_thread_ip() reads context->rip. Do not store a second RIP.
 */

typedef uint64_t robo_thread_id_t;
typedef void (*robo_thread_entry_t)(void *argument);

#define ROBO_THREAD_ID_BOOTSTRAP 0ull
#define ROBO_THREAD_ID_IDLE      1ull
#define ROBO_THREAD_NAME_MAX     32
#define ROBO_THREAD_KSTACK_SIZE  (16u * 1024u)
#define ROBO_THREAD_KSTACK_PAGES 4u
#define ROBO_THREAD_KSTACK_GUARD_PAGES 1u

struct RoboProcess;

typedef struct RoboThread {
    robo_thread_id_t id;
    char name[ROBO_THREAD_NAME_MAX];
    robo_thread_state_t state;

    uint64_t stack;
    uint64_t stack_size;
    uint64_t kernel_stack;
    uint64_t kernel_stack_size;
    uint64_t stack_top;
    uint64_t guard_page;
    int owns_stack;

    struct RoboProcess *process;
    RoboAddressSpace *address_space;
    robo_thread_context_t *context;

    robo_thread_entry_t entry;
    void *argument;

    uint64_t created_at;
    uint64_t last_start_time;
    uint64_t runtime;
    uint64_t slice_end_ns;
    uint64_t wake_deadline;
    int sleep_timer;
    int kind; /* ROBO_THREAD_KERNEL or ROBO_THREAD_USER */

    struct RoboThread *next;
    struct RoboThread *prev;
    struct RoboThread *all_next;
    struct RoboThread *reap_next;
    struct RoboThread *proc_next;
    struct RoboThread *proc_prev;
} RoboThread;

RoboAddressSpace *robo_kernel_address_space(void);

int robo_thread_subsystem_init(void);
RoboThread *robo_thread_adopt_bootstrap(void *stack_base, uint64_t stack_top,
                                        size_t stack_size);

RoboThread *robo_thread_create(const char *name,
                               robo_thread_entry_t entry,
                               void *argument);
RoboThread *robo_thread_create_in_process(struct RoboProcess *process,
                                          const char *name,
                                          robo_thread_entry_t entry,
                                          void *argument);
RoboThread *robo_thread_create_user(struct RoboProcess *process,
                                    const char *name,
                                    uint64_t user_rip, uint64_t user_rsp,
                                    uint64_t user_rdi);
void robo_thread_kill_current(void);
struct RoboProcess *robo_thread_process(const RoboThread *thread);
void robo_thread_exit(void);
void robo_thread_yield(void);
void robo_thread_block(void);
int robo_thread_wake(RoboThread *thread);
int robo_thread_sleep_until(uint64_t deadline_ns);

RoboThread *robo_current_thread(void);
robo_thread_id_t robo_thread_id(const RoboThread *thread);
robo_thread_state_t robo_thread_state(const RoboThread *thread);
uint64_t robo_thread_ip(const RoboThread *thread);
uint64_t robo_thread_sp(const RoboThread *thread);

int robo_thread_transition(RoboThread *thread, robo_thread_state_t next);
int robo_thread_is_valid(const RoboThread *thread);

void robo_thread_set_name(RoboThread *thread, const char *name);
void robo_thread_zero(RoboThread *thread);
int robo_thread_stack_alloc(RoboThread *thread);
void robo_thread_stack_setup_context(RoboThread *thread);
void robo_thread_stack_free(RoboThread *thread);
void robo_thread_bootstrap(void);

void robo_thread_dump(const RoboThread *thread);
void robo_thread_report(void);
#ifndef THREAD_HOST_TEST
void robo_thread_fb_report(void);
#endif
int robo_thread_selftest(void);

RoboThread *robo_thread_all(void);

#endif
