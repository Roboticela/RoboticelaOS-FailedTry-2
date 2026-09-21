#include "syscall/syscall_table.h"
#include "syscall/syscall_impl.h"
#include "syscall/syscall_numbers.h"

static RoboSyscallHandler gTable[RoboSysTableSize];

void SyscallTableInit(void)
{
    unsigned i;

    for (i = 0; i < RoboSysTableSize; i++)
        gTable[i] = SysEnosys;

    gTable[RoboSysAbiInfo] = SysAbiInfo;
    gTable[RoboSysExit] = SysExit;
    gTable[RoboSysRead] = SysRead;
    gTable[RoboSysWrite] = SysWrite;
    gTable[RoboSysOpen] = SysOpen;
    gTable[RoboSysClose] = SysClose;
    gTable[RoboSysMmap] = SysMmap;
    gTable[RoboSysMunmap] = SysMunmap;
    gTable[RoboSysProcessCreate] = SysProcessCreate;
    gTable[RoboSysThreadCreate] = SysThreadCreate;
    gTable[RoboSysSleep] = SysSleep;
    gTable[RoboSysTime] = RoboSysTime;
    gTable[RoboSysIpc] = SysIpc;
    gTable[RoboSysSeek] = SysSeek;
    gTable[RoboSysStat] = SysStat;
    gTable[RoboSysMkdir] = SysMkdir;
    gTable[RoboSysRmdir] = SysRmdir;
    gTable[RoboSysUnlink] = SysUnlink;
    gTable[RoboSysRename] = SysRename;
    gTable[RoboSysWait] = SysWait;
    gTable[RoboSysGetpid] = SysGetpid;
    gTable[RoboSysDriverQuery] = SysDriverQuery;
    gTable[RoboSysDisplay] = SysDisplay;
    gTable[RoboSysSocket] = SysSocket;
    gTable[RoboSysThread] = SysThread;
}

RoboSyscallHandler SyscallLookup(uint64_t number)
{
    if (number >= RoboSysTableSize)
        return 0;
    return gTable[number];
}

int SyscallTableComplete(void)
{
    unsigned i;
    for (i = 0; i < RoboSysTableSize; i++) {
        if (!gTable[i])
            return 0;
    }
    return 1;
}

const char *SyscallName(uint64_t number)
{
    switch (number) {
    case RoboSysAbiInfo: return "SYS_ABI_INFO";
    case RoboSysExit: return "SYS_EXIT";
    case RoboSysRead: return "SYS_READ";
    case RoboSysWrite: return "SYS_WRITE";
    case RoboSysOpen: return "SYS_OPEN";
    case RoboSysClose: return "SYS_CLOSE";
    case RoboSysMmap: return "SYS_MMAP";
    case RoboSysMunmap: return "SYS_MUNMAP";
    case RoboSysProcessCreate: return "SYS_PROCESS_CREATE";
    case RoboSysThreadCreate: return "SYS_THREAD_CREATE";
    case RoboSysSleep: return "SYS_SLEEP";
    case RoboSysTime: return "SYS_TIME";
    case RoboSysIpc: return "SYS_IPC";
    case RoboSysSeek: return "SYS_SEEK";
    case RoboSysStat: return "SYS_STAT";
    case RoboSysMkdir: return "SYS_MKDIR";
    case RoboSysRmdir: return "SYS_RMDIR";
    case RoboSysUnlink: return "SYS_UNLINK";
    case RoboSysRename: return "SYS_RENAME";
    case RoboSysWait: return "SYS_WAIT";
    case RoboSysGetpid: return "SYS_GETPID";
    case RoboSysDriverQuery: return "SYS_DRIVER_QUERY";
    case RoboSysDisplay: return "SYS_DISPLAY";
    case RoboSysSocket: return "SYS_SOCKET";
    case RoboSysThread: return "SYS_THREAD";
    default: return "SYS_UNKNOWN";
    }
}
