#include "fs/backends/pipe.h"
#include "syscall/syscall_errors.h"

static int64_t pipe_read(RoboFile *file, void *buffer, size_t count)
{
    (void)file;
    (void)buffer;
    (void)count;
    return -(int64_t)ROBO_ENOTSUP;
}

static int64_t pipe_write(RoboFile *file, const void *buffer, size_t count)
{
    (void)file;
    (void)buffer;
    (void)count;
    return -(int64_t)ROBO_ENOTSUP;
}

static const RoboFileOps g_pipe_ops = {
    .read = pipe_read,
    .write = pipe_write,
    .close = 0,
    .seek = 0,
    .ioctl = 0,
    .poll = 0,
};

RoboFile *robo_pipe_file_create(uint32_t flags)
{
    return robo_file_create(ROBO_FILE_PIPE, &g_pipe_ops, 0, flags);
}
