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

#define ROBO_EXEC_MAX_BYTES (512u * 1024u)

static int g_exec_quiet;
static robo_exec_status_t g_last_exec;

void robo_exec_set_quiet(int quiet)
{
    g_exec_quiet = quiet ? 1 : 0;
    robo_elf_set_quiet(g_exec_quiet);
}

robo_exec_status_t robo_exec_last_status(void)
{
    return g_last_exec;
}

static const char *name_from_path(const char *path)
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

int robo_exec_image_present(const char *path)
{
    RoboStat st;
    const void *data;
    uint32_t size;

    if (!path)
        return 0;
    if (robo_vfs_stat_path(path, &st) == 0 && st.size > 0 &&
        st.type != ROBO_VNODE_DIRECTORY)
        return 1;
    if (robo_fs_open(path, &data, &size) && data && size)
        return 1;
    return 0;
}

static int load_file_bytes(const char *path, const void **data, size_t *size,
                           int *owned)
{
    RoboStat st;
    RoboFile *file = 0;
    void *buf;
    int64_t n;
    uint32_t init_size;
    const void *init_data;
    int r;

    *data = 0;
    *size = 0;
    *owned = 0;

    r = robo_vfs_stat_path(path, &st);
    if (r == 0) {
        if (st.type == ROBO_VNODE_DIRECTORY || st.size == 0)
            return 0;
        if (st.size > ROBO_EXEC_MAX_BYTES)
            return 0;
        buf = kmalloc((size_t)st.size);
        if (!buf)
            return 0;
        r = robo_vfs_open(path, ROBO_O_RDONLY, 0, &file);
        if (r != 0) {
            kfree(buf);
            return 0;
        }
        n = robo_vfs_read(file, buf, (size_t)st.size);
        robo_file_put(file);
        if (n < 0 || (uint64_t)n != st.size) {
            kfree(buf);
            return 0;
        }
        *data = buf;
        *size = (size_t)st.size;
        *owned = 1;
        return 1;
    }

    if (robo_fs_open(path, &init_data, &init_size) && init_data && init_size) {
        *data = init_data;
        *size = (size_t)init_size;
        *owned = 0;
        return 1;
    }
    return 0;
}

robo_exec_status_t robo_exec_ex(RoboProcess *process, const char *path,
                                const RoboExecArgv *argv)
{
    const void *data;
    size_t size;
    int owned = 0;
    RoboExecImage img;
    robo_exec_status_t st;

    g_last_exec = ROBO_ELF_FILE_FAILURE;
    if (!process || !path || !process->address_space)
        return ROBO_ELF_FILE_FAILURE;
    if (!load_file_bytes(path, &data, &size, &owned) || !data || !size) {
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
    st = robo_elf_load_image(process->address_space, data, size, &img,
                             name_from_path(path), argv);
    if (owned)
        kfree((void *)data);
    if (st != ROBO_EXEC_OK) {
        g_last_exec = st;
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
    g_last_exec = ROBO_EXEC_OK;
    if (!g_exec_quiet) {
        kprintf("ELF LOAD COMPLETE\n\n");
        kprintf("PID         : %llu\n", (unsigned long long)process->id);
        kprintf("Entry       : 0x%llx\n", (unsigned long long)img.entry);
        kprintf("Stack       : 0x%llx\n", (unsigned long long)img.stack_top);
    }
    return ROBO_EXEC_OK;
}

robo_exec_status_t robo_exec(RoboProcess *process, const char *path)
{
    return robo_exec_ex(process, path, 0);
}

static RoboProcess *create_from_elf_ex(const char *path, int as_init,
                                       const RoboExecArgv *argv)
{
    RoboProcess *p;
    RoboThread *t;
    const char *name;

    if (!path)
        return 0;
    name = as_init ? "init" : name_from_path(path);
    p = as_init ? robo_process_create_init() : robo_process_create_named(name);
    if (!p)
        return 0;
    if (robo_exec_ex(p, path, argv) != ROBO_EXEC_OK) {
        robo_process_destroy(p);
        return 0;
    }
    t = robo_thread_create_user(p, name, p->user_entry, p->user_stack_top, 0);
    if (!t) {
        robo_process_destroy(p);
        return 0;
    }
    if (as_init)
        robo_process_set_init(p);
    return p;
}

static RoboProcess *create_from_elf(const char *path, int as_init)
{
    return create_from_elf_ex(path, as_init, 0);
}

RoboProcess *robo_process_create_from_elf(const char *path)
{
    return create_from_elf(path, 0);
}

RoboProcess *robo_process_create_from_elf_argv(const char *path,
                                               const RoboExecArgv *argv)
{
    return create_from_elf_ex(path, 0, argv);
}

RoboProcess *robo_process_create_init_elf(const char *path)
{
    return create_from_elf(path, 1);
}
