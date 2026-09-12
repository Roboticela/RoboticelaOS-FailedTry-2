#include "syscall/syscall_table.h"
#include "syscall/syscall_impl.h"
#include "syscall/syscall_numbers.h"

static RoboSyscallHandler g_table[ROBO_SYS_TABLE_SIZE];

void robo_syscall_table_init(void)
{
    unsigned i;

    for (i = 0; i < ROBO_SYS_TABLE_SIZE; i++)
        g_table[i] = robo_sys_enosys;

    g_table[ROBO_SYS_ABI_INFO] = robo_sys_abi_info;
    g_table[ROBO_SYS_EXIT] = robo_sys_exit;
    g_table[ROBO_SYS_READ] = robo_sys_read;
    g_table[ROBO_SYS_WRITE] = robo_sys_write;
    g_table[ROBO_SYS_OPEN] = robo_sys_open;
    g_table[ROBO_SYS_CLOSE] = robo_sys_close;
    g_table[ROBO_SYS_MMAP] = robo_sys_mmap;
    g_table[ROBO_SYS_MUNMAP] = robo_sys_munmap;
    g_table[ROBO_SYS_PROCESS_CREATE] = robo_sys_process_create;
    g_table[ROBO_SYS_THREAD_CREATE] = robo_sys_thread_create;
    g_table[ROBO_SYS_SLEEP] = robo_sys_sleep;
    g_table[ROBO_SYS_TIME] = robo_sys_time;
    g_table[ROBO_SYS_IPC] = robo_sys_ipc;
    g_table[ROBO_SYS_SEEK] = robo_sys_seek;
    g_table[ROBO_SYS_STAT] = robo_sys_stat;
    g_table[ROBO_SYS_MKDIR] = robo_sys_mkdir;
    g_table[ROBO_SYS_RMDIR] = robo_sys_rmdir;
    g_table[ROBO_SYS_UNLINK] = robo_sys_unlink;
    g_table[ROBO_SYS_RENAME] = robo_sys_rename;
    g_table[ROBO_SYS_WAIT] = robo_sys_wait;
    g_table[ROBO_SYS_GETPID] = robo_sys_getpid;
    g_table[ROBO_SYS_DRIVER_QUERY] = robo_sys_driver_query;
}

RoboSyscallHandler robo_syscall_lookup(uint64_t number)
{
    if (number >= ROBO_SYS_TABLE_SIZE)
        return 0;
    return g_table[number];
}

int robo_syscall_table_complete(void)
{
    unsigned i;
    for (i = 0; i < ROBO_SYS_TABLE_SIZE; i++) {
        if (!g_table[i])
            return 0;
    }
    return 1;
}

const char *robo_syscall_name(uint64_t number)
{
    switch (number) {
    case ROBO_SYS_ABI_INFO: return "SYS_ABI_INFO";
    case ROBO_SYS_EXIT: return "SYS_EXIT";
    case ROBO_SYS_READ: return "SYS_READ";
    case ROBO_SYS_WRITE: return "SYS_WRITE";
    case ROBO_SYS_OPEN: return "SYS_OPEN";
    case ROBO_SYS_CLOSE: return "SYS_CLOSE";
    case ROBO_SYS_MMAP: return "SYS_MMAP";
    case ROBO_SYS_MUNMAP: return "SYS_MUNMAP";
    case ROBO_SYS_PROCESS_CREATE: return "SYS_PROCESS_CREATE";
    case ROBO_SYS_THREAD_CREATE: return "SYS_THREAD_CREATE";
    case ROBO_SYS_SLEEP: return "SYS_SLEEP";
    case ROBO_SYS_TIME: return "SYS_TIME";
    case ROBO_SYS_IPC: return "SYS_IPC";
    case ROBO_SYS_SEEK: return "SYS_SEEK";
    case ROBO_SYS_STAT: return "SYS_STAT";
    case ROBO_SYS_MKDIR: return "SYS_MKDIR";
    case ROBO_SYS_RMDIR: return "SYS_RMDIR";
    case ROBO_SYS_UNLINK: return "SYS_UNLINK";
    case ROBO_SYS_RENAME: return "SYS_RENAME";
    case ROBO_SYS_WAIT: return "SYS_WAIT";
    case ROBO_SYS_GETPID: return "SYS_GETPID";
    case ROBO_SYS_DRIVER_QUERY: return "SYS_DRIVER_QUERY";
    default: return "SYS_UNKNOWN";
    }
}
