#include "fs/fd_table.h"
#include "fs/backends/console.h"
#include "syscall/syscall_errors.h"

#ifdef PROCESS_HOST_TEST
#include <stdlib.h>
#define kmalloc malloc
#define kfree free
static uint64_t FdLock(void) { return 0; }
static void FdUnlock(uint64_t flags) { (void)flags; }
#else
#include "mm/heap/heap.h"
static uint64_t FdLock(void)
{
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
static void FdUnlock(uint64_t flags)
{
    __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
}
#endif

static void TblZero(void *dst, unsigned n)
{
    unsigned char *p = (unsigned char *)dst;
    while (n--)
        *p++ = 0;
}

RoboFDTable *FdTableCreate(void)
{
    RoboFDTable *table;

    table = (RoboFDTable *)kmalloc(sizeof(*table));
    if (!table)
        return 0;
    TblZero(table, (unsigned)sizeof(*table));
    if (ConsoleInstallStdio(table) != 0) {
        FdTableDestroy(table);
        return 0;
    }
    return table;
}

void FdTableDestroy(RoboFDTable *table)
{
    unsigned i;
    uint64_t flags;

    if (!table)
        return;
    flags = FdLock();
    for (i = 0; i < RoboMaxFds; i++) {
        RoboFile *f = table->ent[i].file;
        table->ent[i].file = 0;
        table->ent[i].flags = 0;
        if (f) {
            FdUnlock(flags);
            FilePut(f);
            flags = FdLock();
        }
    }
    table->openCount = 0;
    FdUnlock(flags);
    kfree(table);
}

int FdTableClone(const RoboFDTable *source, RoboFDTable **destination)
{
    RoboFDTable *out;
    unsigned i;

    if (!source || !destination)
        return -RoboEinval;
    out = (RoboFDTable *)kmalloc(sizeof(*out));
    if (!out)
        return -RoboEnomem;
    TblZero(out, (unsigned)sizeof(*out));
    for (i = 0; i < RoboMaxFds; i++) {
        if (!source->ent[i].file)
            continue;
        if (source->ent[i].flags & RoboFdentCloexec)
            continue;
        FileGet(source->ent[i].file);
        out->ent[i].file = source->ent[i].file;
        out->ent[i].flags = source->ent[i].flags;
        out->openCount++;
    }
    *destination = out;
    return 0;
}

unsigned FdTableOpenCount(const RoboFDTable *table)
{
    return table ? table->openCount : 0;
}

Fd FdTableAlloc(RoboFDTable *table, RoboFile *file, uint32_t flags)
{
    unsigned i;
    uint64_t lockf;
    Fd fd = RoboFdInvalid;

    if (!table || !file)
        return RoboFdInvalid;
    lockf = FdLock();
    for (i = 0; i < RoboMaxFds; i++) {
        if (!table->ent[i].file) {
            FileGet(file);
            table->ent[i].file = file;
            table->ent[i].flags = flags;
            table->openCount++;
            fd = (Fd)i;
            break;
        }
    }
    FdUnlock(lockf);
    return fd;
}

int FdTableClose(RoboFDTable *table, Fd fd)
{
    RoboFile *file;
    uint64_t lockf;

    if (!table || fd < 0 || (unsigned)fd >= RoboMaxFds)
        return -RoboEbadf;
    lockf = FdLock();
    file = table->ent[fd].file;
    if (!file) {
        FdUnlock(lockf);
        return -RoboEbadf;
    }
    table->ent[fd].file = 0;
    table->ent[fd].flags = 0;
    if (table->openCount)
        table->openCount--;
    FdUnlock(lockf);
    FilePut(file);
    return 0;
}

RoboFile *FdTableGet(RoboFDTable *table, Fd fd)
{
    RoboFile *file;
    uint64_t lockf;

    if (!table || fd < 0 || (unsigned)fd >= RoboMaxFds)
        return 0;
    lockf = FdLock();
    file = table->ent[fd].file;
    if (file)
        FileGet(file);
    FdUnlock(lockf);
    return file;
}
