#include "fs/backends/console.h"
#include "syscall/syscall_errors.h"

#ifdef PROCESS_HOST_TEST
static void console_write_bytes(const void *buffer, size_t count)
{
    (void)buffer;
    (void)count;
}
#else
#include "kio.h"
static void console_write_bytes(const void *buffer, size_t count)
{
    kwrite((const char *)buffer, (unsigned)count);
}
#endif

static int64_t console_read(RoboFile *file, void *buffer, size_t count)
{
    (void)file;
    (void)buffer;
    (void)count;
    return -(int64_t)ROBO_ENOTSUP;
}

static int64_t console_write(RoboFile *file, const void *buffer, size_t count)
{
    (void)file;
    if (!count)
        return 0;
    if (!buffer)
        return -(int64_t)ROBO_EFAULT;
    console_write_bytes(buffer, count);
    return (int64_t)count;
}

static int console_close(RoboFile *file)
{
    (void)file;
    return 0;
}

static const RoboFileOps g_console_ops = {
    .read = console_read,
    .write = console_write,
    .close = console_close,
    .seek = 0,
    .ioctl = 0,
    .poll = 0,
};

RoboFile *robo_console_open(uint32_t flags)
{
    return robo_file_create(ROBO_FILE_CONSOLE, &g_console_ops, 0, flags);
}

int robo_console_install_stdio(RoboFDTable *table)
{
    RoboFile *in, *out, *err;
    robo_fd_t fd0, fd1, fd2;

    if (!table)
        return -1;
    in = robo_console_open(ROBO_FFLAG_READ);
    out = robo_console_open(ROBO_FFLAG_WRITE);
    err = robo_console_open(ROBO_FFLAG_WRITE);
    if (!in || !out || !err) {
        if (in)
            robo_file_put(in);
        if (out)
            robo_file_put(out);
        if (err)
            robo_file_put(err);
        return -1;
    }
    fd0 = robo_fd_table_alloc(table, in, 0);
    fd1 = robo_fd_table_alloc(table, out, 0);
    fd2 = robo_fd_table_alloc(table, err, 0);
    robo_file_put(in);
    robo_file_put(out);
    robo_file_put(err);
    if (fd0 != ROBO_FD_STDIN || fd1 != ROBO_FD_STDOUT || fd2 != ROBO_FD_STDERR)
        return -1;
    return 0;
}
