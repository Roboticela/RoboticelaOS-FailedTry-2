#ifndef ROBO_LIBC_INTERNAL_H
#define ROBO_LIBC_INTERNAL_H

#include <stdint.h>
#include <stddef.h>

typedef int64_t robo_long;
typedef uint64_t robo_ulong;

robo_long robo_syscall(robo_long number, robo_long a1, robo_long a2,
                       robo_long a3, robo_long a4, robo_long a5, robo_long a6);

int robo_is_err(robo_long r);
int robo_set_err(robo_long r);

void robo_tls_init(void);
void robo_heap_init(void);
void robo_stdio_init(void);
void robo_env_init(char **envp);
void robo_stdio_fini(void);

void robo_libc_start(int argc, char **argv, char **envp);

struct RoboTcb {
    struct RoboTcb *self;
    int err;
};

extern struct RoboTcb g_boot_tcb;

#endif
