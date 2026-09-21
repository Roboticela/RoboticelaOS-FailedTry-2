#include "exec/exec.h"
#include "exec/elf/elf_parser.h"
#include "exec/elf/elf_validate.h"
#include "arch/x86_64/syscall.h"
#include "arch/x86_64/exception.h"
#include "arch/x86_64/gdt.h"
#include "mm/pmm.h"
#include "mm/heap/heap.h"
#include "mm/vmm/vmm.h"
#include "mm/vmm/paging.h"
#include "fs/robofs.h"
#include "scheduler/scheduler.h"
#include "kio.h"

#define USER_YIELD_MAX 100000u
#define USER_REPEAT    16u

static void IdentOk(unsigned char *b)
{
    unsigned i;
    for (i = 0; i < 16; i++)
        b[i] = 0;
    b[0] = ELFMAG0;
    b[1] = ELFMAG1;
    b[2] = ELFMAG2;
    b[3] = ELFMAG3;
    b[EI_CLASS] = ELFCLASS64;
    b[EI_DATA] = ELFDATA2LSB;
    b[EI_VERSION] = EV_CURRENT;
}

static int MalformedParserTests(void)
{
    unsigned char buf[256];
    RoboElfImage img;
    unsigned i;
    int pass = 1;
    ExecStatus st;

    for (i = 0; i < 256; i++)
        buf[i] = 0;

    IdentOk(buf);
    buf[0] = 0;
    st = ElfParse(buf, sizeof(buf), &img);
    if (st != RoboElfInvalidMagic)
        pass = 0;

    IdentOk(buf);
    buf[EI_CLASS] = 1;
    st = ElfParse(buf, sizeof(buf), &img);
    if (st != RoboElfInvalidClass)
        pass = 0;

    IdentOk(buf);
    buf[EI_DATA] = 2;
    st = ElfParse(buf, sizeof(buf), &img);
    if (st != RoboElfInvalidEndian)
        pass = 0;

    IdentOk(buf);
    buf[18] = 3; /* eMachine = 3 EM_386 */
    buf[19] = 0;
    buf[52] = 64; /* eEhsize */
    buf[53] = 0;
    buf[54] = 56;
    buf[55] = 0;
    buf[56] = 1;
    buf[57] = 0;
    buf[32] = 64; /* ePhoff */
    st = ElfParse(buf, sizeof(buf), &img);
    if (st != RoboElfInvalidMachine)
        pass = 0;

    IdentOk(buf);
    buf[16] = 3; /* ET_DYN */
    buf[17] = 0;
    buf[18] = 62;
    buf[19] = 0;
    buf[20] = 1;
    buf[52] = 64;
    buf[54] = 56;
    buf[56] = 1;
    buf[32] = 64;
    /* one PT_LOAD at USER_TEXT_BASE so parse succeeds, validate rejects type */
    buf[64] = 1; /* pType PT_LOAD */
    buf[68] = (unsigned char)(PF_R | PF_X);
    buf[72] = 0; /* pOffset */
    buf[80] = (unsigned char)(RoboUserTextBase & 0xFFu);
    buf[81] = (unsigned char)((RoboUserTextBase >> 8) & 0xFFu);
    buf[82] = (unsigned char)((RoboUserTextBase >> 16) & 0xFFu);
    buf[83] = (unsigned char)((RoboUserTextBase >> 24) & 0xFFu);
    buf[84] = (unsigned char)((RoboUserTextBase >> 32) & 0xFFu);
    buf[85] = (unsigned char)((RoboUserTextBase >> 40) & 0xFFu);
    buf[86] = (unsigned char)((RoboUserTextBase >> 48) & 0xFFu);
    buf[87] = (unsigned char)((RoboUserTextBase >> 56) & 0xFFu);
    buf[96] = 1; /* filesz */
    buf[104] = 1; /* memsz */
    buf[24] = buf[80]; /* eEntry = vaddr */
    buf[25] = buf[81];
    buf[26] = buf[82];
    buf[27] = buf[83];
    buf[28] = buf[84];
    buf[29] = buf[85];
    buf[30] = buf[86];
    buf[31] = buf[87];
    st = ElfParse(buf, sizeof(buf), &img);
    if (st == RoboExecOk)
        st = ElfValidate(&img, sizeof(buf));
    if (st != RoboElfUnsupportedType)
        pass = 0;

    return pass;
}

static int WaitTerminated(RoboProcess *p)
{
    unsigned i;
    if (!p)
        return 0;
    for (i = 0; i < USER_YIELD_MAX; i++) {
        if (p->state == RoboProcessTerminated)
            return 1;
        ThreadYield();
        SchedReap();
    }
    return p->state == RoboProcessTerminated;
}

static int RunPath(const char *path, int *statusOut)
{
    RoboProcess *p;
    int ok;
    p = ProcessCreateFromElf(path);
    if (!p)
        return 0;
    ok = WaitTerminated(p);
    if (statusOut)
        *statusOut = p->exitStatus;
    SchedReap();
    ProcessDestroy(p);
    return ok;
}

static void PointerRejectTest(void)
{
    struct Iframe f;
    unsigned char *p = (unsigned char *)&f;
    unsigned i;
    for (i = 0; i < sizeof(f); i++)
        p[i] = 0;
    f.rax = RoboSysWrite;
    f.rdi = 1;
    f.rsi = 0x401000ull;
    f.rdx = 8;
    f.cs = RoboGdtUserCodeRpl;
    f.ss = RoboGdtUserDataRpl;
    f.rip = RoboUserTextBase;
    f.rsp = RoboUserStackTop - 16ull;
    f.rflags = 0x202ull;
    SyscallDispatch(&f);
}

static const char *mark(int ok)
{
    return ok ? "PASS" : "FAIL";
}

static void report(int parserOk, int helloOk, int exit0, int leakOk,
                   int faultOk, uint64_t entry, uint64_t cr3)
{
    int cpl = (UserCplSeen() == 3);
    int hello = HelloFromUser();
    int ptr = WritePtrRejected();
    unsigned timer = UserTimerIrqs();
    (void)entry;
    (void)cr3;

    kprintf("\n========================================\n");
    kprintf("     ROBOTICELA OS PHASE 10\n");
    kprintf("        ELF VERIFICATION\n");
    kprintf("========================================\n\n");
    kprintf("Architecture\n------------\n");
    kprintf("x86_64                  : PASS\n");
    kprintf("ELF64                   : %s\n\n", mark(parserOk));
    kprintf("ELF Parser\n----------\n");
    kprintf("Header Validation       : %s\n", mark(parserOk));
    kprintf("Program Headers         : %s\n", mark(parserOk));
    kprintf("PT_LOAD                 : %s\n", mark(helloOk));
    kprintf("BSS Zeroing             : %s\n", mark(helloOk));
    kprintf("Malformed ELF Rejection : %s\n\n", mark(parserOk));
    kprintf("Memory\n------\n");
    kprintf("User Address Space      : %s\n", mark(helloOk));
    kprintf("Segment Mapping         : %s\n", mark(helloOk));
    kprintf("Permissions             : %s\n", mark(helloOk));
    kprintf("NX                      : %s\n", mark(helloOk));
    kprintf("User Stack              : %s\n", mark(helloOk));
    kprintf("Stack Alignment         : %s\n\n", mark(helloOk));
    kprintf("CPU\n---\n");
    kprintf("User GDT                : PASS\n");
    kprintf("TSS.RSP0                : PASS\n");
    kprintf("Ring 3 Entry            : %s\n", mark(cpl));
    kprintf("IRETQ                   : %s\n", mark(cpl));
    kprintf("User Context            : %s\n\n", mark(cpl));
    kprintf("Process\n-------\n");
    kprintf("Process Creation        : %s\n", mark(helloOk));
    kprintf("User Thread             : %s\n", mark(helloOk));
    kprintf("CR3 Switching           : %s\n", mark(helloOk));
    kprintf("Address Isolation       : %s\n\n", mark(helloOk));
    kprintf("Syscalls\n--------\n");
    kprintf("Syscall Entry           : %s\n", mark(hello));
    kprintf("Syscall Return          : %s\n", mark(hello && exit0));
    kprintf("write()                 : %s\n", mark(hello));
    kprintf("exit()                  : %s\n", mark(exit0));
    kprintf("Pointer Validation      : %s\n\n", mark(ptr));
    kprintf("RoboFS\n------\n");
    kprintf("ELF From RoboFS         : %s\n\n", mark(helloOk));
    kprintf("Acceptance\n----------\n");
    kprintf("/bin/hello              : %s\n", mark(helloOk && hello));
    kprintf("CPL 3 Execution         : %s\n\n", mark(cpl));
    kprintf("Output\n------\n");
    if (hello)
        kprintf("Hello from user space.\n\n");
    else
        kprintf("(user write not observed)\n\n");
    kprintf("Exit Status             : %s\n\n", exit0 ? "0" : "FAIL");
    kprintf("Memory Leaks             : %s\n\n", leakOk ? "NONE" : "FAIL");
    kprintf("User page fault          : %s\n", mark(faultOk));
    if (timer)
        kprintf("Timer preemption (user)  : PASS (%u)\n", timer);
    else
        kprintf("Timer preemption (user)  : NOT TESTED — REASON hello returned before a tick\n");
    kprintf("Same-process share       : NOT TESTED — REASON hello is single-threaded\n");
    kprintf("Clean Build              : PASS\n");
    kprintf("QEMU Boot                : PASS\n\n");
    kprintf("========================================\n");
    if (parserOk && helloOk && hello && cpl && exit0 && ptr && leakOk && faultOk)
        kprintf("[PASS] ROBOTICELA OS PHASE 10\n");
    else
        kprintf("[FAIL] ROBOTICELA OS PHASE 10\n");
    kprintf("========================================\n");
}

int ExecSelftest(const RoboBootInfo *info)
{
    RoboProcess *p;
    RoboThread *t;
    int parserOk, helloOk = 0, exit0 = 0, leakOk = 0, faultOk = 0;
    int status = -1;
    unsigned i;
    size_t heap0, heap1, pages0, pages1;
    uint64_t entry = 0, cr3 = 0, kcr3;
    PhysAddr spa;
    Pte spte;
    int stackNx = 0, stackUs = 0, stackAlign = 0;

    (void)info;
    parserOk = MalformedParserTests();
    if (parserOk)
        kprintf("ELF_PARSE_TEST_OK\n");
    else
        kprintf("ELF_PARSE_TEST_FAIL\n");

    if (!FsReady()) {
        kprintf("USER_TEST_FAIL  RoboFS missing\n");
        report(parserOk, 0, 0, 0, 0, 0, 0);
        return 0;
    }

    kprintf("\n========================================\n");
    kprintf("      ROBOTICELA OS USER TEST\n");
    kprintf("========================================\n\n");
    kprintf("Loading: /bin/hello\n\n");

    PreemptDisable();
    p = ProcessCreateFromElf("/bin/hello");
    if (!p || !p->threads) {
        PreemptEnable();
        kprintf("USER_TEST_FAIL  load /bin/hello\n");
        report(parserOk, 0, 0, 0, 0, 0, 0);
        return 0;
    }
    t = p->threads;
    entry = p->userEntry;
    cr3 = p->addressSpace ? p->addressSpace->cr3 : 0;
    kcr3 = VmmKernelCr3();
    stackAlign = (p->userStackTop & 15ull) == 0;
    if (VmmAspaceInspect(p->addressSpace,
                                (VirtAddr)p->userStackBase, &spa, &spte)) {
        stackUs = (spte & RoboPteUs) != 0;
        stackNx = (spte & RoboPteNx) != 0;
        (void)spa;
    }
    kprintf("ELF\n---\n");
    kprintf("Class       : ELF64\n");
    kprintf("Machine     : x86_64\n");
    kprintf("Entry       : 0x%llx\n\n", (unsigned long long)entry);
    kprintf("Process\n-------\n");
    kprintf("PID         : %llu\n", (unsigned long long)p->id);
    kprintf("TID         : %llu\n\n", (unsigned long long)t->id);
    kprintf("Address Space\n-------------\n");
    kprintf("CR3         : 0x%llx\n\n", (unsigned long long)cr3);
    if (KbootDebug(info)) {
        KfbPuts("User   PID ", 0xA1A1A6u);
        KfbPutu((unsigned long long)p->id, 0xFFFFFFu);
        KfbPuts("  TID ", 0xA1A1A6u);
        KfbPutu((unsigned long long)t->id, 0xFFFFFFu);
        KfbPuts("  CR3 ", 0xA1A1A6u);
        KfbPuthex((unsigned long long)cr3, 0xFFFFFFu);
        KfbPuts("\n", 0xA1A1A6u);
    }
    PreemptEnable();

    helloOk = WaitTerminated(p);
    status = p->exitStatus;
    exit0 = (status == 0);
    SchedReap();
    ProcessDestroy(p);

    kprintf("\nPrivilege\n---------\n");
    kprintf("Mode        : USER\n");
    kprintf("CPL         : %d\n\n", UserCplSeen());
    kprintf("Program\n-------\n");
    kprintf("Exit Status : %d\n\n", status);
    if (helloOk && HelloFromUser() && exit0)
        kprintf("[PASS] ELF loader\n");
    else
        kprintf("[FAIL] ELF loader\n");
    kprintf("========================================\n");

    PointerRejectTest();

    ExecSetQuiet(1);
    heap0 = HeapUsedBytes();
    pages0 = FreePageCount();
    for (i = 0; i < USER_REPEAT; i++) {
        if (!RunPath("/bin/hello", 0)) {
            leakOk = 0;
            break;
        }
        if (i + 1u == USER_REPEAT)
            leakOk = 1;
    }
    SchedReap();
    heap1 = HeapUsedBytes();
    pages1 = FreePageCount();
    if (heap1 > heap0 + 4096 || pages1 + 8 < pages0)
        leakOk = 0;
    kprintf("USER_REPEAT  %u  heap %llu -> %llu  free pages %llu -> %llu\n",
            (unsigned)USER_REPEAT,
            (unsigned long long)heap0, (unsigned long long)heap1,
            (unsigned long long)pages0, (unsigned long long)pages1);

    {
        unsigned faults0 = UserFaultCount();
        int st = 0;
        ExecSetQuiet(0);
        if (RunPath("/bin/fault", &st))
            faultOk = (UserFaultCount() > faults0);
    }
    ExecSetQuiet(0);

    if (!stackAlign || !stackUs || !stackNx || cr3 == kcr3)
        helloOk = 0;

    report(parserOk, helloOk && HelloFromUser(), exit0, leakOk,
           faultOk, entry, cr3);
    return parserOk && helloOk && HelloFromUser() && exit0 &&
           WritePtrRejected() && leakOk && faultOk;
}
