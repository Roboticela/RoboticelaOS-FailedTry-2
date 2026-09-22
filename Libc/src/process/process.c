#include <robo/process.h>
#include <stdlib.h>
#include <unistd.h>
#include <internal/libc.h>

#ifndef LIBC_HOST_TEST
#include <robosys/syscall_numbers.h>

pid_t robo_getpid(void)
{
    return (pid_t)robo_syscall((robo_long)RoboSysGetpid, 0, 0, 0, 0, 0, 0);
}

pid_t robo_getppid(void)
{
    return (pid_t)robo_syscall((robo_long)RoboSysThread,
                               (robo_long)RoboThreadOpGetppid, 0, 0, 0, 0, 0);
}

void robo_exit(int status)
{
    exit(status);
}

pid_t robo_process_create(const char *path)
{
    robo_long r = robo_syscall((robo_long)RoboSysProcessCreate,
                               (robo_long)(uintptr_t)path, 0, 0, 0, 0, 0);
    if (robo_set_err(r))
        return (pid_t)-1;
    return (pid_t)r;
}

pid_t robo_spawn(const char *path, char *const argv[])
{
    int argc = 0;
    robo_long r;
    if (argv) {
        while (argv[argc])
            argc++;
    }
    r = robo_syscall((robo_long)RoboSysProcessCreate,
                     (robo_long)(uintptr_t)path,
                     (robo_long)(uintptr_t)argv, (robo_long)argc, 0, 0, 0);
    if (robo_set_err(r))
        return (pid_t)-1;
    return (pid_t)r;
}

pid_t robo_wait(pid_t pid, int *status)
{
    robo_long r = robo_syscall((robo_long)RoboSysWait, (robo_long)pid,
                               0, (robo_long)(uintptr_t)status, 0, 0, 0);
    if (robo_set_err(r))
        return (pid_t)-1;
    return (pid_t)r;
}

pid_t getpid(void) { return robo_getpid(); }
pid_t getppid(void) { return robo_getppid(); }

#endif
