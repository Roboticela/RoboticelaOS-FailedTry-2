#include "exec/exec.h"
#include "exec/elf/elf_loader.h"
#include "fs/robofs.h"
#include "kio.h"

static int g_exec_quiet;

void robo_exec_set_quiet(int quiet)
{
    g_exec_quiet = quiet ? 1 : 0;
    robo_elf_set_quiet(g_exec_quiet);
}

robo_exec_status_t robo_exec(RoboProcess *process, const char *path)
{
    const void *data;
    uint32_t size;
    RoboExecImage img;
    robo_exec_status_t st;

    if (!process || !path || !process->address_space)
        return ROBO_ELF_FILE_FAILURE;
    if (!robo_fs_open(path, &data, &size) || !data || !size) {
        if (!g_exec_quiet) {
            kprintf("ELF LOAD FAILED\n\n");
            kprintf("Path   : %s\n", path);
            kprintf("Reason : File read failed\n");
            kprintf("PID    : %llu\n", (unsigned long long)process->id);
        }
        return ROBO_ELF_FILE_FAILURE;
    }

    if (!g_exec_quiet) {
        kprintf("ELF LOAD\n\n");
        kprintf("Path        : %s\n", path);
    }
    st = robo_elf_load_image(process->address_space, data, (size_t)size, &img);
    if (st != ROBO_EXEC_OK) {
        if (!g_exec_quiet) {
            kprintf("ELF LOAD FAILED\n\n");
            kprintf("Path   : %s\n", path);
            kprintf("Reason : %s\n", robo_exec_status_name(st));
            kprintf("PID    : %llu\n", (unsigned long long)process->id);
        }
        return st;
    }
    process->user_entry = img.entry;
    process->user_stack_top = img.stack_top;
    process->user_stack_base = img.stack_base;
    if (!g_exec_quiet) {
        kprintf("ELF LOAD COMPLETE\n\n");
        kprintf("PID         : %llu\n", (unsigned long long)process->id);
        kprintf("Entry       : 0x%llx\n", (unsigned long long)img.entry);
        kprintf("Stack       : 0x%llx\n", (unsigned long long)img.stack_top);
    }
    return ROBO_EXEC_OK;
}

RoboProcess *robo_process_create_from_elf(const char *path)
{
    RoboProcess *p;
    RoboThread *t;
    const char *name = "user";

    if (!path)
        return 0;
    if (path[0] == '/' && path[1] == 'b' && path[2] == 'i' && path[3] == 'n' &&
        path[4] == '/')
        name = path + 5;
    p = robo_process_create_named(name);
    if (!p)
        return 0;
    if (robo_exec(p, path) != ROBO_EXEC_OK) {
        robo_process_destroy(p);
        return 0;
    }
    t = robo_thread_create_user(p, name, p->user_entry, p->user_stack_top);
    if (!t) {
        robo_process_destroy(p);
        return 0;
    }
    return p;
}
