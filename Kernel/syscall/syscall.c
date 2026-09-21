#include "syscall/syscall_impl.h"
#include "syscall/syscall_errors.h"
#include "syscall/syscall_numbers.h"
#include "syscall/syscall_validate.h"
#include "syscall/syscall_internal.h"
#include "syscall/syscall.h"
#include "process/process.h"
#include "thread/thread.h"
#include "scheduler/scheduler.h"
#include "scheduler/sched_internal.h"
#include "mm/vmm/vmm.h"
#include "mm/vmm/paging.h"
#include "mm/page.h"
#include "mm/pmm.h"
#include "mm/heap/heap.h"
#include "time/time.h"
#include "time/convert.h"
#include "fs/robofs.h"
#include "fs/fd.h"
#include "fs/file_ops.h"
#include "fs/vfs/vfs.h"
#include "fs/backends/regular.h"
#include "exec/exec.h"
#include "exec/elf/elf_loader.h"
#include "exec/elf/elf.h"
#include "exec/elf/elf_validate.h"
#include "drivers/manager.h"
#include "graphics/gfx.h"
#include "input/input.h"
#include "drivers/driver.h"
#include "ipc/ipc.h"
#include "display/display.h"
#include "arch/x86_64/mm/paging_cpu.h"
#include <robosys/types.h>

#define RoboFdNone       ((uint64_t)-1)

static uint64_t arg(const RoboSyscallContext *ctx, unsigned n)
{
    const struct Iframe *f = ctx->frame;
    switch (n) {
    case 1: return f->rdi;
    case 2: return f->rsi;
    case 3: return f->rdx;
    case 4: return f->r10;
    case 5: return f->r8;
    case 6: return f->r9;
    default: return 0;
    }
}

static RoboAddressSpace *CtxAs(const RoboSyscallContext *ctx)
{
    if (ctx && ctx->process)
        return ctx->process->addressSpace;
    return 0;
}

static int MsgIsHello(const char *s, unsigned n)
{
    const char *want = "Hello from user space.\n";
    unsigned i;
    for (i = 0; want[i]; i++) {
        if (i >= n || s[i] != want[i])
            return 0;
    }
    return 1;
}

static void ZeroPage(PhysAddr pa)
{
    unsigned char *p = (unsigned char *)(uintptr_t)pa;
    unsigned i;
    for (i = 0; i < RoboPageSize; i++)
        p[i] = 0;
}

static uint64_t ProtToVm(uint64_t prot)
{
    uint64_t vm = RoboVmUser;
    if (prot & RoboProtRead)
        vm |= RoboVmRead;
    if (prot & RoboProtWrite)
        vm |= RoboVmRead | RoboVmWrite;
    if (prot & RoboProtExec)
        vm |= RoboVmRead | RoboVmExecute;
    return vm;
}

static uint64_t SysFromFile(int64_t n)
{
    if (n >= 0)
        return (uint64_t)n;
    return SysErr((int)(-n));
}

static int FdFromUser(uint64_t fd, Fd *out)
{
    if (fd > (uint64_t)0x7FFFFFFF)
        return 0;
    *out = (Fd)fd;
    return 1;
}

uint64_t SysEnosys(RoboSyscallContext *ctx)
{
    (void)ctx;
    return SysErr(RoboEnosys);
}

uint64_t SysAbiInfo(RoboSyscallContext *ctx)
{
    uint64_t out = arg(ctx, 1);
    RoboAbiInfo info;
    RoboAddressSpace *as;

    if (out) {
        as = CtxAs(ctx);
        info.major = RoboSyscallAbiMajor;
        info.minor = RoboSyscallAbiMinor;
        info.features = RoboAbiFeatWrite | RoboAbiFeatExit |
                        RoboAbiFeatTime | RoboAbiFeatSleep |
                        RoboAbiFeatMmap | RoboAbiFeatFs |
                        RoboAbiFeatThread | RoboAbiFeatProcess |
                        RoboAbiFeatWait | RoboAbiFeatDriver |
                        RoboAbiFeatIpc | RoboAbiFeatDisplay |
                        RoboAbiFeatNet | RoboAbiFeatTls;
        info.reserved = 0;
        if (CopyToUser(as, out, &info, sizeof(info)) != 0)
            return SysErr(RoboEfault);
    }
    return ((uint64_t)RoboSyscallAbiMinor << 32) | RoboSyscallAbiMajor;
}

uint64_t SysExit(RoboSyscallContext *ctx)
{
    int status = (int)arg(ctx, 1);
    RoboProcess *p = ctx->process;

    if (p && p != KernelProcess())
        ProcessExit(status);
    return 0;
}

uint64_t SysWrite(RoboSyscallContext *ctx)
{
    uint64_t fd = arg(ctx, 1);
    uint64_t buf = arg(ctx, 2);
    uint64_t len = arg(ctx, 3);
    char tmp[RoboSyscallWriteMax];
    RoboAddressSpace *as = CtxAs(ctx);
    RoboFile *file;
    Fd nfd;
    int64_t n;

    if (!FdFromUser(fd, &nfd))
        return SysErr(RoboEbadf);
    file = FdGet(ctx->process, nfd);
    if (!file)
        return SysErr(RoboEbadf);
    if (!(file->flags & RoboFflagWrite)) {
        FilePut(file);
        return SysErr(RoboEbadf);
    }
    if (!len) {
        FilePut(file);
        return 0;
    }
    if (len > RoboSyscallWriteMax)
        len = RoboSyscallWriteMax;
    if (CopyFromUser(as, tmp, buf, (size_t)len) != 0) {
        FilePut(file);
        return SysErr(RoboEfault);
    }
    if (MsgIsHello(tmp, (unsigned)len))
        SyscallNoteHello();
    SyscallNoteIso(tmp, (unsigned)len);
    n = FileWrite(file, tmp, (size_t)len);
    FilePut(file);
    return SysFromFile(n);
}

uint64_t SysRead(RoboSyscallContext *ctx)
{
    uint64_t fd = arg(ctx, 1);
    uint64_t buf = arg(ctx, 2);
    uint64_t len = arg(ctx, 3);
    char tmp[RoboSyscallWriteMax];
    RoboAddressSpace *as = CtxAs(ctx);
    RoboFile *file;
    Fd nfd;
    int64_t n;

    if (!FdFromUser(fd, &nfd))
        return SysErr(RoboEbadf);
    file = FdGet(ctx->process, nfd);
    if (!file)
        return SysErr(RoboEbadf);
    if (!(file->flags & RoboFflagRead)) {
        FilePut(file);
        return SysErr(RoboEbadf);
    }
    if (!len) {
        FilePut(file);
        return 0;
    }
    if (len > RoboSyscallWriteMax)
        len = RoboSyscallWriteMax;
    if (!UserRangeValid(as, buf, len, RoboUserAccessWrite)) {
        FilePut(file);
        return SysErr(RoboEfault);
    }
    n = FileRead(file, tmp, (size_t)len);
    if (n < 0) {
        FilePut(file);
        return SysFromFile(n);
    }
    if (n && CopyToUser(as, buf, tmp, (size_t)n) != 0) {
        FilePut(file);
        return SysErr(RoboEfault);
    }
    FilePut(file);
    return (uint64_t)n;
}

static uint64_t SysFromNeg(int r)
{
    if (r >= 0)
        return (uint64_t)r;
    return SysErr(-r);
}

uint64_t SysOpen(RoboSyscallContext *ctx)
{
    char path[RoboSyscallPathMax + 1];
    uint64_t upath = arg(ctx, 1);
    uint64_t flags = arg(ctx, 2);
    uint64_t mode = arg(ctx, 3);
    uint32_t known = 3u | RoboOCreat | RoboOTrunc;
    RoboFile *file;
    Fd fd;
    RoboAddressSpace *as = CtxAs(ctx);
    int r;

    if (flags > 0xFFFFFFFFu || (flags & ~(uint64_t)known))
        return SysErr(RoboEinval);
    if ((flags & 3u) > RoboORdwr)
        return SysErr(RoboEinval);
    if (mode > 0xFFFFFFFFu)
        return SysErr(RoboEinval);
    if (CopyUserString(as, path, upath, sizeof(path)) != 0)
        return SysErr(RoboEfault);
    r = VfsOpen(path, (uint32_t)flags, (uint32_t)mode, &file);
    if (r != 0)
        return SysFromNeg(r);
    fd = FdAlloc(ctx->process, file, 0);
    FilePut(file);
    if (fd < 0)
        return SysErr(RoboEmfile);
    return (uint64_t)fd;
}

uint64_t SysClose(RoboSyscallContext *ctx)
{
    uint64_t fd = arg(ctx, 1);
    Fd nfd;
    int r;

    if (!FdFromUser(fd, &nfd))
        return SysErr(RoboEbadf);
    r = FdClose(ctx->process, nfd);
    if (r != 0)
        return SysErr(RoboEbadf);
    return 0;
}

static void MmapUnwind(RoboAddressSpace *as, uint64_t va, uint64_t pages)
{
    uint64_t i;
    for (i = 0; i < pages; i++) {
        PhysAddr pa = RoboInvalidPhysAddr;
        if (VmmAspaceUnmap(as, (VirtAddr)(va + i * RoboPageSize), &pa) &&
            pa != RoboInvalidPhysAddr)
            PageFree(pa);
    }
}

uint64_t SysMmap(RoboSyscallContext *ctx)
{
    uint64_t addr = arg(ctx, 1);
    uint64_t length = arg(ctx, 2);
    uint64_t prot = arg(ctx, 3);
    uint64_t flags = arg(ctx, 4);
    uint64_t fd = arg(ctx, 5);
    uint64_t off = arg(ctx, 6);
    uint64_t mapLen, pages, i, va, vm;
    RoboProcess *p = ctx->process;
    RoboAddressSpace *as = CtxAs(ctx);
    uint64_t stackFloor;

    if (!p || !as)
        return SysErr(RoboEinval);
    if (!length)
        return SysErr(RoboEinval);
    if (length > (~0ull - (RoboPageSize - 1ull)))
        return SysErr(RoboEinval);
    mapLen = (length + RoboPageMask) & ~RoboPageMask;
    if (!mapLen)
        return SysErr(RoboEinval);
    if (!(flags & RoboMapAnon))
        return SysErr(RoboEnosys);
    if (off != 0 || (fd != RoboFdNone && fd != 0))
        return SysErr(RoboEinval);
    if ((prot & RoboProtWrite) && (prot & RoboProtExec))
        return SysErr(RoboEinval);
    if (!(prot & (RoboProtRead | RoboProtWrite | RoboProtExec)))
        return SysErr(RoboEinval);
    vm = ProtToVm(prot);
    if (!VmFlagsValid(vm))
        return SysErr(RoboEinval);

    stackFloor = RoboUserStackTop - (uint64_t)RoboUserStackSize;
    if (flags & RoboMapFixed) {
        if (addr & RoboPageMask)
            return SysErr(RoboEinval);
        va = addr;
    } else {
        if (!p->mmapNext)
            p->mmapNext = RoboUserMmapBase;
        va = p->mmapNext;
        if (va & RoboPageMask)
            va = (va + RoboPageMask) & ~RoboPageMask;
    }
    if (!UserVaOk(va, mapLen))
        return SysErr(RoboEfault);
    if (va < RoboUserMmapBase || va + mapLen > stackFloor)
        return SysErr(RoboEinval);

    pages = mapLen / RoboPageSize;
    for (i = 0; i < pages; i++) {
        PhysAddr pa = PageAlloc();
        if (pa == RoboInvalidPhysAddr) {
            MmapUnwind(as, va, i);
            return SysErr(RoboEnomem);
        }
        ZeroPage(pa);
        if (!VmmAspaceMap(as, (VirtAddr)(va + i * RoboPageSize), pa, vm)) {
            PageFree(pa);
            MmapUnwind(as, va, i);
            return SysErr(RoboEinval);
        }
    }
    if (!(flags & RoboMapFixed))
        p->mmapNext = va + mapLen;
    return va;
}

uint64_t SysMunmap(RoboSyscallContext *ctx)
{
    uint64_t addr = arg(ctx, 1);
    uint64_t length = arg(ctx, 2);
    uint64_t mapLen, pages, i;
    RoboAddressSpace *as = CtxAs(ctx);

    if (!as)
        return SysErr(RoboEinval);
    if (!length)
        return SysErr(RoboEinval);
    if (addr & RoboPageMask)
        return SysErr(RoboEinval);
    if (length > (~0ull - (RoboPageSize - 1ull)))
        return SysErr(RoboEinval);
    mapLen = (length + RoboPageMask) & ~RoboPageMask;
    if (!UserVaOk(addr, mapLen))
        return SysErr(RoboEfault);
    pages = mapLen / RoboPageSize;
    for (i = 0; i < pages; i++) {
        PhysAddr pa = RoboInvalidPhysAddr;
        VirtAddr va = (VirtAddr)(addr + i * RoboPageSize);
        if (VmmAspaceUnmap(as, va, &pa) && pa != RoboInvalidPhysAddr) {
            if (!IpcShmOwns(pa))
                PageFree(pa);
        }
    }
    return 0;
}

uint64_t SysProcessCreate(RoboSyscallContext *ctx)
{
    char path[RoboSyscallPathMax + 1];
    RoboProcess *child;
    int quiet;
    RoboAddressSpace *as = CtxAs(ctx);
    ExecStatus st;
    uint64_t uargv = arg(ctx, 2);
    uint64_t argc = arg(ctx, 3);
    RoboExecArgv av;
    unsigned i;

    if (CopyUserString(as, path, arg(ctx, 1), sizeof(path)) != 0)
        return SysErr(RoboEfault);
    if (!ExecImagePresent(path))
        return SysErr(RoboEnoent);

    av.argc = 0;
    if (argc) {
        if (argc > RoboExecArgvMax)
            return SysErr(RoboEinval);
        if (!uargv)
            return SysErr(RoboEinval);
        for (i = 0; i < argc; i++)
            av.args[i][0] = 0;
        for (i = 0; i < (unsigned)argc; i++) {
            uint64_t uptr = 0;
            uint64_t slot = uargv + (uint64_t)i * 8ull;
            if (CopyFromUser(as, &uptr, slot, 8) != 0)
                return SysErr(RoboEfault);
            if (!uptr)
                return SysErr(RoboEinval);
            if (CopyUserString(as, av.args[i], uptr, sizeof(av.args[i])) != 0)
                return SysErr(RoboEfault);
        }
        av.argc = (unsigned)argc;
    }

    quiet = 1;
    ExecSetQuiet(quiet);
    child = av.argc ? ProcessCreateFromElfArgv(path, &av)
                    : ProcessCreateFromElf(path);
    ExecSetQuiet(0);
    if (!child) {
        st = ExecLastStatus();
        if (st == RoboElfFileFailure)
            return SysErr(RoboEnoent);
        if (st == RoboElfMemoryFailure)
            return SysErr(RoboEnomem);
        return SysErr(RoboEinval);
    }
    return child->id;
}

uint64_t SysThreadCreate(RoboSyscallContext *ctx)
{
    uint64_t entry = arg(ctx, 1);
    uint64_t stack = arg(ctx, 2);
    uint64_t uarg = arg(ctx, 3);
    RoboAddressSpace *as = CtxAs(ctx);
    RoboThread *t;

    if (!ctx->process || ctx->process == KernelProcess())
        return SysErr(RoboEperm);
    if (!UserRangeValid(as, entry, 1, RoboUserAccessExec | RoboUserAccessRead))
        return SysErr(RoboEfault);
    if (!ElfEntryExecutable(as, entry))
        return SysErr(RoboEfault);
    if (!UserVaOk(stack, 1))
        return SysErr(RoboEfault);
    if (!UserRangeValid(as, stack - 8ull, 8, RoboUserAccessWrite))
        return SysErr(RoboEfault);
    t = ThreadCreateUser(ctx->process, "uthread", entry, stack, uarg);
    if (!t)
        return SysErr(RoboEnomem);
    return t->id;
}

uint64_t SysSleep(RoboSyscallContext *ctx)
{
    uint64_t ms = arg(ctx, 1);
    uint64_t ns, deadline;

    if (!ms)
        return 0;
    if (ms > (~0ull / RoboNsPerMs))
        return SysErr(RoboEinval);
    ns = ms * RoboNsPerMs;
    deadline = TimeDeadline(TimeNow(), ns);
    if (!ThreadSleepUntil(deadline))
        return SysErr(RoboEbusy);
    SyscallNoteSlept();
    return 0;
}

uint64_t RoboSysTime(RoboSyscallContext *ctx)
{
    (void)ctx;
    return TimeNow();
}

uint64_t SysIpc(RoboSyscallContext *ctx)
{
    return IpcSyscall(ctx);
}

uint64_t SysSeek(RoboSyscallContext *ctx)
{
    uint64_t fd = arg(ctx, 1);
    int64_t off = (int64_t)arg(ctx, 2);
    uint64_t whence = arg(ctx, 3);
    RoboFile *file;
    Fd nfd;
    int r;
    uint64_t pos;

    if (!FdFromUser(fd, &nfd))
        return SysErr(RoboEbadf);
    if (whence > (uint64_t)RoboSeekEnd)
        return SysErr(RoboEinval);
    file = FdGet(ctx->process, nfd);
    if (!file)
        return SysErr(RoboEbadf);
    r = FileSeek(file, off, (int)whence);
    if (r != 0) {
        FilePut(file);
        return SysFromNeg(r);
    }
    pos = file->offset;
    FilePut(file);
    return pos;
}

uint64_t SysStat(RoboSyscallContext *ctx)
{
    char path[RoboSyscallPathMax + 1];
    uint64_t upath = arg(ctx, 1);
    uint64_t ust = arg(ctx, 2);
    RoboStat st;
    RoboAddressSpace *as = CtxAs(ctx);
    int r;

    if (CopyUserString(as, path, upath, sizeof(path)) != 0)
        return SysErr(RoboEfault);
    if (!UserRangeValid(as, ust, sizeof(st), RoboUserAccessWrite))
        return SysErr(RoboEfault);
    r = VfsStatPath(path, &st);
    if (r != 0)
        return SysFromNeg(r);
    if (CopyToUser(as, ust, &st, sizeof(st)) != 0)
        return SysErr(RoboEfault);
    return 0;
}

uint64_t SysMkdir(RoboSyscallContext *ctx)
{
    char path[RoboSyscallPathMax + 1];
    uint64_t upath = arg(ctx, 1);
    uint64_t mode = arg(ctx, 2);
    RoboAddressSpace *as = CtxAs(ctx);
    int r;

    if (mode > 0xFFFFFFFFu)
        return SysErr(RoboEinval);
    if (CopyUserString(as, path, upath, sizeof(path)) != 0)
        return SysErr(RoboEfault);
    r = VfsMkdir(path, (uint32_t)mode);
    return SysFromNeg(r);
}

uint64_t SysRmdir(RoboSyscallContext *ctx)
{
    char path[RoboSyscallPathMax + 1];
    uint64_t upath = arg(ctx, 1);
    RoboAddressSpace *as = CtxAs(ctx);
    int r;

    if (CopyUserString(as, path, upath, sizeof(path)) != 0)
        return SysErr(RoboEfault);
    r = VfsRmdir(path);
    return SysFromNeg(r);
}

uint64_t SysUnlink(RoboSyscallContext *ctx)
{
    char path[RoboSyscallPathMax + 1];
    uint64_t upath = arg(ctx, 1);
    RoboAddressSpace *as = CtxAs(ctx);
    int r;

    if (CopyUserString(as, path, upath, sizeof(path)) != 0)
        return SysErr(RoboEfault);
    r = VfsUnlink(path);
    return SysFromNeg(r);
}

uint64_t SysRename(RoboSyscallContext *ctx)
{
    char oldp[RoboSyscallPathMax + 1];
    char newp[RoboSyscallPathMax + 1];
    uint64_t uold = arg(ctx, 1);
    uint64_t unew = arg(ctx, 2);
    RoboAddressSpace *as = CtxAs(ctx);
    int r;

    if (CopyUserString(as, oldp, uold, sizeof(oldp)) != 0)
        return SysErr(RoboEfault);
    if (CopyUserString(as, newp, unew, sizeof(newp)) != 0)
        return SysErr(RoboEfault);
    r = VfsRename(oldp, newp);
    return SysFromNeg(r);
}

uint64_t SysWait(RoboSyscallContext *ctx)
{
    uint64_t pid = arg(ctx, 1);
    uint64_t flags = arg(ctx, 2);
    uint64_t ust = arg(ctx, 3);
    RoboAddressSpace *as = CtxAs(ctx);
    int status = 0;
    ProcessId out = 0;
    int r;

    if (flags & ~((uint64_t)RoboWaitNohang))
        return SysErr(RoboEinval);
    if (ust && !UserRangeValid(as, ust, sizeof(int), RoboUserAccessWrite))
        return SysErr(RoboEfault);
    r = ProcessWait(pid, (uint32_t)flags, &status, &out);
    if (r != 0)
        return SysFromNeg(r);
    if (ust && CopyToUser(as, ust, &status, sizeof(status)) != 0)
        return SysErr(RoboEfault);
    return out;
}

uint64_t SysGetpid(RoboSyscallContext *ctx)
{
    if (!ctx->process)
        return SysErr(RoboEinval);
    return ctx->process->id;
}

uint64_t SysDriverQuery(RoboSyscallContext *ctx)
{
    uint64_t u = arg(ctx, 1);
    RoboDriverStatus st;
    RoboDisplayInfo di;
    unsigned pci = 0, reg = 0, loaded = 0, failed = 0, sig = 0, rej = 0;
    RoboAddressSpace *as = CtxAs(ctx);

    if (!u)
        return SysErr(RoboEinval);
    if (!UserRangeValid(as, u, sizeof(st), RoboUserAccessWrite))
        return SysErr(RoboEfault);
    HwStatus(&pci, &reg, &loaded, &failed, &sig, &rej);
    st.pciCount = pci;
    st.driversRegistered = reg;
    st.driversLoaded = loaded;
    st.driversFailed = failed;
    st.signedOk = sig;
    st.rejected = rej;
    st.storageReady = HwStorageReady() ? 1u : 0u;
    st.gfxWidth = 0;
    st.gfxHeight = 0;
    if (DisplayGetInfo(&di)) {
        st.gfxWidth = di.width;
        st.gfxHeight = di.height;
    }
    st.keyboard = InputKeyboardReady() ? 1u : 0u;
    st.mouse = InputMouseReady() ? 1u : 0u;
    st.testMode = DriverTestMode() ? 1u : 0u;
    if (CopyToUser(as, u, &st, sizeof(st)) != 0)
        return SysErr(RoboEfault);
    return 0;
}

uint64_t SysThread(RoboSyscallContext *ctx)
{
    uint64_t op = arg(ctx, 1);
    uint64_t a2 = arg(ctx, 2);
    RoboThread *cur = ctx->thread;
    RoboProcess *proc = ctx->process;
    RoboThread *target;
    RoboAddressSpace *as = CtxAs(ctx);
    int status;

    if (!cur || !proc)
        return SysErr(RoboEinval);

    switch (op) {
    case RoboThreadOpYield:
        ThreadYield();
        return 0;
    case RoboThreadOpExit:
        cur->userExitStatus = (int)a2;
        ThreadExit();
        return 0;
    case RoboThreadOpJoin:
        if (!a2)
            return SysErr(RoboEinval);
        target = ThreadById(a2);
        if (!target || target->process != proc)
            return SysErr(RoboEsrch);
        if (target == cur || target->kind != RoboThreadUser)
            return SysErr(RoboEinval);
        if (target->joinConsumed)
            return SysErr(RoboEinval);
        if (target->joinPosted) {
            status = target->userExitStatus;
            target->joinConsumed = 1;
            SchedReapLater(target);
            return (uint64_t)(uint32_t)status;
        }
        if (target->joinWaiter)
            return SysErr(RoboEagain);
        target->joinWaiter = cur;
        ThreadBlock();
        target = ThreadById(a2);
        if (!target)
            return SysErr(RoboEsrch);
        status = target->userExitStatus;
        target->joinConsumed = 1;
        target->joinWaiter = 0;
        SchedReapLater(target);
        return (uint64_t)(uint32_t)status;
    case RoboThreadOpSetFs:
        if (!a2 || !UserRangeValid(as, a2, 16, RoboUserAccessWrite))
            return SysErr(RoboEfault);
        cur->userFsBase = a2;
        CpuWriteFsBase(a2);
        return 0;
    case RoboThreadOpGettid:
        return cur->id;
    case RoboThreadOpGetppid:
        return proc->parent ? proc->parent->id : 0;
    default:
        return SysErr(RoboEinval);
    }
}
