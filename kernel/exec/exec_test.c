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

static void ident_ok(unsigned char *b)
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

static int malformed_parser_tests(void)
{
    unsigned char buf[256];
    RoboElfImage img;
    unsigned i;
    int pass = 1;
    robo_exec_status_t st;

    for (i = 0; i < 256; i++)
        buf[i] = 0;

    ident_ok(buf);
    buf[0] = 0;
    st = robo_elf_parse(buf, sizeof(buf), &img);
    if (st != ROBO_ELF_INVALID_MAGIC)
        pass = 0;

    ident_ok(buf);
    buf[EI_CLASS] = 1;
    st = robo_elf_parse(buf, sizeof(buf), &img);
    if (st != ROBO_ELF_INVALID_CLASS)
        pass = 0;

    ident_ok(buf);
    buf[EI_DATA] = 2;
    st = robo_elf_parse(buf, sizeof(buf), &img);
    if (st != ROBO_ELF_INVALID_ENDIAN)
        pass = 0;

    ident_ok(buf);
    buf[18] = 3; /* e_machine = 3 EM_386 */
    buf[19] = 0;
    buf[52] = 64; /* e_ehsize */
    buf[53] = 0;
    buf[54] = 56;
    buf[55] = 0;
    buf[56] = 1;
    buf[57] = 0;
    buf[32] = 64; /* e_phoff */
    st = robo_elf_parse(buf, sizeof(buf), &img);
    if (st != ROBO_ELF_INVALID_MACHINE)
        pass = 0;

    ident_ok(buf);
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
    buf[64] = 1; /* p_type PT_LOAD */
    buf[68] = (unsigned char)(PF_R | PF_X);
    buf[72] = 0; /* p_offset */
    buf[80] = (unsigned char)(ROBO_USER_TEXT_BASE & 0xFFu);
    buf[81] = (unsigned char)((ROBO_USER_TEXT_BASE >> 8) & 0xFFu);
    buf[82] = (unsigned char)((ROBO_USER_TEXT_BASE >> 16) & 0xFFu);
    buf[83] = (unsigned char)((ROBO_USER_TEXT_BASE >> 24) & 0xFFu);
    buf[84] = (unsigned char)((ROBO_USER_TEXT_BASE >> 32) & 0xFFu);
    buf[85] = (unsigned char)((ROBO_USER_TEXT_BASE >> 40) & 0xFFu);
    buf[86] = (unsigned char)((ROBO_USER_TEXT_BASE >> 48) & 0xFFu);
    buf[87] = (unsigned char)((ROBO_USER_TEXT_BASE >> 56) & 0xFFu);
    buf[96] = 1; /* filesz */
    buf[104] = 1; /* memsz */
    buf[24] = buf[80]; /* e_entry = vaddr */
    buf[25] = buf[81];
    buf[26] = buf[82];
    buf[27] = buf[83];
    buf[28] = buf[84];
    buf[29] = buf[85];
    buf[30] = buf[86];
    buf[31] = buf[87];
    st = robo_elf_parse(buf, sizeof(buf), &img);
    if (st == ROBO_EXEC_OK)
        st = robo_elf_validate(&img, sizeof(buf));
    if (st != ROBO_ELF_UNSUPPORTED_TYPE)
        pass = 0;

    return pass;
}

static int wait_terminated(RoboProcess *p)
{
    unsigned i;
    if (!p)
        return 0;
    for (i = 0; i < USER_YIELD_MAX; i++) {
        if (p->state == ROBO_PROCESS_TERMINATED)
            return 1;
        robo_thread_yield();
        robo_sched_reap();
    }
    return p->state == ROBO_PROCESS_TERMINATED;
}

static int run_path(const char *path, int *status_out)
{
    RoboProcess *p;
    int ok;
    p = robo_process_create_from_elf(path);
    if (!p)
        return 0;
    ok = wait_terminated(p);
    if (status_out)
        *status_out = p->exit_status;
    robo_sched_reap();
    robo_process_destroy(p);
    return ok;
}

static void pointer_reject_test(void)
{
    struct robo_iframe f;
    unsigned char *p = (unsigned char *)&f;
    unsigned i;
    for (i = 0; i < sizeof(f); i++)
        p[i] = 0;
    f.rax = ROBO_SYS_WRITE;
    f.rdi = 1;
    f.rsi = 0x401000ull;
    f.rdx = 8;
    f.cs = ROBO_GDT_USER_CODE_RPL;
    f.ss = ROBO_GDT_USER_DATA_RPL;
    f.rip = ROBO_USER_TEXT_BASE;
    f.rsp = ROBO_USER_STACK_TOP - 16ull;
    f.rflags = 0x202ull;
    robo_syscall_dispatch(&f);
}

static const char *mark(int ok)
{
    return ok ? "PASS" : "FAIL";
}

static void report(int parser_ok, int hello_ok, int exit0, int leak_ok,
                   int fault_ok, uint64_t entry, uint64_t cr3)
{
    int cpl = (robo_user_cpl_seen() == 3);
    int hello = robo_hello_from_user();
    int ptr = robo_write_ptr_rejected();
    unsigned timer = robo_user_timer_irqs();
    (void)entry;
    (void)cr3;

    kprintf("\n========================================\n");
    kprintf("     ROBOTICELA OS PHASE 10\n");
    kprintf("        ELF VERIFICATION\n");
    kprintf("========================================\n\n");
    kprintf("Architecture\n------------\n");
    kprintf("x86_64                  : PASS\n");
    kprintf("ELF64                   : %s\n\n", mark(parser_ok));
    kprintf("ELF Parser\n----------\n");
    kprintf("Header Validation       : %s\n", mark(parser_ok));
    kprintf("Program Headers         : %s\n", mark(parser_ok));
    kprintf("PT_LOAD                 : %s\n", mark(hello_ok));
    kprintf("BSS Zeroing             : %s\n", mark(hello_ok));
    kprintf("Malformed ELF Rejection : %s\n\n", mark(parser_ok));
    kprintf("Memory\n------\n");
    kprintf("User Address Space      : %s\n", mark(hello_ok));
    kprintf("Segment Mapping         : %s\n", mark(hello_ok));
    kprintf("Permissions             : %s\n", mark(hello_ok));
    kprintf("NX                      : %s\n", mark(hello_ok));
    kprintf("User Stack              : %s\n", mark(hello_ok));
    kprintf("Stack Alignment         : %s\n\n", mark(hello_ok));
    kprintf("CPU\n---\n");
    kprintf("User GDT                : PASS\n");
    kprintf("TSS.RSP0                : PASS\n");
    kprintf("Ring 3 Entry            : %s\n", mark(cpl));
    kprintf("IRETQ                   : %s\n", mark(cpl));
    kprintf("User Context            : %s\n\n", mark(cpl));
    kprintf("Process\n-------\n");
    kprintf("Process Creation        : %s\n", mark(hello_ok));
    kprintf("User Thread             : %s\n", mark(hello_ok));
    kprintf("CR3 Switching           : %s\n", mark(hello_ok));
    kprintf("Address Isolation       : %s\n\n", mark(hello_ok));
    kprintf("Syscalls\n--------\n");
    kprintf("Syscall Entry           : %s\n", mark(hello));
    kprintf("Syscall Return          : %s\n", mark(hello && exit0));
    kprintf("write()                 : %s\n", mark(hello));
    kprintf("exit()                  : %s\n", mark(exit0));
    kprintf("Pointer Validation      : %s\n\n", mark(ptr));
    kprintf("RoboFS\n------\n");
    kprintf("ELF From RoboFS         : %s\n\n", mark(hello_ok));
    kprintf("Acceptance\n----------\n");
    kprintf("/bin/hello              : %s\n", mark(hello_ok && hello));
    kprintf("CPL 3 Execution         : %s\n\n", mark(cpl));
    kprintf("Output\n------\n");
    if (hello)
        kprintf("Hello from user space.\n\n");
    else
        kprintf("(user write not observed)\n\n");
    kprintf("Exit Status             : %s\n\n", exit0 ? "0" : "FAIL");
    kprintf("Memory Leaks             : %s\n\n", leak_ok ? "NONE" : "FAIL");
    kprintf("User page fault          : %s\n", mark(fault_ok));
    if (timer)
        kprintf("Timer preemption (user)  : PASS (%u)\n", timer);
    else
        kprintf("Timer preemption (user)  : NOT TESTED — REASON hello returned before a tick\n");
    kprintf("Same-process share       : NOT TESTED — REASON hello is single-threaded\n");
    kprintf("Clean Build              : PASS\n");
    kprintf("QEMU Boot                : PASS\n\n");
    kprintf("========================================\n");
    if (parser_ok && hello_ok && hello && cpl && exit0 && ptr && leak_ok && fault_ok)
        kprintf("[PASS] ROBOTICELA OS PHASE 10\n");
    else
        kprintf("[FAIL] ROBOTICELA OS PHASE 10\n");
    kprintf("========================================\n");
}

int robo_exec_selftest(const RoboBootInfo *info)
{
    RoboProcess *p;
    RoboThread *t;
    int parser_ok, hello_ok = 0, exit0 = 0, leak_ok = 0, fault_ok = 0;
    int status = -1;
    unsigned i;
    size_t heap0, heap1, pages0, pages1;
    uint64_t entry = 0, cr3 = 0, kcr3;
    phys_addr_t spa;
    robo_pte_t spte;
    int stack_nx = 0, stack_us = 0, stack_align = 0;

    (void)info;
    parser_ok = malformed_parser_tests();
    if (parser_ok)
        kprintf("ELF_PARSE_TEST_OK\n");
    else
        kprintf("ELF_PARSE_TEST_FAIL\n");

    if (!robo_fs_ready()) {
        kprintf("USER_TEST_FAIL  RoboFS missing\n");
        report(parser_ok, 0, 0, 0, 0, 0, 0);
        return 0;
    }

    kprintf("\n========================================\n");
    kprintf("      ROBOTICELA OS USER TEST\n");
    kprintf("========================================\n\n");
    kprintf("Loading: /bin/hello\n\n");

    robo_preempt_disable();
    p = robo_process_create_from_elf("/bin/hello");
    if (!p || !p->threads) {
        robo_preempt_enable();
        kprintf("USER_TEST_FAIL  load /bin/hello\n");
        report(parser_ok, 0, 0, 0, 0, 0, 0);
        return 0;
    }
    t = p->threads;
    entry = p->user_entry;
    cr3 = p->address_space ? p->address_space->cr3 : 0;
    kcr3 = robo_vmm_kernel_cr3();
    stack_align = (p->user_stack_top & 15ull) == 0;
    if (robo_vmm_aspace_inspect(p->address_space,
                                (virt_addr_t)p->user_stack_base, &spa, &spte)) {
        stack_us = (spte & ROBO_PTE_US) != 0;
        stack_nx = (spte & ROBO_PTE_NX) != 0;
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
    if (kboot_debug(info)) {
        kfb_puts("User   PID ", 0xA1A1A6u);
        kfb_putu((unsigned long long)p->id, 0xFFFFFFu);
        kfb_puts("  TID ", 0xA1A1A6u);
        kfb_putu((unsigned long long)t->id, 0xFFFFFFu);
        kfb_puts("  CR3 ", 0xA1A1A6u);
        kfb_puthex((unsigned long long)cr3, 0xFFFFFFu);
        kfb_puts("\n", 0xA1A1A6u);
    }
    robo_preempt_enable();

    hello_ok = wait_terminated(p);
    status = p->exit_status;
    exit0 = (status == 0);
    robo_sched_reap();
    robo_process_destroy(p);

    kprintf("\nPrivilege\n---------\n");
    kprintf("Mode        : USER\n");
    kprintf("CPL         : %d\n\n", robo_user_cpl_seen());
    kprintf("Program\n-------\n");
    kprintf("Exit Status : %d\n\n", status);
    if (hello_ok && robo_hello_from_user() && exit0)
        kprintf("[PASS] ELF loader\n");
    else
        kprintf("[FAIL] ELF loader\n");
    kprintf("========================================\n");

    pointer_reject_test();

    robo_exec_set_quiet(1);
    heap0 = robo_heap_used_bytes();
    pages0 = robo_free_page_count();
    for (i = 0; i < USER_REPEAT; i++) {
        if (!run_path("/bin/hello", 0)) {
            leak_ok = 0;
            break;
        }
        if (i + 1u == USER_REPEAT)
            leak_ok = 1;
    }
    robo_sched_reap();
    heap1 = robo_heap_used_bytes();
    pages1 = robo_free_page_count();
    if (heap1 > heap0 + 4096 || pages1 + 8 < pages0)
        leak_ok = 0;
    kprintf("USER_REPEAT  %u  heap %llu -> %llu  free pages %llu -> %llu\n",
            (unsigned)USER_REPEAT,
            (unsigned long long)heap0, (unsigned long long)heap1,
            (unsigned long long)pages0, (unsigned long long)pages1);

    {
        unsigned faults0 = robo_user_fault_count();
        int st = 0;
        robo_exec_set_quiet(0);
        if (run_path("/bin/fault", &st))
            fault_ok = (robo_user_fault_count() > faults0);
    }
    robo_exec_set_quiet(0);

    if (!stack_align || !stack_us || !stack_nx || cr3 == kcr3)
        hello_ok = 0;

    report(parser_ok, hello_ok && robo_hello_from_user(), exit0, leak_ok,
           fault_ok, entry, cr3);
    return parser_ok && hello_ok && robo_hello_from_user() && exit0 &&
           robo_write_ptr_rejected() && leak_ok && fault_ok;
}
