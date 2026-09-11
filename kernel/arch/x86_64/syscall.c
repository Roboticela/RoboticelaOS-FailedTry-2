#include "arch/x86_64/syscall.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/context/context.h"
#include "exec/elf/elf_validate.h"
#include "mm/vmm/vmm.h"
#include "mm/page.h"
#include "process/process.h"
#include "thread/thread.h"
#include "scheduler/scheduler.h"
#include "kio.h"

#define IA32_EFER      0xC0000080u
#define IA32_STAR      0xC0000081u
#define IA32_LSTAR     0xC0000082u
#define IA32_FMASK     0xC0000084u
#define IA32_EFER_SCE  (1ull << 0)
#define ROBO_WRITE_MAX 4096u

extern void robo_syscall_entry(void);

static int g_user_cpl;
static unsigned g_user_timer;
static int g_hello_user;
static int g_ptr_rejected;

static uint64_t rdmsr(uint32_t id)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(id));
    return ((uint64_t)hi << 32) | lo;
}

static void wrmsr(uint32_t id, uint64_t v)
{
    uint32_t lo = (uint32_t)v;
    uint32_t hi = (uint32_t)(v >> 32);
    __asm__ volatile ("wrmsr" : : "c"(id), "a"(lo), "d"(hi) : "memory");
}

void robo_syscall_init(void)
{
    uint64_t efer, star, lstar;
    efer = rdmsr(IA32_EFER) | IA32_EFER_SCE;
    wrmsr(IA32_EFER, efer);
    star = ((uint64_t)ROBO_GDT_KERNEL_DATA << 48) |
           ((uint64_t)ROBO_GDT_KERNEL_CODE << 32);
    wrmsr(IA32_STAR, star);
    lstar = (uint64_t)(uintptr_t)robo_syscall_entry;
    wrmsr(IA32_LSTAR, lstar);
    wrmsr(IA32_FMASK, ROBO_RFLAGS_IF);
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

static int user_range_mapped(RoboAddressSpace *as, uint64_t va, uint64_t len)
{
    uint64_t page, end;
    if (!as || !len)
        return 1;
    if (!robo_user_va_ok(va, len))
        return 0;
    if (!robo_u64_add_ok(va, len, &end))
        return 0;
    page = va & ~ROBO_PAGE_MASK;
    for (; page < end; page += ROBO_PAGE_SIZE) {
        phys_addr_t pa;
        if (!robo_vmm_aspace_translate(as, (virt_addr_t)page, &pa))
            return 0;
        (void)pa;
    }
    return 1;
}

static unsigned copy_from_user(RoboAddressSpace *as, uint64_t uva, uint64_t len, char *dst)
{
    unsigned got = 0;
    if (!as || !dst || !len)
        return 0;
    if (!user_range_mapped(as, uva, len))
        return 0;
    while (got < len) {
        uint64_t page = (uva + got) & ~ROBO_PAGE_MASK;
        uint64_t off = (uva + got) & ROBO_PAGE_MASK;
        uint64_t chunk = ROBO_PAGE_SIZE - off;
        phys_addr_t pa;
        const char *src;
        unsigned i;
        if (chunk > len - got)
            chunk = len - got;
        if (!robo_vmm_aspace_translate(as, (virt_addr_t)page, &pa))
            return 0;
        src = (const char *)(uintptr_t)(pa + off);
        for (i = 0; i < chunk; i++)
            dst[got + i] = src[i];
        got += (unsigned)chunk;
    }
    return got;
}

static int msg_is_hello(const char *s, unsigned n)
{
    const char *want = "Hello from user space.\n";
    unsigned i;
    for (i = 0; want[i]; i++) {
        if (i >= n || s[i] != want[i])
            return 0;
    }
    return 1;
}

static struct robo_iframe *sys_write(struct robo_iframe *frame)
{
    RoboProcess *p = robo_current_process();
    RoboAddressSpace *as;
    uint64_t fd = frame->rdi;
    uint64_t buf = frame->rsi;
    uint64_t len = frame->rdx;
    char tmp[ROBO_WRITE_MAX];
    unsigned n;

    as = p && p->address_space ? p->address_space : 0;
    if (fd != 1ull && fd != 2ull) {
        frame->rax = (uint64_t)-1;
        return frame;
    }
    if (!len) {
        frame->rax = 0;
        return frame;
    }
    if (len > ROBO_WRITE_MAX)
        len = ROBO_WRITE_MAX;
    if (!as || !user_range_mapped(as, buf, len)) {
        g_ptr_rejected = 1;
        frame->rax = (uint64_t)-1;
        return frame;
    }
    n = copy_from_user(as, buf, len, tmp);
    if (!n) {
        g_ptr_rejected = 1;
        frame->rax = (uint64_t)-1;
        return frame;
    }
    if (msg_is_hello(tmp, n))
        g_hello_user = 1;
    kwrite(tmp, n);
    frame->rax = n;
    return frame;
}

static struct robo_iframe *sys_exit(struct robo_iframe *frame)
{
    RoboProcess *p = robo_current_process();
    int status = (int)frame->rdi;
    if (p && p != robo_kernel_process())
        p->exit_status = status;
    robo_thread_kill_current();
    return robo_scheduler_resched(frame);
}

struct robo_iframe *robo_syscall_dispatch(struct robo_iframe *frame)
{
    RoboThread *th;
    if (!frame)
        return 0;
    th = robo_current_thread();
    if (th && th->kind == ROBO_THREAD_USER && (frame->cs & 3ull) == 3ull)
        g_user_cpl = 3;
    if (frame->rax == ROBO_SYS_WRITE)
        return sys_write(frame);
    if (frame->rax == ROBO_SYS_EXIT)
        return sys_exit(frame);
    frame->rax = (uint64_t)-1;
    return frame;
}
