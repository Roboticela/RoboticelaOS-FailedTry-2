#ifndef ROBO_MEMORY_H
#define ROBO_MEMORY_H

#include <stddef.h>

#define ROBO_PROT_READ  1
#define ROBO_PROT_WRITE 2
#define ROBO_PROT_EXEC  4
#define ROBO_MAP_PRIVATE 1
#define ROBO_MAP_ANON    2
#define ROBO_MAP_FIXED   4

void *robo_mmap(void *addr, size_t len, int prot, int flags, int fd, size_t off);
int robo_munmap(void *addr, size_t len);

#endif
