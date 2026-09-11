#ifndef ROBO_ARCH_SYSCALL_H
#define ROBO_ARCH_SYSCALL_H

#include "arch/x86_64/exception.h"
#include <stdint.h>

#define ROBO_SYS_WRITE 1ull
#define ROBO_SYS_EXIT  2ull
#define ROBO_SYS_VEC   0x80ull

extern uint64_t robo_syscall_kstack;

void robo_syscall_init(void);
struct robo_iframe *robo_syscall_dispatch(struct robo_iframe *frame);
int robo_user_cpl_seen(void);
unsigned robo_user_timer_irqs(void);
void robo_user_note_iframe(const struct robo_iframe *frame);
int robo_hello_from_user(void);
int robo_write_ptr_rejected(void);

#endif
