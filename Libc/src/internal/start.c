#include <stdlib.h>
#include <internal/libc.h>
#include <robo/memory.h>

#ifndef LIBC_HOST_TEST
#include <robosys/syscall_numbers.h>

extern int main(int argc, char **argv, char **envp);

void robo_tls_init(void)
{
    struct RoboTcb *tcb;
    tcb = (struct RoboTcb *)robo_mmap(0, 4096, ROBO_PROT_READ | ROBO_PROT_WRITE,
                                      ROBO_MAP_PRIVATE | ROBO_MAP_ANON, -1, 0);
    if (!tcb || tcb == (void *)-1)
        tcb = &g_boot_tcb;
    tcb->self = tcb;
    tcb->err = 0;
    robo_syscall((robo_long)ROBO_SYS_THREAD, (robo_long)ROBO_THREAD_OP_SET_FS,
                 (robo_long)(uintptr_t)tcb, 0, 0, 0, 0);
}

void robo_libc_start(int argc, char **argv, char **envp)
{
    int status;
    robo_tls_init();
    robo_heap_init();
    robo_stdio_init();
    robo_env_init(envp);
    status = main(argc, argv, envp);
    exit(status);
}

#endif
