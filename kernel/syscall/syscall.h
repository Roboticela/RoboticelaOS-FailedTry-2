#ifndef ROBO_SYSCALL_H
#define ROBO_SYSCALL_H

#include <stdint.h>
#include "roboticela/bootinfo.h"
#include "arch/x86_64/exception.h"

void robo_syscall_subsystem_init(void);
void robo_syscall_set_trace(int on);
int robo_syscall_trace(void);
uint64_t robo_syscall_count(void);
uint64_t robo_sysret_count(void);
unsigned robo_syscall_slept(void);
int robo_user_cpl_seen(void);
unsigned robo_user_timer_irqs(void);
void robo_user_note_iframe(const struct robo_iframe *frame);
int robo_hello_from_user(void);
int robo_write_ptr_rejected(void);
int robo_sys_saw_enosys(void);
int robo_sys_saw_efault(void);
int robo_sys_saw_ebadf(void);
int robo_sys_iso_a(void);
int robo_sys_iso_b(void);
int robo_syscall_selftest(const RoboBootInfo *info);
#ifndef SYSCALL_HOST_TEST
void robo_syscall_fb_report(void);
#endif

#endif
