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

static int gTrace;
static uint64_t gCount;
static uint64_t gSysret;
static unsigned gSlept;
static int gUserCpl;
static unsigned gUserTimer;
static int gHelloUser;
static int gPtrRejected;
static int gSawEnosys;
static int gSawEfault;
static int gSawEbadf;
static int gIsoA;
static int gIsoB;

void SyscallSetTrace(int on)
{
    gTrace = on ? 1 : 0;
}

int SyscallTrace(void)
{
    return gTrace;
}

uint64_t SyscallCount(void)
{
    return gCount;
}

uint64_t SysretCount(void)
{
    return gSysret;
}

unsigned SyscallSlept(void)
{
    return gSlept;
}

void UserNoteIframe(const struct Iframe *frame)
{
    if (!frame)
        return;
    if ((frame->cs & 3ull) == 3ull) {
        gUserCpl = 3;
        gUserTimer++;
    }
}

int UserCplSeen(void)
{
    return gUserCpl;
}

unsigned UserTimerIrqs(void)
{
    return gUserTimer;
}

int HelloFromUser(void)
{
    return gHelloUser;
}

int WritePtrRejected(void)
{
    return gPtrRejected;
}

int SysSawEnosys(void)
{
    return gSawEnosys;
}

int SysSawEfault(void)
{
    return gSawEfault;
}

int SysSawEbadf(void)
{
    return gSawEbadf;
}

int SysIsoA(void)
{
    return gIsoA;
}

int SysIsoB(void)
{
    return gIsoB;
}

void SyscallNoteHello(void)
{
    gHelloUser = 1;
}

void SyscallNotePtrRejected(void)
{
    gPtrRejected = 1;
    gSawEfault = 1;
}

void SyscallNoteEbadf(void)
{
    gSawEbadf = 1;
}

void SyscallNoteEnosys(void)
{
    gSawEnosys = 1;
}

void SyscallNoteSlept(void)
{
    gSlept++;
}

void SyscallNoteIso(const char *s, unsigned n)
{
    if (n >= 2 && s[0] == 'A' && s[1] == '\n')
        gIsoA = 1;
    if (n >= 2 && s[0] == 'B' && s[1] == '\n')
        gIsoB = 1;
}

static void TraceCall(const RoboSyscallContext *ctx, uint64_t ret)
{
    RoboProcess *p;
    RoboThread *t;
    struct Iframe *f;

    if (!gTrace || !ctx || !ctx->frame)
        return;
    p = ctx->process;
    t = ctx->thread;
    f = ctx->frame;
    kprintf("SYSCALL\n-------\n");
    kprintf("PID      : %llu\n", (unsigned long long)(p ? p->id : 0));
    kprintf("TID      : %llu\n", (unsigned long long)(t ? t->id : 0));
    kprintf("Number   : %llu\n", (unsigned long long)ctx->number);
    kprintf("Name     : %s\n\n", SyscallName(ctx->number));
    kprintf("ARG1     : 0x%llx\n", (unsigned long long)f->rdi);
    kprintf("ARG2     : 0x%llx\n", (unsigned long long)f->rsi);
    kprintf("ARG3     : 0x%llx\n", (unsigned long long)f->rdx);
    kprintf("Return   : 0x%llx\n", (unsigned long long)ret);
}

struct Iframe *SyscallDispatch(struct Iframe *frame)
{
    RoboSyscallContext ctx;
    RoboSyscallHandler handler;
    uint64_t r;
    int userThread;

    if (!frame)
        return 0;

    ctx.frame = frame;
    ctx.thread = CurrentThread();
    ctx.process = CurrentProcess();
    ctx.number = frame->rax;
    userThread = ctx.thread && ctx.thread->kind == RoboThreadUser;

    if (userThread && (frame->cs & 3ull) == 3ull)
        gUserCpl = 3;

    gCount++;

    if (userThread)
        __asm__ volatile ("sti" ::: "memory");

    handler = SyscallLookup(ctx.number);
    if (!handler) {
        SyscallNoteEnosys();
        r = SysErr(RoboEnosys);
    } else {
        r = handler(&ctx);
    }

    frame->rax = r;
    if ((int64_t)r == -(int64_t)RoboEnosys)
        SyscallNoteEnosys();
    if ((int64_t)r == -(int64_t)RoboEfault)
        SyscallNotePtrRejected();
    if ((int64_t)r == -(int64_t)RoboEbadf)
        SyscallNoteEbadf();

    TraceCall(&ctx, r);

    if (userThread && !UserReturnOk(frame)) {
        __asm__ volatile ("cli" ::: "memory");
        if (ctx.process && ctx.process != KernelProcess())
            ProcessExit((int)RoboEfault);
        return SchedulerResched(frame);
    }

    if (userThread && (frame->cs & 3ull) == 3ull)
        gSysret++;

    if (userThread)
        __asm__ volatile ("cli" ::: "memory");
    return frame;
}
