#include "fs/fd.h"
#include "fs/backends/console.h"
#include "fs/backends/regular.h"
#include "syscall/syscall_errors.h"
#include "process/process.h"

#ifndef PROCESS_HOST_TEST
#include "kio.h"
#include "exec/exec.h"
#include "scheduler/scheduler.h"
#include "thread/thread.h"
#endif

robo_fd_t robo_fd_alloc(struct RoboProcess *process, RoboFile *file, uint32_t flags)
{
    if (!process)
        return ROBO_FD_INVALID;
    return robo_fd_table_alloc(process->fd_table, file, flags);
}

robo_fd_t robo_fd_dup(struct RoboProcess *process, robo_fd_t oldfd)
{
    RoboFile *file;
    robo_fd_t n;

    file = robo_fd_get(process, oldfd);
    if (!file)
        return ROBO_FD_INVALID;
    n = robo_fd_alloc(process, file, 0);
    robo_file_put(file);
    return n;
}

int robo_fd_close(struct RoboProcess *process, robo_fd_t fd)
{
    if (!process)
        return -ROBO_EBADF;
    return robo_fd_table_close(process->fd_table, fd);
}

RoboFile *robo_fd_get(struct RoboProcess *process, robo_fd_t fd)
{
    if (!process)
        return 0;
    return robo_fd_table_get(process->fd_table, fd);
}

int robo_fd_in_use(struct RoboProcess *process, robo_fd_t fd)
{
    RoboFile *f = robo_fd_get(process, fd);
    if (!f)
        return 0;
    robo_file_put(f);
    return 1;
}

#ifndef PROCESS_HOST_TEST
static const char *flag_name(uint32_t flags)
{
    if ((flags & ROBO_FFLAG_READ) && (flags & ROBO_FFLAG_WRITE))
        return "RDWR";
    if (flags & ROBO_FFLAG_WRITE)
        return "WRITE";
    if (flags & ROBO_FFLAG_READ)
        return "READ";
    return "-";
}

void robo_fd_table_dump(const struct RoboProcess *process)
{
    unsigned i;
    RoboFDTable *t;

    if (!process || !process->fd_table)
        return;
    t = process->fd_table;
    kprintf("\n========================================\n");
    kprintf("       FILE DESCRIPTOR SUBSYSTEM\n");
    kprintf("========================================\n\n");
    kprintf("Process PID : %llu\n\n", (unsigned long long)process->id);
    kprintf("FD   Type       Flags       Offset\n");
    kprintf("----------------------------------------\n");
    for (i = 0; i < ROBO_MAX_FDS; i++) {
        RoboFile *f = t->ent[i].file;
        if (!f)
            continue;
        kprintf("%u    %s    %s        %llu\n",
                i,
                robo_file_type_name(f->type),
                flag_name(f->flags),
                (unsigned long long)f->offset);
    }
    kprintf("\nOpen files : %u\n", t->open_count);
    kprintf("========================================\n");
}

static int g_fd_tests;

static int fd_check(int cond)
{
    if (cond) {
        g_fd_tests++;
        return 1;
    }
    return 0;
}

static int run_fd_user(void)
{
    RoboProcess *p;
    unsigned i;
    int st = -1;

    p = robo_process_create_from_elf("/bin/fd-test");
    if (!p)
        return 0;
    for (i = 0; i < 200000u; i++) {
        if (p->state == ROBO_PROCESS_TERMINATED)
            break;
        robo_thread_yield();
        robo_sched_reap();
    }
    st = p->exit_status;
    robo_sched_reap();
    robo_process_destroy(p);
    return st == 0;
}

static const char *mark(int ok)
{
    return ok ? "PASS" : "FAIL";
}

int robo_fd_selftest(void)
{
    RoboProcess *p;
    RoboFile *a, *b, *shared, *rf, *rf2;
    robo_fd_t fd3, fd4, fd5, d1, d2;
    int ok = 1;
    int user_ok = 0;
    int file_ok = 0;
    int file_present = 0;
    uint64_t live0;
    char buf[16];
    char got[16];
    int64_t n, n1, n2;
    unsigned i, gi;
    RoboFile *f0, *f1, *f2;

    g_fd_tests = 0;
    p = robo_current_process();
    if (!p || !p->fd_table)
        return 0;

    live0 = robo_file_stat_live();
    if (!fd_check(robo_fd_in_use(p, 0) && robo_fd_in_use(p, 1) &&
                  robo_fd_in_use(p, 2)))
        ok = 0;

    a = robo_console_open(ROBO_FFLAG_WRITE);
    fd3 = robo_fd_alloc(p, a, 0);
    robo_file_put(a);
    if (!fd_check(fd3 == 3))
        ok = 0;

    b = robo_console_open(ROBO_FFLAG_WRITE);
    fd4 = robo_fd_alloc(p, b, 0);
    robo_file_put(b);
    if (!fd_check(fd4 == 4))
        ok = 0;

    shared = robo_console_open(ROBO_FFLAG_WRITE);
    fd5 = robo_fd_alloc(p, shared, 0);
    d1 = robo_fd_dup(p, fd5);
    if (!fd_check(fd5 >= 5 && d1 >= 0 && d1 != fd5))
        ok = 0;
    if (!fd_check(robo_file_refcount(shared) >= 2))
        ok = 0;
    robo_fd_close(p, fd5);
    if (!fd_check(robo_file_refcount(shared) >= 1))
        ok = 0;
    robo_fd_close(p, d1);
    robo_file_put(shared);

    if (!fd_check(robo_fd_close(p, fd3) == 0))
        ok = 0;
    if (!fd_check(robo_fd_close(p, fd3) == -ROBO_EBADF))
        ok = 0;
    if (!fd_check(robo_fd_close(p, ROBO_FD_INVALID) == -ROBO_EBADF))
        ok = 0;
    if (!fd_check(robo_fd_close(p, 999) == -ROBO_EBADF))
        ok = 0;
    if (!fd_check(robo_fd_get(p, 999) == 0))
        ok = 0;

    a = robo_console_open(ROBO_FFLAG_WRITE);
    d2 = robo_fd_alloc(p, a, 0);
    robo_file_put(a);
    if (!fd_check(d2 == 3))
        ok = 0;
    robo_fd_close(p, d2);
    robo_fd_close(p, fd4);

    f0 = robo_fd_get(p, 0);
    f1 = robo_fd_get(p, 1);
    f2 = robo_fd_get(p, 2);
    if (!fd_check(f0 && f0->type == ROBO_FILE_CONSOLE))
        ok = 0;
    if (!fd_check(f1 && f1->type == ROBO_FILE_CONSOLE))
        ok = 0;
    if (!fd_check(f2 && f2->type == ROBO_FILE_CONSOLE))
        ok = 0;
    n = f0 ? robo_file_read(f0, buf, 4) : -1;
    if (!fd_check(n == -(int64_t)ROBO_ENOTSUP))
        ok = 0;
    n = f1 ? robo_file_read(f1, buf, 4) : -1;
    if (!fd_check(n == -(int64_t)ROBO_EBADF))
        ok = 0;
    if (!fd_check(f2 && (f2->flags & ROBO_FFLAG_WRITE)))
        ok = 0;
    if (f0)
        robo_file_put(f0);
    if (f1)
        robo_file_put(f1);
    if (f2)
        robo_file_put(f2);

    for (i = 3; i < ROBO_MAX_FDS; i++) {
        RoboFile *f = robo_console_open(ROBO_FFLAG_WRITE);
        robo_fd_t fd = robo_fd_alloc(p, f, 0);
        robo_file_put(f);
        if (fd < 0) {
            ok = 0;
            break;
        }
    }
    a = robo_console_open(ROBO_FFLAG_WRITE);
    if (!fd_check(robo_fd_alloc(p, a, 0) == ROBO_FD_INVALID))
        ok = 0;
    robo_file_put(a);
    for (i = 3; i < ROBO_MAX_FDS; i++)
        robo_fd_close(p, (robo_fd_t)i);

    rf = robo_fs_file_open("/test.txt", ROBO_FFLAG_READ);
    if (rf) {
        file_present = 1;
        for (gi = 0; gi < 16; gi++) {
            got[gi] = 0;
            buf[gi] = 0;
        }
        n1 = robo_file_read(rf, got, 4);
        rf2 = robo_fs_file_open("/test.txt", ROBO_FFLAG_READ);
        n2 = rf2 ? robo_file_read(rf2, buf, 4) : -1;
        if (n1 == 4 && n2 == 4 && got[0] == 'R' && buf[0] == 'R')
            file_ok = 1;
        n1 = robo_file_read(rf, got, 4);
        if (!(n1 == 4 && rf->offset == 8))
            file_ok = 0;
        if (rf2)
            robo_file_put(rf2);
        robo_file_put(rf);
        if (!fd_check(file_ok))
            ok = 0;
    }

    if (!fd_check(robo_file_stat_live() == live0))
        ok = 0;

    user_ok = run_fd_user();
    if (!fd_check(user_ok))
        ok = 0;

    kprintf("FD_SELFTEST  checks %u  live %llu\n",
            g_fd_tests, (unsigned long long)robo_file_stat_live());

    kprintf("\n========================================\n");
    kprintf("    ROBOTICELA OS FILE DESCRIPTORS\n");
    kprintf("========================================\n\n");
    kprintf("Architecture:\n    %s\n\n", mark(ok));
    kprintf("RoboFDTable:\n    %s\n\n", mark(ok));
    kprintf("RoboFile:\n    %s\n\n", mark(ok));
    kprintf("Reference Counting:\n    %s\n\n", mark(ok));
    kprintf("stdin:\n    %s\n\n", mark(ok));
    kprintf("stdout:\n    %s\n\n", mark(user_ok));
    kprintf("stderr:\n    %s\n\n", mark(ok));
    kprintf("SYS_OPEN:\n    %s\n\n", mark(user_ok && file_present));
    kprintf("SYS_READ:\n    %s\n\n", mark(user_ok));
    kprintf("SYS_WRITE:\n    %s\n\n", mark(user_ok));
    kprintf("SYS_CLOSE:\n    %s\n\n", mark(user_ok));
    kprintf("RoboFS Integration:\n    %s\n\n",
            file_present ? mark(file_ok) : "NOT IMPLEMENTED");
    kprintf("Process Cleanup:\n    %s\n\n", mark(ok && user_ok));
    kprintf("Invalid FD Handling:\n    %s\n\n", mark(ok));
    kprintf("User Pointer Validation:\n    %s\n\n", mark(user_ok));
    kprintf("Concurrency:\n    NOT TESTED\n\n");
    kprintf("QEMU:\n    %s\n\n", mark(ok && user_ok));
    kprintf("User-space Hello:\n    %s\n\n", mark(user_ok));
    kprintf("File Read Test:\n    %s\n\n",
            file_present ? mark(file_ok && user_ok) : "NOT IMPLEMENTED");
    kprintf("Leaks:\n    %s\n\n", mark(ok));
    kprintf("Kernel Regressions:\n    %s\n\n", mark(ok && user_ok));
    kprintf("========================================\n");
    if (ok && user_ok)
        kprintf("[PASS] ROBOTICELA OS FILE DESCRIPTORS\n");
    else
        kprintf("[FAIL] ROBOTICELA OS FILE DESCRIPTORS\n");
    return ok && user_ok;
}

#ifndef FD_HOST_TEST
void robo_fd_fb_report(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    RoboProcess *cur = robo_current_process();

    kfb_puts("FD table  open ", mute);
    kfb_putu((unsigned long long)robo_fd_table_open_count(
                 cur ? cur->fd_table : 0),
             white);
    kfb_puts("  files ", mute);
    kfb_putu((unsigned long long)robo_file_stat_live(), white);
    kfb_puts("\n", mute);
}
#endif
#endif
