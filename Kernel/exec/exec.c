#include "exec/exec.h"
#include "exec/elf/elf_loader.h"
#include "fs/robofs.h"
#include "fs/vfs/vfs.h"
#include "fs/vfs/vfs_types.h"
#include "fs/file.h"
#include "syscall/syscall_numbers.h"
#include "syscall/syscall_errors.h"
#include "mm/heap/heap.h"
#include "kio.h"

#define RoboExecMaxBytes (512u * 1024u)

static int gExecQuiet = 1;
static ExecStatus gLastExec;

void ExecSetQuiet(int quiet)
{
    gExecQuiet = quiet ? 1 : 0;
    ElfSetQuiet(gExecQuiet);
}

ExecStatus ExecLastStatus(void)
{
    return gLastExec;
}

static const char *NameFromPath(const char *path)
{
    const char *s;
    const char *last;

    if (!path || !path[0])
        return "user";
    last = path;
    for (s = path; *s; s++) {
        if (*s == '/' && s[1])
            last = s + 1;
    }
    return last;
}

int ExecImagePresent(const char *path)
{
    RoboStat st;
    const void *data;
    uint32_t size;

    if (!path)
        return 0;
    if (VfsStatPath(path, &st) == 0 && st.size > 0 &&
        st.type != RoboVnodeDirectory)
        return 1;
    if (FsOpen(path, &data, &size) && data && size)
        return 1;
    return 0;
}

static int LoadFileBytes(const char *path, const void **data, size_t *size,
                           int *owned)
{
    RoboStat st;
    RoboFile *file = 0;
    void *buf;
    int64_t n;
    uint32_t initSize;
    const void *initData;
    int r;

    *data = 0;
    *size = 0;
    *owned = 0;

    r = VfsStatPath(path, &st);
    if (r == 0) {
        if (st.type == RoboVnodeDirectory || st.size == 0)
            return 0;
        if (st.size > RoboExecMaxBytes)
            return 0;
        buf = kmalloc((size_t)st.size);
        if (!buf)
            return 0;
        r = VfsOpen(path, RoboORdonly, 0, &file);
        if (r != 0) {
            kfree(buf);
            return 0;
        }
        n = VfsRead(file, buf, (size_t)st.size);
        FilePut(file);
        if (n < 0 || (uint64_t)n != st.size) {
            kfree(buf);
            return 0;
        }
        *data = buf;
        *size = (size_t)st.size;
        *owned = 1;
        return 1;
    }

    if (FsOpen(path, &initData, &initSize) && initData && initSize) {
        *data = initData;
        *size = (size_t)initSize;
        *owned = 0;
        return 1;
    }
    return 0;
}

ExecStatus ExecEx(RoboProcess *process, const char *path,
                                const RoboExecArgv *argv)
{
    const void *data;
    size_t size;
    int owned = 0;
    RoboExecImage img;
    ExecStatus st;

    gLastExec = RoboElfFileFailure;
    if (!process || !path || !process->addressSpace)
        return RoboElfFileFailure;
    if (!LoadFileBytes(path, &data, &size, &owned) || !data || !size) {
        if (!gExecQuiet) {
            kprintf("ELF LOAD FAILED\n\n");
            kprintf("Path   : %s\n", path);
            kprintf("Reason : File read failed\n");
            kprintf("PID    : %llu\n", (unsigned long long)process->id);
        }
        return RoboElfFileFailure;
    }

    if (!gExecQuiet) {
        kprintf("ELF LOAD\n\n");
        kprintf("Path        : %s\n", path);
    }
    st = ElfLoadImage(process->addressSpace, data, size, &img,
                             NameFromPath(path), argv);
    if (owned)
        kfree((void *)data);
    if (st != RoboExecOk) {
        gLastExec = st;
        if (!gExecQuiet) {
            kprintf("ELF LOAD FAILED\n\n");
            kprintf("Path   : %s\n", path);
            kprintf("Reason : %s\n", ExecStatusName(st));
            kprintf("PID    : %llu\n", (unsigned long long)process->id);
        }
        return st;
    }
    process->userEntry = img.entry;
    process->userStackTop = img.StackTop;
    process->userStackBase = img.stackBase;
    gLastExec = RoboExecOk;
    if (!gExecQuiet) {
        kprintf("ELF LOAD COMPLETE\n\n");
        kprintf("PID         : %llu\n", (unsigned long long)process->id);
        kprintf("Entry       : 0x%llx\n", (unsigned long long)img.entry);
        kprintf("Stack       : 0x%llx\n", (unsigned long long)img.StackTop);
    }
    return RoboExecOk;
}

ExecStatus Exec(RoboProcess *process, const char *path)
{
    return ExecEx(process, path, 0);
}

static RoboProcess *CreateFromElfEx(const char *path, int asInit,
                                       const RoboExecArgv *argv)
{
    RoboProcess *p;
    RoboThread *t;
    const char *name;

    if (!path)
        return 0;
    name = asInit ? "init" : NameFromPath(path);
    p = asInit ? ProcessCreateInit() : ProcessCreateNamed(name);
    if (!p)
        return 0;
    if (ExecEx(p, path, argv) != RoboExecOk) {
        ProcessDestroy(p);
        return 0;
    }
    t = ThreadCreateUser(p, name, p->userEntry, p->userStackTop, 0);
    if (!t) {
        ProcessDestroy(p);
        return 0;
    }
    if (asInit)
        ProcessSetInit(p);
    return p;
}

static RoboProcess *CreateFromElf(const char *path, int asInit)
{
    return CreateFromElfEx(path, asInit, 0);
}

RoboProcess *ProcessCreateFromElf(const char *path)
{
    return CreateFromElf(path, 0);
}

RoboProcess *ProcessCreateFromElfArgv(const char *path,
                                               const RoboExecArgv *argv)
{
    return CreateFromElfEx(path, 0, argv);
}

RoboProcess *ProcessCreateInitElf(const char *path,
                                          const RoboExecArgv *argv)
{
    return CreateFromElfEx(path, 1, argv);
}
