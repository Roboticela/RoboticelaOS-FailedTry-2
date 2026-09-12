#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <internal/libc.h>
#include <robo/file.h>

#ifndef LIBC_HOST_TEST
#include <robosys/syscall_numbers.h>
#include <robosys/types.h>

static int sys_ret(robo_long r)
{
    if (robo_set_err(r))
        return -1;
    return (int)r;
}

int robo_open(const char *path, int flags, int mode)
{
    return sys_ret(robo_syscall((robo_long)ROBO_SYS_OPEN, (robo_long)(uintptr_t)path,
                                (robo_long)flags, (robo_long)mode, 0, 0, 0));
}

int robo_close(int fd)
{
    return sys_ret(robo_syscall((robo_long)ROBO_SYS_CLOSE, (robo_long)fd, 0, 0, 0, 0, 0));
}

ssize_t robo_read(int fd, void *buf, size_t n)
{
    robo_long r = robo_syscall((robo_long)ROBO_SYS_READ, (robo_long)fd,
                               (robo_long)(uintptr_t)buf, (robo_long)n, 0, 0, 0);
    if (robo_set_err(r))
        return -1;
    return (ssize_t)r;
}

ssize_t robo_write(int fd, const void *buf, size_t n)
{
    robo_long r = robo_syscall((robo_long)ROBO_SYS_WRITE, (robo_long)fd,
                               (robo_long)(uintptr_t)buf, (robo_long)n, 0, 0, 0);
    if (robo_set_err(r))
        return -1;
    return (ssize_t)r;
}

off_t robo_seek(int fd, off_t off, int whence)
{
    robo_long r = robo_syscall((robo_long)ROBO_SYS_SEEK, (robo_long)fd,
                               (robo_long)off, (robo_long)whence, 0, 0, 0);
    if (robo_set_err(r))
        return -1;
    return (off_t)r;
}

int robo_stat(const char *path, struct stat *st)
{
    RoboStat ks;
    robo_long r;
    if (!st) {
        errno = EINVAL;
        return -1;
    }
    r = robo_syscall((robo_long)ROBO_SYS_STAT, (robo_long)(uintptr_t)path,
                     (robo_long)(uintptr_t)&ks, 0, 0, 0, 0);
    if (robo_set_err(r))
        return -1;
    st->st_size = ks.size;
    st->st_ctime = ks.created;
    st->st_mtime = ks.modified;
    st->st_atime = ks.accessed;
    st->st_nlink = ks.nlink;
    st->st_mode = (ks.type == 2) ? S_IFDIR : S_IFREG;
    return 0;
}

int robo_mkdir(const char *path, int mode)
{
    return sys_ret(robo_syscall((robo_long)ROBO_SYS_MKDIR, (robo_long)(uintptr_t)path,
                                (robo_long)mode, 0, 0, 0, 0));
}

int robo_rmdir(const char *path)
{
    return sys_ret(robo_syscall((robo_long)ROBO_SYS_RMDIR, (robo_long)(uintptr_t)path,
                                0, 0, 0, 0, 0));
}

int robo_unlink(const char *path)
{
    return sys_ret(robo_syscall((robo_long)ROBO_SYS_UNLINK, (robo_long)(uintptr_t)path,
                                0, 0, 0, 0, 0));
}

int robo_rename(const char *oldpath, const char *newpath)
{
    return sys_ret(robo_syscall((robo_long)ROBO_SYS_RENAME,
                                (robo_long)(uintptr_t)oldpath,
                                (robo_long)(uintptr_t)newpath, 0, 0, 0, 0));
}

int open(const char *path, int flags, ...)
{
    int mode = 0;
    va_list ap;
    va_start(ap, flags);
    if (flags & O_CREAT)
        mode = va_arg(ap, int);
    va_end(ap);
    return robo_open(path, flags, mode);
}

ssize_t read(int fd, void *buf, size_t n) { return robo_read(fd, buf, n); }
ssize_t write(int fd, const void *buf, size_t n) { return robo_write(fd, buf, n); }
int close(int fd) { return robo_close(fd); }
off_t lseek(int fd, off_t off, int whence) { return robo_seek(fd, off, whence); }
int unlink(const char *path) { return robo_unlink(path); }
int rmdir(const char *path) { return robo_rmdir(path); }
int mkdir(const char *path, mode_t mode) { return robo_mkdir(path, (int)mode); }
int rename(const char *a, const char *b) { return robo_rename(a, b); }
int stat(const char *path, struct stat *st) { return robo_stat(path, st); }

int isatty(int fd)
{
    return (fd >= 0 && fd <= 2) ? 1 : 0;
}

unsigned sleep(unsigned seconds)
{
    struct timespec ts;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = 0;
    return nanosleep(&ts, 0) == 0 ? 0 : seconds;
}

int usleep(unsigned usec)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(usec / 1000000u);
    ts.tv_nsec = (long)(usec % 1000000u) * 1000L;
    return nanosleep(&ts, 0);
}

#endif
