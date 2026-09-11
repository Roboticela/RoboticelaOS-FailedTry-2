#include "syscall/syscall_impl.h"
#include "syscall/syscall_errors.h"
#include "syscall/syscall_numbers.h"
#include "syscall/syscall_validate.h"
#include "syscall/syscall_internal.h"
#include "syscall/syscall.h"
#include "process/process.h"
#include "thread/thread.h"
#include "scheduler/scheduler.h"
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
#include "fs/backends/regular.h"
#include "exec/exec.h"
#include "exec/elf/elf_loader.h"
#include "exec/elf/elf.h"
#include "exec/elf/elf_validate.h"
#include <robosys/types.h>

#define ROBO_FD_NONE       ((uint64_t)-1)

static uint64_t arg(const RoboSyscallContext *ctx, unsigned n)
{
    const struct robo_iframe *f = ctx->frame;
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

static RoboAddressSpace *ctx_as(const RoboSyscallContext *ctx)
{
    if (ctx && ctx->process)
        return ctx->process->address_space;
    return 0;
}

static int msg_is_hello(const char *s, unsigned n)
{
    const char *want = "Hello from user space.\n";
    unsigned i;
    for (i = 0; want[i]; i++) {
        if (i >= n || s[i] != want[i])
            return 0;
    }
    return 1;
}

static void zero_page(phys_addr_t pa)
{
    unsigned char *p = (unsigned char *)(uintptr_t)pa;
    unsigned i;
    for (i = 0; i < ROBO_PAGE_SIZE; i++)
        p[i] = 0;
}

static uint64_t prot_to_vm(uint64_t prot)
{
    uint64_t vm = ROBO_VM_USER;
    if (prot & ROBO_PROT_READ)
        vm |= ROBO_VM_READ;
    if (prot & ROBO_PROT_WRITE)
        vm |= ROBO_VM_READ | ROBO_VM_WRITE;
    if (prot & ROBO_PROT_EXEC)
        vm |= ROBO_VM_READ | ROBO_VM_EXECUTE;
    return vm;
}

static uint64_t sys_from_file(int64_t n)
{
    if (n >= 0)
        return (uint64_t)n;
    return robo_sys_err((int)(-n));
}

static int fd_from_user(uint64_t fd, robo_fd_t *out)
{
    if (fd > (uint64_t)0x7FFFFFFF)
        return 0;
    *out = (robo_fd_t)fd;
    return 1;
}

uint64_t robo_sys_enosys(RoboSyscallContext *ctx)
{
    (void)ctx;
    return robo_sys_err(ROBO_ENOSYS);
}

uint64_t robo_sys_abi_info(RoboSyscallContext *ctx)
{
    uint64_t out = arg(ctx, 1);
    RoboAbiInfo info;
    RoboAddressSpace *as;

    if (out) {
        as = ctx_as(ctx);
        info.major = ROBO_SYSCALL_ABI_MAJOR;
        info.minor = ROBO_SYSCALL_ABI_MINOR;
        info.features = ROBO_ABI_FEAT_WRITE | ROBO_ABI_FEAT_EXIT |
                        ROBO_ABI_FEAT_TIME | ROBO_ABI_FEAT_SLEEP |
                        ROBO_ABI_FEAT_MMAP | ROBO_ABI_FEAT_FS |
                        ROBO_ABI_FEAT_THREAD | ROBO_ABI_FEAT_PROCESS;
        info.reserved = 0;
        if (robo_copy_to_user(as, out, &info, sizeof(info)) != 0)
            return robo_sys_err(ROBO_EFAULT);
    }
    return ((uint64_t)ROBO_SYSCALL_ABI_MINOR << 32) | ROBO_SYSCALL_ABI_MAJOR;
}

uint64_t robo_sys_exit(RoboSyscallContext *ctx)
{
    int status = (int)arg(ctx, 1);
    RoboProcess *p = ctx->process;

    if (p && p != robo_kernel_process())
        robo_process_exit(status);
    return 0;
}

uint64_t robo_sys_write(RoboSyscallContext *ctx)
{
    uint64_t fd = arg(ctx, 1);
    uint64_t buf = arg(ctx, 2);
    uint64_t len = arg(ctx, 3);
    char tmp[ROBO_SYSCALL_WRITE_MAX];
    RoboAddressSpace *as = ctx_as(ctx);
    RoboFile *file;
    robo_fd_t nfd;
    int64_t n;

    if (!fd_from_user(fd, &nfd))
        return robo_sys_err(ROBO_EBADF);
    file = robo_fd_get(ctx->process, nfd);
    if (!file)
        return robo_sys_err(ROBO_EBADF);
    if (!(file->flags & ROBO_FFLAG_WRITE)) {
        robo_file_put(file);
        return robo_sys_err(ROBO_EBADF);
    }
    if (!len) {
        robo_file_put(file);
        return 0;
    }
    if (len > ROBO_SYSCALL_WRITE_MAX)
        len = ROBO_SYSCALL_WRITE_MAX;
    if (robo_copy_from_user(as, tmp, buf, (size_t)len) != 0) {
        robo_file_put(file);
        return robo_sys_err(ROBO_EFAULT);
    }
    if (msg_is_hello(tmp, (unsigned)len))
        robo_syscall_note_hello();
    robo_syscall_note_iso(tmp, (unsigned)len);
    n = robo_file_write(file, tmp, (size_t)len);
    robo_file_put(file);
    return sys_from_file(n);
}

uint64_t robo_sys_read(RoboSyscallContext *ctx)
{
    uint64_t fd = arg(ctx, 1);
    uint64_t buf = arg(ctx, 2);
    uint64_t len = arg(ctx, 3);
    char tmp[ROBO_SYSCALL_WRITE_MAX];
    RoboAddressSpace *as = ctx_as(ctx);
    RoboFile *file;
    robo_fd_t nfd;
    int64_t n;

    if (!fd_from_user(fd, &nfd))
        return robo_sys_err(ROBO_EBADF);
    file = robo_fd_get(ctx->process, nfd);
    if (!file)
        return robo_sys_err(ROBO_EBADF);
    if (!(file->flags & ROBO_FFLAG_READ)) {
        robo_file_put(file);
        return robo_sys_err(ROBO_EBADF);
    }
    if (!len) {
        robo_file_put(file);
        return 0;
    }
    if (len > ROBO_SYSCALL_WRITE_MAX)
        len = ROBO_SYSCALL_WRITE_MAX;
    if (!robo_user_range_valid(as, buf, len, ROBO_USER_ACCESS_WRITE)) {
        robo_file_put(file);
        return robo_sys_err(ROBO_EFAULT);
    }
    n = robo_file_read(file, tmp, (size_t)len);
    if (n < 0) {
        robo_file_put(file);
        return sys_from_file(n);
    }
    if (n && robo_copy_to_user(as, buf, tmp, (size_t)n) != 0) {
        robo_file_put(file);
        return robo_sys_err(ROBO_EFAULT);
    }
    robo_file_put(file);
    return (uint64_t)n;
}

uint64_t robo_sys_open(RoboSyscallContext *ctx)
{
    char path[ROBO_SYSCALL_PATH_MAX + 1];
    uint64_t upath = arg(ctx, 1);
    uint64_t flags = arg(ctx, 2);
    const void *data;
    uint32_t size;
    RoboFile *file;
    robo_fd_t fd;
    RoboAddressSpace *as = ctx_as(ctx);

    (void)arg(ctx, 3);
    if (flags != ROBO_O_RDONLY)
        return robo_sys_err(ROBO_EINVAL);
    if (robo_copy_user_string(as, path, upath, sizeof(path)) != 0)
        return robo_sys_err(ROBO_EFAULT);
    file = robo_fs_file_open(path, ROBO_FFLAG_READ);
    if (!file) {
        if (!robo_fs_open(path, &data, &size) || !data)
            return robo_sys_err(ROBO_ENOENT);
        return robo_sys_err(ROBO_ENOMEM);
    }
    fd = robo_fd_alloc(ctx->process, file, 0);
    robo_file_put(file);
    if (fd < 0)
        return robo_sys_err(ROBO_EMFILE);
    return (uint64_t)fd;
}

uint64_t robo_sys_close(RoboSyscallContext *ctx)
{
    uint64_t fd = arg(ctx, 1);
    robo_fd_t nfd;
    int r;

    if (!fd_from_user(fd, &nfd))
        return robo_sys_err(ROBO_EBADF);
    r = robo_fd_close(ctx->process, nfd);
    if (r != 0)
        return robo_sys_err(ROBO_EBADF);
    return 0;
}

static void mmap_unwind(RoboAddressSpace *as, uint64_t va, uint64_t pages)
{
    uint64_t i;
    for (i = 0; i < pages; i++) {
        phys_addr_t pa = ROBO_INVALID_PHYS_ADDR;
        if (robo_vmm_aspace_unmap(as, (virt_addr_t)(va + i * ROBO_PAGE_SIZE), &pa) &&
            pa != ROBO_INVALID_PHYS_ADDR)
            robo_page_free(pa);
    }
}

uint64_t robo_sys_mmap(RoboSyscallContext *ctx)
{
    uint64_t addr = arg(ctx, 1);
    uint64_t length = arg(ctx, 2);
    uint64_t prot = arg(ctx, 3);
    uint64_t flags = arg(ctx, 4);
    uint64_t fd = arg(ctx, 5);
    uint64_t off = arg(ctx, 6);
    uint64_t map_len, pages, i, va, vm;
    RoboProcess *p = ctx->process;
    RoboAddressSpace *as = ctx_as(ctx);
    uint64_t stack_floor;

    if (!p || !as)
        return robo_sys_err(ROBO_EINVAL);
    if (!length)
        return robo_sys_err(ROBO_EINVAL);
    if (length > (~0ull - (ROBO_PAGE_SIZE - 1ull)))
        return robo_sys_err(ROBO_EINVAL);
    map_len = (length + ROBO_PAGE_MASK) & ~ROBO_PAGE_MASK;
    if (!map_len)
        return robo_sys_err(ROBO_EINVAL);
    if (!(flags & ROBO_MAP_ANON))
        return robo_sys_err(ROBO_ENOSYS);
    if (off != 0 || (fd != ROBO_FD_NONE && fd != 0))
        return robo_sys_err(ROBO_EINVAL);
    if ((prot & ROBO_PROT_WRITE) && (prot & ROBO_PROT_EXEC))
        return robo_sys_err(ROBO_EINVAL);
    if (!(prot & (ROBO_PROT_READ | ROBO_PROT_WRITE | ROBO_PROT_EXEC)))
        return robo_sys_err(ROBO_EINVAL);
    vm = prot_to_vm(prot);
    if (!robo_vm_flags_valid(vm))
        return robo_sys_err(ROBO_EINVAL);

    stack_floor = ROBO_USER_STACK_TOP - (uint64_t)ROBO_USER_STACK_SIZE;
    if (flags & ROBO_MAP_FIXED) {
        if (addr & ROBO_PAGE_MASK)
            return robo_sys_err(ROBO_EINVAL);
        va = addr;
    } else {
        if (!p->mmap_next)
            p->mmap_next = ROBO_USER_MMAP_BASE;
        va = p->mmap_next;
        if (va & ROBO_PAGE_MASK)
            va = (va + ROBO_PAGE_MASK) & ~ROBO_PAGE_MASK;
    }
    if (!robo_user_va_ok(va, map_len))
        return robo_sys_err(ROBO_EFAULT);
    if (va < ROBO_USER_MMAP_BASE || va + map_len > stack_floor)
        return robo_sys_err(ROBO_EINVAL);

    pages = map_len / ROBO_PAGE_SIZE;
    for (i = 0; i < pages; i++) {
        phys_addr_t pa = robo_page_alloc();
        if (pa == ROBO_INVALID_PHYS_ADDR) {
            mmap_unwind(as, va, i);
            return robo_sys_err(ROBO_ENOMEM);
        }
        zero_page(pa);
        if (!robo_vmm_aspace_map(as, (virt_addr_t)(va + i * ROBO_PAGE_SIZE), pa, vm)) {
            robo_page_free(pa);
            mmap_unwind(as, va, i);
            return robo_sys_err(ROBO_EINVAL);
        }
    }
    if (!(flags & ROBO_MAP_FIXED))
        p->mmap_next = va + map_len;
    return va;
}

uint64_t robo_sys_munmap(RoboSyscallContext *ctx)
{
    uint64_t addr = arg(ctx, 1);
    uint64_t length = arg(ctx, 2);
    uint64_t map_len, pages, i;
    RoboAddressSpace *as = ctx_as(ctx);

    if (!as)
        return robo_sys_err(ROBO_EINVAL);
    if (!length)
        return robo_sys_err(ROBO_EINVAL);
    if (addr & ROBO_PAGE_MASK)
        return robo_sys_err(ROBO_EINVAL);
    if (length > (~0ull - (ROBO_PAGE_SIZE - 1ull)))
        return robo_sys_err(ROBO_EINVAL);
    map_len = (length + ROBO_PAGE_MASK) & ~ROBO_PAGE_MASK;
    if (!robo_user_va_ok(addr, map_len))
        return robo_sys_err(ROBO_EFAULT);
    pages = map_len / ROBO_PAGE_SIZE;
    for (i = 0; i < pages; i++) {
        phys_addr_t pa = ROBO_INVALID_PHYS_ADDR;
        virt_addr_t va = (virt_addr_t)(addr + i * ROBO_PAGE_SIZE);
        if (robo_vmm_aspace_unmap(as, va, &pa) && pa != ROBO_INVALID_PHYS_ADDR)
            robo_page_free(pa);
    }
    return 0;
}

uint64_t robo_sys_process_create(RoboSyscallContext *ctx)
{
    char path[ROBO_SYSCALL_PATH_MAX + 1];
    const void *data;
    uint32_t size;
    RoboProcess *child;
    int quiet;
    RoboAddressSpace *as = ctx_as(ctx);

    if (robo_copy_user_string(as, path, arg(ctx, 1), sizeof(path)) != 0)
        return robo_sys_err(ROBO_EFAULT);
    if (!robo_fs_open(path, &data, &size) || !data)
        return robo_sys_err(ROBO_ENOENT);
    (void)size;
    quiet = 1;
    robo_exec_set_quiet(quiet);
    child = robo_process_create_from_elf(path);
    robo_exec_set_quiet(0);
    if (!child)
        return robo_sys_err(ROBO_ENOMEM);
    return child->id;
}

uint64_t robo_sys_thread_create(RoboSyscallContext *ctx)
{
    uint64_t entry = arg(ctx, 1);
    uint64_t stack = arg(ctx, 2);
    uint64_t uarg = arg(ctx, 3);
    RoboAddressSpace *as = ctx_as(ctx);
    RoboThread *t;

    if (!ctx->process || ctx->process == robo_kernel_process())
        return robo_sys_err(ROBO_EPERM);
    if (!robo_user_range_valid(as, entry, 1, ROBO_USER_ACCESS_EXEC | ROBO_USER_ACCESS_READ))
        return robo_sys_err(ROBO_EFAULT);
    if (!robo_elf_entry_executable(as, entry))
        return robo_sys_err(ROBO_EFAULT);
    if (!robo_user_va_ok(stack, 1))
        return robo_sys_err(ROBO_EFAULT);
    if (!robo_user_range_valid(as, stack - 8ull, 8, ROBO_USER_ACCESS_WRITE))
        return robo_sys_err(ROBO_EFAULT);
    t = robo_thread_create_user(ctx->process, "uthread", entry, stack, uarg);
    if (!t)
        return robo_sys_err(ROBO_ENOMEM);
    return t->id;
}

uint64_t robo_sys_sleep(RoboSyscallContext *ctx)
{
    uint64_t ms = arg(ctx, 1);
    uint64_t ns, deadline;

    if (!ms)
        return 0;
    if (ms > (~0ull / ROBO_NS_PER_MS))
        return robo_sys_err(ROBO_EINVAL);
    ns = ms * ROBO_NS_PER_MS;
    deadline = robo_time_deadline(robo_time_now(), ns);
    if (!robo_thread_sleep_until(deadline))
        return robo_sys_err(ROBO_EBUSY);
    robo_syscall_note_slept();
    return 0;
}

uint64_t robo_sys_time(RoboSyscallContext *ctx)
{
    (void)ctx;
    return robo_time_now();
}

uint64_t robo_sys_ipc(RoboSyscallContext *ctx)
{
    (void)ctx;
    return robo_sys_err(ROBO_ENOSYS);
}
