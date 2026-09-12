#ifndef ROBO_PROCESS_H
#define ROBO_PROCESS_H

#include <sys/types.h>

pid_t robo_getpid(void);
pid_t robo_getppid(void);
void robo_exit(int status);
pid_t robo_process_create(const char *path);
pid_t robo_spawn(const char *path, char *const argv[]);
pid_t robo_wait(pid_t pid, int *status);

#endif
