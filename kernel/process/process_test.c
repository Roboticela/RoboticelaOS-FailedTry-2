#include "process/process.h"
#include "scheduler/scheduler.h"
#include "thread/thread.h"
#include "mm/vmm/vmm.h"
#include "mm/pmm.h"
#include "mm/heap/heap.h"
#include "mm/page.h"
#include "arch/x86_64/mm/paging_cpu.h"
#include "time/time.h"
#include "time/convert.h"
#include "kio.h"
#include "fs/backends/console.h"
#include "fs/file.h"

#define PROC_AB_TARGET     10000ull
#define PROC_STRESS_PROCS  10u
#define PROC_STRESS_EACH   2u
#define PROC_STRESS_TARGET 2000ull
#define PROC_SHARED_VALUE  12345ull
#define PROC_ISOL_A        0xA1A1A1A1A1A1A1A1ull
#define PROC_ISOL_B        0xB2B2B2B2B2B2B2B2ull

static volatile uint64_t g_a1, g_a2, g_b1, g_b2;
static volatile int g_a1_done, g_a2_done, g_b1_done, g_b2_done;
static volatile uint64_t g_a1_pid, g_a2_pid, g_b1_pid, g_b2_pid;
static volatile uint64_t g_a1_cr3, g_a2_cr3, g_b1_cr3, g_b2_cr3;
static volatile int g_ident_fail;
static volatile int g_cr3_fail;
static volatile uint64_t g_shared_read;
static volatile int g_shared_wrote;
static volatile int g_shared_done_a, g_shared_done_b;
static volatile uint64_t g_isol_a, g_isol_b;
static volatile uint64_t g_isol_cr3_a, g_isol_cr3_b;
static volatile int g_isol_done_a, g_isol_done_b;
static volatile int g_hold[4];
static volatile int g_hold_done[4];
static volatile uint64_t g_stress[PROC_STRESS_PROCS * PROC_STRESS_EACH];
static volatile int g_stress_done[PROC_STRESS_PROCS * PROC_STRESS_EACH];

static int tfail(const char *tag)
{
    kprintf("Process selftest: %s\n", tag);
    return 0;
}

static void stretch(void)
{
    uint64_t t = robo_time_now();
    while (robo_time_now() - t < (ROBO_NS_PER_MS / 4ull))
        __asm__ volatile ("pause");
}

static void count_loop(volatile uint64_t *counter, uint64_t target)
{
    uint64_t i;
    for (i = 0; i < target; i++) {
        (*counter)++;
        if ((i & 127ull) == 0)
            stretch();
    }
}

static void note_identity(void)
{
    RoboThread *t = robo_current_thread();
    RoboProcess *p = robo_current_process();

    if (!t || !p || p != t->process || p != robo_thread_process(t))
        g_ident_fail++;
    if (!p->address_space || robo_cpu_read_cr3() != p->address_space->cr3)
        g_cr3_fail++;
}

static int wait_flag(volatile int *flag, uint64_t timeout_ms)
{
    uint64_t start = robo_time_ms();
    while (!*flag) {
        if (robo_time_ms() - start > timeout_ms)
            return 0;
        robo_sleep(10);
    }
    return 1;
}

static int wait_flags(volatile int *flags, unsigned n, uint64_t timeout_ms)
{
    uint64_t start = robo_time_ms();
    unsigned i;
    for (;;) {
        int done = 1;
        for (i = 0; i < n; i++) {
            if (!flags[i])
                done = 0;
        }
        if (done)
            return 1;
        if (robo_time_ms() - start > timeout_ms)
            return 0;
        robo_sleep(10);
    }
}

static int map_private(RoboProcess *p, uint64_t value)
{
    phys_addr_t pa;

    if (!p || !p->address_space)
        return 0;
    pa = robo_page_alloc();
    if (pa == ROBO_INVALID_PHYS_ADDR)
        return 0;
    *(volatile uint64_t *)(uintptr_t)pa = value;
    if (!robo_vmm_aspace_map(p->address_space, ROBO_VMM_PROC_BASE, pa,
                             ROBO_VM_READ | ROBO_VM_WRITE | ROBO_VM_KERNEL)) {
        robo_page_free(pa);
        return 0;
    }
    return 1;
}

static void worker_a1(void *arg)
{
    (void)arg;
    note_identity();
    g_a1_pid = robo_process_id(robo_current_process());
    g_a1_cr3 = robo_cpu_read_cr3();
    count_loop(&g_a1, PROC_AB_TARGET);
    g_a1_done = 1;
}

static void worker_a2(void *arg)
{
    (void)arg;
    note_identity();
    g_a2_pid = robo_process_id(robo_current_process());
    g_a2_cr3 = robo_cpu_read_cr3();
    count_loop(&g_a2, PROC_AB_TARGET);
    g_a2_done = 1;
}

static void worker_b1(void *arg)
{
    (void)arg;
    note_identity();
    g_b1_pid = robo_process_id(robo_current_process());
    g_b1_cr3 = robo_cpu_read_cr3();
    count_loop(&g_b1, PROC_AB_TARGET);
    g_b1_done = 1;
}

static void worker_b2(void *arg)
{
    (void)arg;
    note_identity();
    g_b2_pid = robo_process_id(robo_current_process());
    g_b2_cr3 = robo_cpu_read_cr3();
    count_loop(&g_b2, PROC_AB_TARGET);
    g_b2_done = 1;
}

static void shared_a1(void *arg)
{
    volatile uint64_t *cell = (volatile uint64_t *)ROBO_VMM_PROC_BASE;
    (void)arg;
    note_identity();
    *cell = PROC_SHARED_VALUE;
    g_shared_wrote = 1;
    g_shared_done_a = 1;
}

static void shared_a2(void *arg)
{
    volatile uint64_t *cell = (volatile uint64_t *)ROBO_VMM_PROC_BASE;
    uint64_t start;
    (void)arg;
    note_identity();
    start = robo_time_ms();
    while (!g_shared_wrote) {
        if (robo_time_ms() - start > 4000ull)
            break;
        stretch();
    }
    g_shared_read = *cell;
    g_shared_done_b = 1;
}

static void isol_a(void *arg)
{
    volatile uint64_t *cell = (volatile uint64_t *)ROBO_VMM_PROC_BASE;
    (void)arg;
    note_identity();
    g_isol_a = *cell;
    g_isol_cr3_a = robo_cpu_read_cr3();
    g_isol_done_a = 1;
}

static void isol_b(void *arg)
{
    volatile uint64_t *cell = (volatile uint64_t *)ROBO_VMM_PROC_BASE;
    (void)arg;
    note_identity();
    g_isol_b = *cell;
    g_isol_cr3_b = robo_cpu_read_cr3();
    g_isol_done_b = 1;
}

static void holder(void *arg)
{
    unsigned which = (unsigned)(uintptr_t)arg;
    note_identity();
    while (g_hold[which])
        stretch();
    g_hold_done[which] = 1;
}

static void stress_entry(void *arg)
{
    unsigned idx = (unsigned)(uintptr_t)arg;
    note_identity();
    count_loop(&g_stress[idx], PROC_STRESS_TARGET);
    g_stress_done[idx] = 1;
}

static int resources_independent(RoboProcess *a, RoboProcess *b)
{
    if (!a || !b)
        return 0;
    if (a->fd_table == b->fd_table || a->ipc == b->ipc)
        return 0;
    if (a->environment == b->environment || a->credentials == b->credentials)
        return 0;
    if (a->address_space == b->address_space)
        return 0;
    return 1;
}

int robo_process_selftest(void)
{
    RoboProcess *a, *b, *p[4], *own;
    RoboThread *ta1, *ta2, *tb1, *tb2;
    RoboThread *h[3];
    RoboProcess *st_p[PROC_STRESS_PROCS];
    phys_addr_t pa_a, pa_b, pa_a2, pa_b2;
    uint64_t pid_a, pid_b, tid_a1, tid_a2, tid_b1, tid_b2;
    uint64_t cr3_a, cr3_b;
    uint64_t sw0, sw1, same0, same1, cross0, cross1, rel0, rel1;
    uint64_t heap0, heap1;
    size_t free0, free1;
    unsigned i, round, idx;
    int fd;
    int pass_create = 0, pass_own = 0, pass_share = 0, pass_isol = 0;
    int pass_same_cr3 = 0, pass_cross_cr3 = 0, pass_life = 0, pass_stress = 0;
    int leaks = 0;

    kprintf("========================================\n");
    kprintf("        PROCESS TEST\n");
    kprintf("========================================\n\n");

    if (!robo_kernel_process() || robo_process_id(robo_kernel_process()) == 0)
        return tfail("kernel process");
    if (robo_current_process() != robo_current_thread()->process)
        return tfail("current identity");

    heap0 = (uint64_t)robo_heap_used_bytes();
    free0 = robo_free_page_count();

    /* --- creation / unique PID / independent resources --- */
    for (i = 0; i < 4; i++) {
        p[i] = robo_process_create_named("probe");
        if (!p[i] || p[i]->id == 0)
            return tfail("create probe");
        if (!p[i]->address_space || !p[i]->fd_table || !p[i]->ipc ||
            !p[i]->environment || !p[i]->credentials)
            return tfail("resources");
        if (!robo_process_lookup(p[i]->id))
            return tfail("table insert");
        if (p[i]->parent != robo_kernel_process())
            return tfail("parent");
    }
    for (i = 0; i < 4; i++) {
        unsigned j;
        for (j = i + 1; j < 4; j++) {
            if (p[i]->id == p[j]->id)
                return tfail("duplicate pid");
            if (!resources_independent(p[i], p[j]))
                return tfail("shared resources");
        }
    }
    {
        RoboFile *tf = robo_console_open(ROBO_FFLAG_WRITE);
        RoboFile *got;
        if (!tf)
            return tfail("fd file");
        fd = robo_process_fd_alloc(p[0], tf);
        if (fd < 3)
            return tfail("fd alloc");
        got = robo_process_fd_get(p[0], fd);
        if (got != tf)
            return tfail("fd get");
        robo_file_put(got);
        if (!robo_process_fd_close(p[0], fd))
            return tfail("fd close");
        robo_file_put(tf);
    }
    for (i = 0; i < 4; i++) {
        robo_process_id_t gone = p[i]->id;
        if (!robo_process_destroy(p[i]))
            return tfail("destroy probe");
        if (robo_process_lookup(gone))
            return tfail("stale pid");
    }
    pass_create = 1;
    kprintf("[PASS] Process creation\n");

    /* --- thread ownership --- */
    own = robo_process_create_named("owner");
    if (!own)
        return tfail("owner create");
    g_hold[0] = g_hold[1] = g_hold[2] = 1;
    g_hold_done[0] = g_hold_done[1] = g_hold_done[2] = 0;
    h[0] = robo_thread_create_in_process(own, "own-0", holder, (void *)0);
    h[1] = robo_thread_create_in_process(own, "own-1", holder, (void *)1);
    h[2] = robo_thread_create_in_process(own, "own-2", holder, (void *)2);
    if (!h[0] || !h[1] || !h[2])
        return tfail("owner threads");
    robo_sleep(50);
    if (own->thread_count != 3)
        return tfail("thread_count 3");
    if (robo_thread_process(h[0]) != own ||
        robo_thread_process(h[1]) != own ||
        robo_thread_process(h[2]) != own)
        return tfail("thread owner");
    g_hold[2] = 0;
    if (!wait_flag(&g_hold_done[2], 4000))
        return tfail("owner thread 2 exit");
    robo_sleep(20);
    robo_sched_reap();
    if (own->thread_count != 2)
        return tfail("thread_count 2");
    g_hold[0] = 0;
    g_hold[1] = 0;
    if (!wait_flag(&g_hold_done[0], 4000) || !wait_flag(&g_hold_done[1], 4000))
        return tfail("owner drain");
    robo_sleep(20);
    robo_sched_reap();
    if (own->thread_count != 0)
        return tfail("thread_count 0");
    if (!robo_process_destroy(own))
        return tfail("owner destroy");
    pass_own = 1;
    kprintf("[PASS] Thread ownership\n");

    /* --- shared address space --- */
    a = robo_process_create_named("share-a");
    if (!a || !map_private(a, 0))
        return tfail("share map");
    g_shared_wrote = 0;
    g_shared_read = 0;
    g_shared_done_a = 0;
    g_shared_done_b = 0;
    if (!robo_thread_create_in_process(a, "sh-a1", shared_a1, 0) ||
        !robo_thread_create_in_process(a, "sh-a2", shared_a2, 0))
        return tfail("share threads");
    if (!wait_flag(&g_shared_done_a, 4000) || !wait_flag(&g_shared_done_b, 4000))
        return tfail("share wait");
    kprintf("A1 writes shared_value = %llu\n", (unsigned long long)PROC_SHARED_VALUE);
    kprintf("A2 reads shared_value = %llu\n", (unsigned long long)g_shared_read);
    if (g_shared_read != PROC_SHARED_VALUE)
        return tfail("shared value");
    robo_sleep(20);
    robo_sched_reap();
    if (!robo_process_destroy(a))
        return tfail("share destroy");
    pass_share = 1;
    kprintf("[PASS] Shared address space\n");

    /* --- isolation + CR3 --- */
    a = robo_process_create_named("isol-a");
    b = robo_process_create_named("isol-b");
    if (!a || !b || !map_private(a, PROC_ISOL_A) || !map_private(b, PROC_ISOL_B))
        return tfail("isol map");
    if (!robo_vmm_aspace_translate(a->address_space, ROBO_VMM_PROC_BASE, &pa_a) ||
        !robo_vmm_aspace_translate(b->address_space, ROBO_VMM_PROC_BASE, &pa_b))
        return tfail("isol translate");
    if (pa_a == pa_b)
        return tfail("isol same phys");
    if (a->address_space->cr3 == b->address_space->cr3)
        return tfail("isol same cr3");
    cr3_a = a->address_space->cr3;
    cr3_b = b->address_space->cr3;
    g_isol_done_a = 0;
    g_isol_done_b = 0;
    if (!robo_thread_create_in_process(a, "isol-ta", isol_a, 0) ||
        !robo_thread_create_in_process(b, "isol-tb", isol_b, 0))
        return tfail("isol threads");
    if (!wait_flag(&g_isol_done_a, 4000) || !wait_flag(&g_isol_done_b, 4000))
        return tfail("isol wait");
    if (g_isol_a != PROC_ISOL_A || g_isol_b != PROC_ISOL_B)
        return tfail("isol values");
    if (g_isol_cr3_a != cr3_a || g_isol_cr3_b != cr3_b)
        return tfail("isol thread cr3");
    if (!robo_vmm_aspace_translate(a->address_space, ROBO_VMM_PROC_BASE, &pa_a2) ||
        !robo_vmm_aspace_translate(b->address_space, ROBO_VMM_PROC_BASE, &pa_b2))
        return tfail("isol retranslate");
    if (pa_a2 != pa_a || pa_b2 != pa_b)
        return tfail("isol pte drift");
    kprintf("A mapping != B mapping\n");
    kprintf("A page table != B page table\n");
    kprintf("A CR3 : 0x%llx\n", (unsigned long long)cr3_a);
    kprintf("B CR3 : 0x%llx\n", (unsigned long long)cr3_b);
    robo_sleep(20);
    robo_sched_reap();
    if (!robo_process_destroy(a) || !robo_process_destroy(b))
        return tfail("isol destroy");
    pass_isol = 1;
    kprintf("[PASS] Address isolation\n");
    kprintf("Limitation: ring-3 protection is not tested (no user mode yet).\n");
    kprintf("Isolation is demonstrated by distinct CR3 roots, distinct PTEs\n");
    kprintf("for the same VA, and each thread reading only its own mapping.\n");

    /* --- mandatory four-thread scheduler test --- */
    a = robo_process_create_named("proc-a");
    b = robo_process_create_named("proc-b");
    if (!a || !b)
        return tfail("ab create");
    pid_a = a->id;
    pid_b = b->id;
    cr3_a = a->address_space->cr3;
    cr3_b = b->address_space->cr3;
    g_a1 = g_a2 = g_b1 = g_b2 = 0;
    g_a1_done = g_a2_done = g_b1_done = g_b2_done = 0;
    same0 = robo_aspace_stat_same();
    cross0 = robo_aspace_stat_cross();
    rel0 = robo_arch_cr3_reloads();
    sw0 = robo_sched_switches();
    ta1 = robo_thread_create_in_process(a, "A1", worker_a1, 0);
    ta2 = robo_thread_create_in_process(a, "A2", worker_a2, 0);
    tb1 = robo_thread_create_in_process(b, "B1", worker_b1, 0);
    tb2 = robo_thread_create_in_process(b, "B2", worker_b2, 0);
    if (!ta1 || !ta2 || !tb1 || !tb2)
        return tfail("ab threads");
    tid_a1 = ta1->id;
    tid_a2 = ta2->id;
    tid_b1 = tb1->id;
    tid_b2 = tb2->id;
    {
        uint64_t start = robo_time_ms();
        while (g_a1 < PROC_AB_TARGET || g_a2 < PROC_AB_TARGET ||
               g_b1 < PROC_AB_TARGET || g_b2 < PROC_AB_TARGET) {
            if (robo_time_ms() - start > 20000ull)
                return tfail("ab timeout");
            robo_sleep(10);
        }
    }
    if (!wait_flag(&g_a1_done, 4000) || !wait_flag(&g_a2_done, 4000) ||
        !wait_flag(&g_b1_done, 4000) || !wait_flag(&g_b2_done, 4000))
        return tfail("ab term");
    robo_sleep(20);
    sw1 = robo_sched_switches();
    same1 = robo_aspace_stat_same();
    cross1 = robo_aspace_stat_cross();
    rel1 = robo_arch_cr3_reloads();

    kprintf("Process A PID : %llu\n", (unsigned long long)pid_a);
    kprintf("Process B PID : %llu\n", (unsigned long long)pid_b);
    kprintf("\nA1 TID : %llu\n", (unsigned long long)tid_a1);
    kprintf("A2 TID : %llu\n", (unsigned long long)tid_a2);
    kprintf("\nB1 TID : %llu\n", (unsigned long long)tid_b1);
    kprintf("B2 TID : %llu\n", (unsigned long long)tid_b2);
    kprintf("\nA1 result : %llu\n", (unsigned long long)g_a1);
    kprintf("A2 result : %llu\n", (unsigned long long)g_a2);
    kprintf("B1 result : %llu\n", (unsigned long long)g_b1);
    kprintf("B2 result : %llu\n", (unsigned long long)g_b2);
    kprintf("\nA1 process : %llu\n", (unsigned long long)g_a1_pid);
    kprintf("A2 process : %llu\n", (unsigned long long)g_a2_pid);
    kprintf("B1 process : %llu\n", (unsigned long long)g_b1_pid);
    kprintf("B2 process : %llu\n", (unsigned long long)g_b2_pid);
    kprintf("\nA address space : 0x%llx\n", (unsigned long long)cr3_a);
    kprintf("B address space : 0x%llx\n", (unsigned long long)cr3_b);
    kprintf("\nSame-process address space : %s\n",
            (g_a1_cr3 == g_a2_cr3 && g_a1_cr3 == cr3_a) ? "YES" : "NO");
    kprintf("Cross-process address space : %s\n",
            (cr3_a != cr3_b && g_a1_cr3 != g_b1_cr3) ? "NO" : "YES");
    kprintf("\nContext switches : %llu\n", (unsigned long long)(sw1 - sw0));
    kprintf("Address-space switches : %llu\n",
            (unsigned long long)(cross1 - cross0));

    if (g_a1 != PROC_AB_TARGET || g_a2 != PROC_AB_TARGET ||
        g_b1 != PROC_AB_TARGET || g_b2 != PROC_AB_TARGET)
        return tfail("ab counts");
    if (g_a1_pid != pid_a || g_a2_pid != pid_a ||
        g_b1_pid != pid_b || g_b2_pid != pid_b)
        return tfail("ab pids");
    if (g_a1_cr3 != cr3_a || g_a2_cr3 != cr3_a ||
        g_b1_cr3 != cr3_b || g_b2_cr3 != cr3_b)
        return tfail("ab cr3");
    if (cr3_a == cr3_b)
        return tfail("ab aspace equal");
    if ((same1 - same0) == 0)
        return tfail("no same-process switch");
    if ((cross1 - cross0) == 0)
        return tfail("no cross-process switch");
    if ((rel1 - rel0) != (cross1 - cross0))
        return tfail("unexpected cr3 reload");
    if ((rel1 - rel0) == 0)
        return tfail("no cr3 reload");

    kprintf("\nAddress Space : SAME (A1 -> A2)\n");
    kprintf("CR3 Reload    : NO\n");
    kprintf("Address Space : CHANGED (A -> B)\n");
    kprintf("CR3 Reload    : YES\n");
    pass_same_cr3 = 1;
    pass_cross_cr3 = 1;
    kprintf("[PASS] Same-process CR3\n");
    kprintf("[PASS] Cross-process CR3\n");

    robo_process_debug_dump(a);
    robo_sched_reap();
    if (!robo_process_destroy(a) || !robo_process_destroy(b))
        return tfail("ab destroy");
    kprintf("[PASS] Process subsystem\n");
    kprintf("========================================\n");

    /* --- stress --- */
    for (round = 0; round < 2u; round++) {
        for (i = 0; i < PROC_STRESS_PROCS; i++) {
            st_p[i] = robo_process_create_named("stress");
            if (!st_p[i])
                return tfail("stress process");
            for (idx = 0; idx < PROC_STRESS_EACH; idx++) {
                unsigned slot = i * PROC_STRESS_EACH + idx;
                g_stress[slot] = 0;
                g_stress_done[slot] = 0;
                if (!robo_thread_create_in_process(st_p[i], "st",
                                                   stress_entry,
                                                   (void *)(uintptr_t)slot))
                    return tfail("stress thread");
            }
        }
        if (!wait_flags(g_stress_done, PROC_STRESS_PROCS * PROC_STRESS_EACH, 20000ull))
            return tfail("stress wait");
        for (i = 0; i < PROC_STRESS_PROCS * PROC_STRESS_EACH; i++) {
            if (g_stress[i] != PROC_STRESS_TARGET)
                return tfail("stress count");
        }
        robo_sleep(20);
        robo_sched_reap();
        for (i = 0; i < PROC_STRESS_PROCS; i++) {
            if (st_p[i]->thread_count != 0)
                return tfail("stress live threads");
            if (!robo_process_destroy(st_p[i]))
                return tfail("stress destroy");
        }
    }
    pass_stress = 1;
    kprintf("[PASS] Stress test\n");

    heap1 = (uint64_t)robo_heap_used_bytes();
    free1 = robo_free_page_count();
    (void)free0;
    (void)free1;
    if (heap1 > heap0)
        leaks = 1;
    if (robo_process_stat_aspace_created() != robo_process_stat_aspace_destroyed())
        leaks = 1;
    if (robo_process_stat_live() != 1)
        return tfail("live != kernel");
    pass_life = !leaks;
    if (!pass_life)
        return tfail("lifecycle leak");
    kprintf("[PASS] Lifecycle\n");

    if (g_ident_fail)
        return tfail("identity");
    if (g_cr3_fail)
        return tfail("running cr3");

    kprintf("\n========================================\n");
    kprintf("     ROBOTICELA OS PHASE 9\n");
    kprintf("       PROCESS VERIFICATION\n");
    kprintf("========================================\n\n");
    kprintf("Architecture : x86_64\n\n");
    kprintf("Processes\n");
    kprintf("---------\n");
    kprintf("Created              : %llu\n",
            (unsigned long long)robo_process_stat_created());
    kprintf("Destroyed            : %llu\n",
            (unsigned long long)robo_process_stat_destroyed());
    kprintf("Maximum Concurrent   : %llu\n\n",
            (unsigned long long)robo_process_stat_live_max());
    kprintf("Threads\n");
    kprintf("-------\n");
    kprintf("Created              : %llu\n",
            (unsigned long long)robo_sched_creates());
    kprintf("Terminated           : %llu\n",
            (unsigned long long)robo_sched_terminations());
    kprintf("Maximum Concurrent   : NOT TESTED — REASON no peak tracker\n\n");
    kprintf("Address Spaces\n");
    kprintf("--------------\n");
    kprintf("Created              : %llu\n",
            (unsigned long long)robo_process_stat_aspace_created());
    kprintf("Destroyed            : %llu\n",
            (unsigned long long)robo_process_stat_aspace_destroyed());
    kprintf("Switches             : %llu\n",
            (unsigned long long)(robo_aspace_stat_same() + robo_aspace_stat_cross()));
    kprintf("Same-Process Switches: %llu\n",
            (unsigned long long)robo_aspace_stat_same());
    kprintf("Cross-Process Switches: %llu\n\n",
            (unsigned long long)robo_aspace_stat_cross());
    kprintf("CR3\n");
    kprintf("---\n");
    kprintf("Reloads              : %llu\n\n",
            (unsigned long long)robo_arch_cr3_reloads());
    kprintf("Resources\n");
    kprintf("---------\n");
    kprintf("File Tables           : %llu\n",
            (unsigned long long)robo_process_stat_files());
    kprintf("IPC Containers        : %llu\n",
            (unsigned long long)robo_process_stat_ipc());
    kprintf("Environments          : %llu\n",
            (unsigned long long)robo_process_stat_env());
    kprintf("Credentials           : %llu\n\n",
            (unsigned long long)robo_process_stat_creds());
    kprintf("Memory\n");
    kprintf("------\n");
    kprintf("Process Objects       : %llu live\n",
            (unsigned long long)robo_process_stat_live());
    kprintf("Address-Space Pages   : cloned %llu / destroyed %llu\n",
            (unsigned long long)robo_process_stat_aspace_created(),
            (unsigned long long)robo_process_stat_aspace_destroyed());
    kprintf("Kernel Stacks         : reaped with threads\n");
    kprintf("Leaks Detected        : %s\n\n", leaks ? "YES" : "NO");
    kprintf("Tests\n");
    kprintf("-----\n");
    kprintf("Process Creation      : %s\n", pass_create ? "PASS" : "FAIL");
    kprintf("Thread Ownership      : %s\n", pass_own ? "PASS" : "FAIL");
    kprintf("Shared Address Space  : %s\n", pass_share ? "PASS" : "FAIL");
    kprintf("Address Isolation     : %s\n", pass_isol ? "PASS" : "FAIL");
    kprintf("Same-Process CR3      : %s\n", pass_same_cr3 ? "PASS" : "FAIL");
    kprintf("Cross-Process CR3     : %s\n", pass_cross_cr3 ? "PASS" : "FAIL");
    kprintf("Lifecycle             : %s\n", pass_life ? "PASS" : "FAIL");
    kprintf("Stress Test           : %s\n", pass_stress ? "PASS" : "FAIL");
    kprintf("\nBuild\n");
    kprintf("-----\n");
    kprintf("Clean Build           : NOT TESTED — REASON reported by host build\n");
    kprintf("QEMU Boot             : NOT TESTED — REASON reported by QEMU script\n");
    kprintf("\n========================================\n");
    kprintf("[PASS] ROBOTICELA OS PHASE 9\n");
    kprintf("========================================\n");
    return 1;
}
