#include "syscall/syscall_dispatch.h"
#include "syscall/syscall.h"
#include "syscall/syscall_internal.h"
#include "syscall/syscall_table.h"
#include "syscall/syscall_types.h"
#include "syscall/syscall_validate.h"
#include "syscall/syscall_errors.h"
#include "syscall/syscall_impl.h"
#include "syscall/syscall_numbers.h"
#include "process/process.h"
#include "thread/thread.h"
#include "scheduler/scheduler.h"
#include "kio.h"

static int g_trace;
static uint64_t g_count;
static uint64_t g_sysret;
static unsigned g_slept;
static int g_user_cpl;
static unsigned g_user_timer;
static int g_hello_user;
static int g_ptr_rejected;
static int g_saw_enosys;
static int g_saw_efault;
static int g_saw_ebadf;
static int g_iso_a;
static int g_iso_b;

void robo_syscall_set_trace(int on)
{
    g_trace = on ? 1 : 0;
}

int robo_syscall_trace(void)
{
    return g_trace;
}

uint64_t robo_syscall_count(void)
{
    return g_count;
}

uint64_t robo_sysret_count(void)
{
    return g_sysret;
}

unsigned robo_syscall_slept(void)
{
    return g_slept;
}

void robo_user_note_iframe(const struct robo_iframe *frame)
{
    if (!frame)
        return;
    if ((frame->cs & 3ull) == 3ull) {
        g_user_cpl = 3;
        g_user_timer++;
    }
}

int robo_user_cpl_seen(void)
{
    return g_user_cpl;
}

unsigned robo_user_timer_irqs(void)
{
    return g_user_timer;
}

int robo_hello_from_user(void)
{
    return g_hello_user;
}

int robo_write_ptr_rejected(void)
{
    return g_ptr_rejected;
}

int robo_sys_saw_enosys(void)
{
    return g_saw_enosys;
}

int robo_sys_saw_efault(void)
{
    return g_saw_efault;
}

int robo_sys_saw_ebadf(void)
{
    return g_saw_ebadf;
}

int robo_sys_iso_a(void)
{
    return g_iso_a;
}

int robo_sys_iso_b(void)
{
    return g_iso_b;
}

void robo_syscall_note_hello(void)
{
    g_hello_user = 1;
}

void robo_syscall_note_ptr_rejected(void)
{
    g_ptr_rejected = 1;
    g_saw_efault = 1;
}

void robo_syscall_note_ebadf(void)
{
    g_saw_ebadf = 1;
}

void robo_syscall_note_enosys(void)
{
    g_saw_enosys = 1;
}

void robo_syscall_note_slept(void)
{
    g_slept++;
}

void robo_syscall_note_iso(const char *s, unsigned n)
{
    if (n >= 2 && s[0] == 'A' && s[1] == '\n')
        g_iso_a = 1;
    if (n >= 2 && s[0] == 'B' && s[1] == '\n')
        g_iso_b = 1;
}

static void trace_call(const RoboSyscallContext *ctx, uint64_t ret)
{
    RoboProcess *p;
    RoboThread *t;
    struct robo_iframe *f;

    if (!g_trace || !ctx || !ctx->frame)
        return;
    p = ctx->process;
    t = ctx->thread;
    f = ctx->frame;
    kprintf("SYSCALL\n-------\n");
    kprintf("PID      : %llu\n", (unsigned long long)(p ? p->id : 0));
    kprintf("TID      : %llu\n", (unsigned long long)(t ? t->id : 0));
    kprintf("Number   : %llu\n", (unsigned long long)ctx->number);
    kprintf("Name     : %s\n\n", robo_syscall_name(ctx->number));
    kprintf("ARG1     : 0x%llx\n", (unsigned long long)f->rdi);
    kprintf("ARG2     : 0x%llx\n", (unsigned long long)f->rsi);
    kprintf("ARG3     : 0x%llx\n", (unsigned long long)f->rdx);
    kprintf("Return   : 0x%llx\n", (unsigned long long)ret);
}

struct robo_iframe *robo_syscall_dispatch(struct robo_iframe *frame)
{
    RoboSyscallContext ctx;
    RoboSyscallHandler handler;
    uint64_t r;
    int user_thread;

    if (!frame)
        return 0;

    ctx.frame = frame;
    ctx.thread = robo_current_thread();
    ctx.process = robo_current_process();
    ctx.number = frame->rax;
    user_thread = ctx.thread && ctx.thread->kind == ROBO_THREAD_USER;

    if (user_thread && (frame->cs & 3ull) == 3ull)
        g_user_cpl = 3;

    g_count++;

    if (user_thread)
        __asm__ volatile ("sti" ::: "memory");

    handler = robo_syscall_lookup(ctx.number);
    if (!handler) {
        robo_syscall_note_enosys();
        r = robo_sys_err(ROBO_ENOSYS);
    } else {
        r = handler(&ctx);
    }

    frame->rax = r;
    if ((int64_t)r == -(int64_t)ROBO_ENOSYS)
        robo_syscall_note_enosys();
    if ((int64_t)r == -(int64_t)ROBO_EFAULT)
        robo_syscall_note_ptr_rejected();
    if ((int64_t)r == -(int64_t)ROBO_EBADF)
        robo_syscall_note_ebadf();

    trace_call(&ctx, r);

    if (user_thread && !robo_user_return_ok(frame)) {
        __asm__ volatile ("cli" ::: "memory");
        if (ctx.process && ctx.process != robo_kernel_process())
            robo_process_exit((int)ROBO_EFAULT);
        return robo_scheduler_resched(frame);
    }

    if (user_thread && (frame->cs & 3ull) == 3ull)
        g_sysret++;

    if (user_thread)
        __asm__ volatile ("cli" ::: "memory");
    return frame;
}
