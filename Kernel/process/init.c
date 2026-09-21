#include "process/init.h"
#include "process/process.h"
#include "exec/exec.h"
#include "exec/exec_error.h"
#include "fs/vfs/vfs.h"
#include "fs/vfs/vfs_types.h"
#include "fs/fd.h"
#include "kio.h"

static void HaltForever(void)
{
    for (;;)
        __asm__ volatile ("cli; hlt");
}

static void PanicStartup(const char *path, const char *error,
                          const char *elf, const char *process,
                          const char *address)
{
    kprintf("========================================\n");
    kprintf("          KERNEL PANIC\n");
    kprintf("========================================\n\n");
    kprintf("INIT STARTUP FAILURE\n\n");
    kprintf("Unable to start /System/init\n\n");
    kprintf("Path      : %s\n", path ? path : RoboInitPath);
    kprintf("Error     : %s\n", error ? error : "unknown");
    kprintf("ELF       : %s\n", elf ? elf : "-");
    kprintf("Process   : %s\n", process ? process : "-");
    kprintf("Address   : %s\n", address ? address : "-");
    kprintf("\n========================================\n");
    HaltForever();
}

void InitStart(const RoboBootInfo *info)
{
    static int started;
    RoboStat st;
    RoboProcess *p;
    RoboExecArgv av;
    unsigned i;
    int r;
    int fdOk;
    int tests;

    if (started)
        PanicStartup(RoboInitPath, "init started more than once",
                      "-", "-", "-");
    started = 1;
    tests = KbootSelftest(info);

    kprintf("[ OK ] RoboFS mounted\n");

    r = VfsStatPath(RoboInitPath, &st);
    if (r != 0 || st.size == 0 || st.type == RoboVnodeDirectory)
        PanicStartup(RoboInitPath, "filesystem error",
                      "-", "-", "VFS lookup failed");
    kprintf("[ OK ] /System/init found\n");

    av.argc = 1;
    av.args[0][0] = 'i';
    av.args[0][1] = 'n';
    av.args[0][2] = 'i';
    av.args[0][3] = 't';
    av.args[0][4] = 0;
    if (tests) {
        const char *t = "selftest";
        av.argc = 2;
        for (i = 0; t[i] && i < RoboExecArgMax; i++)
            av.args[1][i] = t[i];
        av.args[1][i] = 0;
        kprintf("Init  selftest\n");
    } else {
        kprintf("Init  production\n");
    }

    p = ProcessCreateInitElf(RoboInitPath, &av);
    if (!p) {
        PanicStartup(RoboInitPath,
                      "process creation error",
                      ExecStatusName(ExecLastStatus()),
                      "create failed",
                      "memory allocation or ELF load");
    }
    kprintf("[ OK ] ELF loaded\n");

    if (p->id != RoboProcessIdInit)
        PanicStartup(RoboInitPath, "process creation error",
                      "ok", "PID is not 1", "-");
    if (!p->addressSpace || !p->ownsAspace)
        PanicStartup(RoboInitPath, "memory allocation error",
                      "ok", "no user address space", "-");
    if (!p->userEntry || !p->userStackTop)
        PanicStartup(RoboInitPath, "ELF error",
                      "invalid entry point or stack", "created", "-");

    fdOk = ProcessFdInUse(p, 0) &&
            ProcessFdInUse(p, 1) &&
            ProcessFdInUse(p, 2);
    if (!fdOk)
        PanicStartup(RoboInitPath, "process creation error",
                      "ok", "stdio fds missing", "-");

    kprintf("[ OK ] Init process created\n");
    kprintf("[ OK ] Entering user space\n");
    if (KbootDebug(info)) {
        KfbPuts("Init  PID 1  ", 0xA1A1A6u);
        KfbPuts(RoboInitPath, 0xFFFFFFFFu);
        KfbPuts("\n", 0xA1A1A6u);
    }
}
