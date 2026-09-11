#include "fs/fd_table.h"
#include "fs/backends/console.h"
#include "syscall/syscall_errors.h"

#ifdef PROCESS_HOST_TEST
#include <stdlib.h>
#define kmalloc malloc
#define kfree free
static uint64_t fd_lock(void) { return 0; }
static void fd_unlock(uint64_t flags) { (void)flags; }
#else
#include "mm/heap/heap.h"
static uint64_t fd_lock(void)
{
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
static void fd_unlock(uint64_t flags)
{
    __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
}
#endif

static void tbl_zero(void *dst, unsigned n)
{
    unsigned char *p = (unsigned char *)dst;
    while (n--)
        *p++ = 0;
}

RoboFDTable *robo_fd_table_create(void)
{
    RoboFDTable *table;

    table = (RoboFDTable *)kmalloc(sizeof(*table));
    if (!table)
        return 0;
    tbl_zero(table, (unsigned)sizeof(*table));
    if (robo_console_install_stdio(table) != 0) {
        robo_fd_table_destroy(table);
        return 0;
    }
    return table;
}

void robo_fd_table_destroy(RoboFDTable *table)
{
    unsigned i;
    uint64_t flags;

    if (!table)
        return;
    flags = fd_lock();
    for (i = 0; i < ROBO_MAX_FDS; i++) {
        RoboFile *f = table->ent[i].file;
        table->ent[i].file = 0;
        table->ent[i].flags = 0;
        if (f) {
            fd_unlock(flags);
            robo_file_put(f);
            flags = fd_lock();
        }
    }
    table->open_count = 0;
    fd_unlock(flags);
    kfree(table);
}

int robo_fd_table_clone(const RoboFDTable *source, RoboFDTable **destination)
{
    RoboFDTable *out;
    unsigned i;

    if (!source || !destination)
        return -ROBO_EINVAL;
    out = (RoboFDTable *)kmalloc(sizeof(*out));
    if (!out)
        return -ROBO_ENOMEM;
    tbl_zero(out, (unsigned)sizeof(*out));
    for (i = 0; i < ROBO_MAX_FDS; i++) {
        if (!source->ent[i].file)
            continue;
        if (source->ent[i].flags & ROBO_FDENT_CLOEXEC)
            continue;
        robo_file_get(source->ent[i].file);
        out->ent[i].file = source->ent[i].file;
        out->ent[i].flags = source->ent[i].flags;
        out->open_count++;
    }
    *destination = out;
    return 0;
}

unsigned robo_fd_table_open_count(const RoboFDTable *table)
{
    return table ? table->open_count : 0;
}

robo_fd_t robo_fd_table_alloc(RoboFDTable *table, RoboFile *file, uint32_t flags)
{
    unsigned i;
    uint64_t lockf;
    robo_fd_t fd = ROBO_FD_INVALID;

    if (!table || !file)
        return ROBO_FD_INVALID;
    lockf = fd_lock();
    for (i = 0; i < ROBO_MAX_FDS; i++) {
        if (!table->ent[i].file) {
            robo_file_get(file);
            table->ent[i].file = file;
            table->ent[i].flags = flags;
            table->open_count++;
            fd = (robo_fd_t)i;
            break;
        }
    }
    fd_unlock(lockf);
    return fd;
}

int robo_fd_table_close(RoboFDTable *table, robo_fd_t fd)
{
    RoboFile *file;
    uint64_t lockf;

    if (!table || fd < 0 || (unsigned)fd >= ROBO_MAX_FDS)
        return -ROBO_EBADF;
    lockf = fd_lock();
    file = table->ent[fd].file;
    if (!file) {
        fd_unlock(lockf);
        return -ROBO_EBADF;
    }
    table->ent[fd].file = 0;
    table->ent[fd].flags = 0;
    if (table->open_count)
        table->open_count--;
    fd_unlock(lockf);
    robo_file_put(file);
    return 0;
}

RoboFile *robo_fd_table_get(RoboFDTable *table, robo_fd_t fd)
{
    RoboFile *file;
    uint64_t lockf;

    if (!table || fd < 0 || (unsigned)fd >= ROBO_MAX_FDS)
        return 0;
    lockf = fd_lock();
    file = table->ent[fd].file;
    if (file)
        robo_file_get(file);
    fd_unlock(lockf);
    return file;
}
