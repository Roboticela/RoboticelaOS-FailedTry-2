#ifndef ROBO_SYSCALL_INTERNAL_H
#define ROBO_SYSCALL_INTERNAL_H

void robo_syscall_note_hello(void);
void robo_syscall_note_ptr_rejected(void);
void robo_syscall_note_ebadf(void);
void robo_syscall_note_enosys(void);
void robo_syscall_note_slept(void);
void robo_syscall_note_iso(const char *s, unsigned n);

#endif
