#ifndef ROBO_LIBC_SYS_STAT_H
#define ROBO_LIBC_SYS_STAT_H

#include <stdint.h>
#include <sys/types.h>

#define S_IFDIR 0x4000
#define S_IFREG 0x8000

struct stat {
    uint64_t st_size;
    uint64_t st_ctime;
    uint64_t st_mtime;
    uint64_t st_atime;
    uint32_t st_mode;
    uint32_t st_nlink;
};

int stat(const char *path, struct stat *st);
int mkdir(const char *path, mode_t mode);
int rename(const char *oldpath, const char *newpath);

#endif
