#ifndef ROBO_FILE_H
#define ROBO_FILE_H

#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int robo_open(const char *path, int flags, int mode);
int robo_close(int fd);
ssize_t robo_read(int fd, void *buf, size_t n);
ssize_t robo_write(int fd, const void *buf, size_t n);
off_t robo_seek(int fd, off_t off, int whence);
int robo_stat(const char *path, struct stat *st);
int robo_mkdir(const char *path, int mode);
int robo_rmdir(const char *path);
int robo_unlink(const char *path);
int robo_rename(const char *oldpath, const char *newpath);

#endif
