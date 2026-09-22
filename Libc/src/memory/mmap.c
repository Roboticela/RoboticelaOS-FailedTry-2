#include <robo/memory.h>
#include <internal/libc.h>
#include <errno.h>

#ifndef LIBC_HOST_TEST
#include <robosys/syscall_numbers.h>

void *robo_mmap(void *addr, size_t len, int prot, int flags, int fd, size_t off)
{
    robo_long r = robo_syscall((robo_long)RoboSysMmap, (robo_long)(uintptr_t)addr,
                               (robo_long)len, (robo_long)prot, (robo_long)flags,
                               (robo_long)fd, (robo_long)off);
    if (robo_set_err(r))
        return (void *)-1;
    return (void *)(uintptr_t)(uint64_t)r;
}

int robo_munmap(void *addr, size_t len)
{
    robo_long r = robo_syscall((robo_long)RoboSysMunmap, (robo_long)(uintptr_t)addr,
                               (robo_long)len, 0, 0, 0, 0);
    if (robo_set_err(r))
        return -1;
    return 0;
}
#endif
