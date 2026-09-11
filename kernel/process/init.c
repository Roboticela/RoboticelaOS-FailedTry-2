#include "process/init.h"
#include "process/process.h"
#include "exec/exec.h"
#include "exec/exec_error.h"
#include "fs/vfs/vfs.h"
#include "fs/vfs/vfs_types.h"
#include "fs/fd.h"
#include "kio.h"

static void halt_forever(void)
{
    for (;;)
        __asm__ volatile ("cli; hlt");
}

static void panic_startup(const char *path, const char *error,
                          const char *elf, const char *process,
                          const char *address)
{
    kprintf("========================================\n");
    kprintf("          KERNEL PANIC\n");
    kprintf("========================================\n\n");
    kprintf("INIT STARTUP FAILURE\n\n");
    kprintf("Unable to start /System/init\n\n");
    kprintf("Path      : %s\n", path ? path : ROBO_INIT_PATH);
    kprintf("Error     : %s\n", error ? error : "unknown");
    kprintf("ELF       : %s\n", elf ? elf : "-");
    kprintf("Process   : %s\n", process ? process : "-");
    kprintf("Address   : %s\n", address ? address : "-");
    kprintf("\n========================================\n");
    halt_forever();
}

void robo_init_start(const RoboBootInfo *info)
{
    static int started;
    RoboStat st;
    RoboProcess *p;
    int r;
    int fd_ok;

    if (started)
        panic_startup(ROBO_INIT_PATH, "init started more than once",
                      "-", "-", "-");
    started = 1;

    kprintf("[ OK ] RoboFS mounted\n");

    r = robo_vfs_stat_path(ROBO_INIT_PATH, &st);
    if (r != 0 || st.size == 0 || st.type == ROBO_VNODE_DIRECTORY)
        panic_startup(ROBO_INIT_PATH, "filesystem error",
                      "-", "-", "VFS lookup failed");
    kprintf("[ OK ] /System/init found\n");

    p = robo_process_create_init_elf(ROBO_INIT_PATH);
    if (!p) {
        panic_startup(ROBO_INIT_PATH,
                      "process creation error",
                      robo_exec_status_name(robo_exec_last_status()),
                      "create failed",
                      "memory allocation or ELF load");
    }
    kprintf("[ OK ] ELF loaded\n");

    if (p->id != ROBO_PROCESS_ID_INIT)
        panic_startup(ROBO_INIT_PATH, "process creation error",
                      "ok", "PID is not 1", "-");
    if (!p->address_space || !p->owns_aspace)
        panic_startup(ROBO_INIT_PATH, "memory allocation error",
                      "ok", "no user address space", "-");
    if (!p->user_entry || !p->user_stack_top)
        panic_startup(ROBO_INIT_PATH, "ELF error",
                      "invalid entry point or stack", "created", "-");

    fd_ok = robo_process_fd_in_use(p, 0) &&
            robo_process_fd_in_use(p, 1) &&
            robo_process_fd_in_use(p, 2);
    if (!fd_ok)
        panic_startup(ROBO_INIT_PATH, "process creation error",
                      "ok", "stdio fds missing", "-");

    kprintf("[ OK ] Init process created\n");
    kprintf("[ OK ] Entering user space\n");
    if (kboot_debug(info)) {
        kfb_puts("Init  PID 1  ", 0xA1A1A6u);
        kfb_puts(ROBO_INIT_PATH, 0xFFFFFFFFu);
        kfb_puts("\n", 0xA1A1A6u);
    }
}
