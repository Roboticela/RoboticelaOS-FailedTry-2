#ifndef ROBO_LIBC_UNISTD_H
#define ROBO_LIBC_UNISTD_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
int close(int fd);
off_t lseek(int fd, off_t off, int whence);

unsigned sleep(unsigned seconds);
int usleep(unsigned usec);

pid_t getpid(void);
pid_t getppid(void);

int isatty(int fd);
int unlink(const char *path);
int rmdir(const char *path);

#endif
