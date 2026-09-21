#ifndef ROBO_SYSCALL_INTERNAL_H
#define ROBO_SYSCALL_INTERNAL_H

void SyscallNoteHello(void);
void SyscallNotePtrRejected(void);
void SyscallNoteEbadf(void);
void SyscallNoteEnosys(void);
void SyscallNoteSlept(void);
void SyscallNoteIso(const char *s, unsigned n);

#endif
