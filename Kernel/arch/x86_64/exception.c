#include "arch/x86_64/exception.h"
#include "arch/x86_64/timer/pit.h"
#include "arch/x86_64/syscall.h"
#include "scheduler/scheduler.h"
#include "process/process.h"
#include "thread/thread.h"
#include "kio.h"

#include <stddef.h>

extern volatile uint64_t SavedCr2;
extern void PfProbeRead(uint64_t va);
extern char PfProbeReadRip[];
extern char PfProbeReadResume[];
extern void IrqDispatch(struct Iframe *frame);

static unsigned gBpCount;
static unsigned gUserFaults;

static int gPfRecover;
static uint64_t gPfExpectVa;
static uint64_t gPfResume;
static int gPfCaught;
static uint64_t gPfGotCr2;
static uint64_t gPfGotRip;
static uint64_t gPfGotErr;

static const char *ExcName(uint64_t vec)
{
    static const char *names[32] = {
        "Divide Error",
        "Debug",
        "NMI",
        "Breakpoint",
        "Overflow",
        "Bound Range",
        "Invalid Opcode",
        "Device Not Available",
        "Double Fault",
        "Coprocessor Segment",
        "Invalid TSS",
        "Segment Not Present",
        "Stack Fault",
        "General Protection Fault",
        "Page Fault",
        "Reserved",
        "x87 Floating Point",
        "Alignment Check",
        "Machine Check",
        "SIMD Floating Point",
        "Virtualization",
        "Control Protection",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved"
    };
    if (vec < 32)
        return names[vec];
    return "Unknown";
}

static void HaltForever(void)
{
    for (;;)
        __asm__ volatile ("cli; hlt");
}

static void PfHex(const char *label, uint64_t v)
{
    static const char *d = "0123456789abcdef";
    char buf[19];
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 16; i++)
        buf[2 + i] = d[(v >> (60 - 4 * i)) & 15ull];
    buf[18] = 0;
    kprintf("%s%s\n", label, buf);
}

static void PanicRegs(const struct Iframe *f)
{
    kprintf("Vector  : %llu\n", (unsigned long long)f->vector);
    PfHex("Error   : ", f->error);
    PfHex("RIP     : ", f->rip);
    PfHex("CS      : ", f->cs);
    PfHex("RFLAGS  : ", f->rflags);
    PfHex("RSP     : ", f->rsp);
    PfHex("SS      : ", f->ss);
}

static void PfDiagnose(const struct Iframe *f, uint64_t cr2)
{
    uint64_t err = f->error;

    kprintf("========================================\n");
    kprintf("          KERNEL PANIC\n");
    kprintf("========================================\n\n");
    kprintf("Page Fault\n\n");
    PfHex("Address : ", cr2);
    PfHex("RIP     : ", f->rip);
    PfHex("Error   : ", err);
    kprintf("Access  : %s\n", (err & RoboPfWr) ? "WRITE" : "READ");
    kprintf("Mode    : %s\n", (err & RoboPfUs) ? "USER" : "KERNEL");
    kprintf("Present : %s\n", (err & RoboPfP) ? "YES" : "NO");
    kprintf("Reserved : %s\n", (err & RoboPfRsvd) ? "YES" : "NO");
    kprintf("Execute : %s\n", (err & RoboPfId) ? "YES" : "NO");
    kprintf("\n========================================\n");
}

static void PanicPageFault(struct Iframe *f)
{
    uint64_t cr2 = SavedCr2;

    PfDiagnose(f, cr2);

    if (gPfRecover && cr2 == gPfExpectVa) {
        gPfGotCr2 = cr2;
        gPfGotRip = f->rip;
        gPfGotErr = f->error;
        gPfCaught = 1;
        f->rip = gPfResume;
        return;
    }

    HaltForever();
}

static void PanicException(const struct Iframe *f)
{
    kprintf("========================================\n");
    kprintf("          KERNEL PANIC\n");
    kprintf("========================================\n\n");
    kprintf("%s\n\n", ExcName(f->vector));
    PanicRegs(f);
    kprintf("\n========================================\n");
    HaltForever();
}

unsigned ExceptionBreakpointCount(void)
{
    return gBpCount;
}

unsigned UserFaultCount(void)
{
    return gUserFaults;
}

int PfCatchRead(uint64_t va, uint64_t *cr2, uint64_t *rip, uint64_t *err)
{
    gPfCaught = 0;
    gPfGotCr2 = 0;
    gPfGotRip = 0;
    gPfGotErr = 0;
    gPfExpectVa = va;
    gPfResume = (uint64_t)(uintptr_t)PfProbeReadResume;
    gPfRecover = 1;
    __asm__ volatile ("" ::: "memory");
    PfProbeRead(va);
    __asm__ volatile ("" ::: "memory");
    gPfRecover = 0;
    if (!gPfCaught)
        return 0;
    if (cr2) *cr2 = gPfGotCr2;
    if (rip) *rip = gPfGotRip;
    if (err) *err = gPfGotErr;
    return 1;
}

uint64_t PfProbeRip(void)
{
    return (uint64_t)(uintptr_t)PfProbeReadRip;
}

static int IframeFromUser(const struct Iframe *f)
{
    return f && ((f->cs & 3ull) == 3ull);
}

static struct Iframe *UserProcessFault(struct Iframe *f, uint64_t cr2)
{
    RoboThread *t = CurrentThread();
    RoboProcess *p = t ? t->process : 0;
    uint64_t err = f->error;

    kprintf("USER PROCESS FAULT\n\n");
    kprintf("PID     : %llu\n", (unsigned long long)(p ? p->id : 0));
    kprintf("TID     : %llu\n", (unsigned long long)(t ? t->id : 0));
    PfHex("Address : ", cr2);
    PfHex("RIP     : ", f->rip);
    PfHex("Error   : ", err);
    kprintf("\n");
    if (f->vector == RoboExcPf) {
        kprintf("Access  : %s\n", (err & RoboPfWr) ? "WRITE" : "READ");
        kprintf("Present : %s\n", (err & RoboPfP) ? "YES" : "NO");
        kprintf("Execute : %s\n", (err & RoboPfId) ? "YES" : "NO");
        kprintf("Mode    : USER\n");
    } else {
        kprintf("Vector  : %llu\n", (unsigned long long)f->vector);
        kprintf("Mode    : USER\n");
    }
    ThreadKillCurrent();
    gUserFaults++;
    return SchedulerResched(f);
}

struct Iframe *ExceptionDispatch(struct Iframe *frame)
{
    if (!frame) {
        HaltForever();
        return 0;
    }

    if (frame->vector == RoboExcBp) {
        gBpCount++;
        kprintf("CPU  breakpoint  RIP 0x%llx\n", (unsigned long long)frame->rip);
        return frame;
    }

    if (frame->vector == RoboVecResched)
        return SchedulerResched(frame);

    if (frame->vector >= 32 && frame->vector < 48) {
        if (frame->vector == RoboIrqBase)
            UserNoteIframe(frame);
        IrqDispatch(frame);
        if (frame->vector == RoboIrqBase)
            return SchedulerTick(frame);
        return frame;
    }

    if (IframeFromUser(frame))
        return UserProcessFault(frame,
                                 (frame->vector == RoboExcPf) ? SavedCr2 : 0);

    if (frame->vector == RoboExcPf) {
        PanicPageFault(frame);
        return frame;
    }

    PanicException(frame);
    return frame;
}
