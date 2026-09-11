#include "time/time.h"
#include "time/convert.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/timer/tsc.h"
#include "arch/x86_64/timer/pit.h"
#include "kio.h"

extern void (*irq_stub_table[16])(void);

#define ROBO_TSC_HZ_MIN 10000000ull
#define ROBO_TSC_HZ_MAX 15000000000ull

struct robo_timer_slot {
    int used;
    uint64_t deadline;
    uint64_t period;
    robo_timer_fn callback;
    void *context;
};

static int g_ready;
static enum robo_clock_source g_source;
static struct robo_tsc_info g_tsc;
static uint64_t g_hz;
static uint64_t g_epoch_tsc;
static uint64_t g_last_ns;
static uint64_t g_irq_count;
static uint64_t g_res_ns;
static robo_sched_tick_fn g_sched_tick;
static struct robo_timer_slot g_timers[ROBO_TIMER_MAX];

static void timer_panic(void)
{
    kprintf("========================================\n");
    kprintf("          KERNEL PANIC\n");
    kprintf("========================================\n\n");
    kprintf("Timer initialization failed\n\n");
    kprintf("No reliable monotonic clock source available.\n");
    kprintf("\n========================================\n");
    for (;;)
        __asm__ volatile ("cli; hlt");
}

static uint64_t pit_now_ns(void)
{
    uint64_t irqs, ticks, ns;
    uint32_t div;
    uint16_t rem;
    uint64_t flags;

    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    irqs = g_irq_count;
    rem = robo_pit_latch0();
    div = robo_pit_divisor();
    __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory");

    if (div < 1u)
        div = 1u;
    if (rem > div)
        rem = (uint16_t)div;
    ticks = irqs * (uint64_t)div + (uint64_t)(div - rem);
    ns = robo_ticks_to_ns(ticks, ROBO_PIT_HZ);
    if (ns < g_last_ns)
        ns = g_last_ns;
    g_last_ns = ns;
    return ns;
}

static uint64_t tsc_now_ns(void)
{
    uint64_t t, ns;
    t = robo_x86_tsc_read();
    if (t < g_epoch_tsc)
        t = g_epoch_tsc;
    ns = robo_ticks_to_ns(t - g_epoch_tsc, g_hz);
    if (ns < g_last_ns)
        ns = g_last_ns;
    g_last_ns = ns;
    return ns;
}

uint64_t robo_time_now(void)
{
    if (!g_ready)
        return 0;
    if (g_source == ROBO_CLOCK_TSC)
        return tsc_now_ns();
    return pit_now_ns();
}

uint64_t robo_time_ns(void)
{
    return robo_time_now();
}

uint64_t robo_time_us(void)
{
    return robo_time_now() / ROBO_NS_PER_US;
}

uint64_t robo_time_ms(void)
{
    return robo_time_now() / ROBO_NS_PER_MS;
}

enum robo_clock_source robo_timer_source(void)
{
    return g_source;
}

int robo_timer_tsc_invariant(void)
{
    return g_tsc.invariant;
}

int robo_timer_rdtscp(void)
{
    return g_tsc.rdtscp;
}

uint64_t robo_timer_frequency_hz(void)
{
    return g_hz;
}

uint64_t robo_timer_resolution_ns(void)
{
    return g_res_ns;
}

uint64_t robo_timer_irq_count(void)
{
    return g_irq_count;
}

int robo_timer_ready(void)
{
    return g_ready;
}

void robo_timer_set_sched_tick(robo_sched_tick_fn fn)
{
    g_sched_tick = fn;
}

static void fire_expired(uint64_t now)
{
    int i;
    for (i = 0; i < (int)ROBO_TIMER_MAX; i++) {
        struct robo_timer_slot *s = &g_timers[i];
        if (!s->used || now < s->deadline)
            continue;
        if (s->callback)
            s->callback(s->context);
        if (s->period) {
            s->deadline = robo_time_deadline(s->deadline, s->period);
            if (s->deadline <= now)
                s->deadline = robo_time_deadline(now, s->period);
        } else {
            s->used = 0;
        }
    }
}

void robo_timer_irq(void)
{
    g_irq_count++;
    if (g_ready)
        fire_expired(robo_time_now());
    if (g_sched_tick)
        g_sched_tick();
}

int robo_timer_add(uint64_t deadline_ns, uint64_t period_ns,
                   robo_timer_fn callback, void *context)
{
    int i;
    uint64_t flags;
    if (!callback)
        return 0;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    for (i = 0; i < (int)ROBO_TIMER_MAX; i++) {
        if (g_timers[i].used)
            continue;
        g_timers[i].used = 1;
        g_timers[i].deadline = deadline_ns;
        g_timers[i].period = period_ns;
        g_timers[i].callback = callback;
        g_timers[i].context = context;
        __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory");
        return i + 1;
    }
    __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory");
    return 0;
}

int robo_timer_cancel(int handle)
{
    uint64_t flags;
    int idx;
    if (handle < 1 || handle > (int)ROBO_TIMER_MAX)
        return 0;
    idx = handle - 1;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    g_timers[idx].used = 0;
    g_timers[idx].callback = 0;
    g_timers[idx].context = 0;
    __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory");
    return 1;
}

static void sleep_wake(void *ctx)
{
    volatile int *done = (volatile int *)ctx;
    *done = 1;
}

void robo_sleep(uint64_t milliseconds)
{
    uint64_t deadline, now;
    volatile int done = 0;
    int handle;

    if (!g_ready || milliseconds == 0)
        return;
    deadline = robo_time_deadline(robo_time_now(), milliseconds * ROBO_NS_PER_MS);
    handle = robo_timer_add(deadline, 0, sleep_wake, (void *)&done);
    for (;;) {
        now = robo_time_now();
        if (now >= deadline || done)
            break;
        if (deadline - now > 500000ull)
            __asm__ volatile ("hlt");
        else
            __asm__ volatile ("pause");
    }
    if (handle)
        robo_timer_cancel(handle);
}

static uint64_t measure_resolution(void)
{
    uint64_t prev, cur, min = UINT64_MAX;
    int i, seen = 0;
    prev = robo_time_now();
    for (i = 0; i < 100000; i++) {
        cur = robo_time_now();
        if (cur > prev) {
            uint64_t d = cur - prev;
            if (d < min)
                min = d;
            seen = 1;
            if (seen && i > 64)
                break;
        }
        prev = cur;
    }
    if (!seen || min == UINT64_MAX)
        return 1;
    return min;
}

static int hz_sane(uint64_t hz)
{
    return hz >= ROBO_TSC_HZ_MIN && hz <= ROBO_TSC_HZ_MAX;
}

static void install_irqs(void)
{
    unsigned i;
    for (i = 0; i < 16; i++)
        robo_idt_set(ROBO_IRQ_BASE + i, irq_stub_table[i], 0);
}

int robo_timer_init(void)
{
    uint64_t cal;
    unsigned i;

    if (g_ready)
        return 1;

    for (i = 0; i < ROBO_TIMER_MAX; i++)
        g_timers[i].used = 0;

    robo_tsc_probe(&g_tsc);
    install_irqs();
    robo_pic_init();
    robo_pit_program(ROBO_TIMER_HZ);
    robo_pic_unmask(ROBO_PIC_IRQ_PIT);
    __asm__ volatile ("sti" ::: "memory");

    g_source = ROBO_CLOCK_NONE;
    g_hz = 0;
    g_last_ns = 0;
    g_irq_count = 0;

    if (g_tsc.present) {
        /* ~30 ms PIT channel-2 one-shot. Documented calibration wait. */
        cal = robo_pit_calibrate_tsc(35795u);
        if (hz_sane(cal)) {
            g_hz = cal;
            g_source = ROBO_CLOCK_TSC;
            kprintf("Timer  TSC calibrated against PIT\n");
        } else if (hz_sane(g_tsc.hz)) {
            g_hz = g_tsc.hz;
            g_source = ROBO_CLOCK_TSC;
            kprintf("Timer  TSC frequency from CPUID 0x15\n");
        } else {
            kprintf("Timer  TSC present but frequency unknown\n");
        }
    } else {
        kprintf("Timer  TSC unavailable\n");
    }

    if (g_source == ROBO_CLOCK_TSC) {
        g_epoch_tsc = robo_x86_tsc_read();
        g_tsc.hz = g_hz;
    } else {
        g_source = ROBO_CLOCK_PIT;
        g_hz = ROBO_PIT_HZ;
        kprintf("Timer  using PIT fallback\n");
    }

    g_ready = 1;
    g_last_ns = 0;
    (void)robo_time_now();
    g_res_ns = measure_resolution();
    if (!g_ready || g_source == ROBO_CLOCK_NONE)
        timer_panic();
    return 1;
}

void robo_timer_report(void)
{
    const char *src = "none";
    if (g_source == ROBO_CLOCK_TSC)
        src = "TSC";
    else if (g_source == ROBO_CLOCK_PIT)
        src = "PIT";
    kprintf("Timer initialized\n");
    kprintf("Timer\n");
    kprintf("-----\n");
    kprintf("Clock Source : %s\n", src);
    kprintf("Invariant    : %s\n", g_tsc.invariant ? "YES" : "NO");
    kprintf("RDTSCP       : %s\n", g_tsc.rdtscp ? "YES" : "NO");
    kprintf("Frequency    : %llu Hz\n", (unsigned long long)g_hz);
    kprintf("Resolution   : %llu ns\n", (unsigned long long)g_res_ns);
}

#ifndef TIME_HOST_TEST
void robo_timer_fb_report(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    if (!g_ready)
        return;
    kfb_puts("Timer  ", mute);
    kfb_puts(g_source == ROBO_CLOCK_TSC ? "TSC " : "PIT ", white);
    kfb_putu((unsigned long long)(g_hz / 1000000ull), white);
    kfb_puts(" MHz  res ", mute);
    kfb_putu((unsigned long long)g_res_ns, white);
    kfb_puts(" ns\n", mute);
}
#endif
