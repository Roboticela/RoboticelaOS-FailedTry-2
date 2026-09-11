#include "fs/file_ops.h"
#include "fs/file.h"
#include "syscall/syscall_errors.h"

int64_t robo_file_read(RoboFile *file, void *buffer, size_t count)
{
    if (!file)
        return -(int64_t)ROBO_EBADF;
    if (!(file->flags & ROBO_FFLAG_READ))
        return -(int64_t)ROBO_EBADF;
    if (!count)
        return 0;
    if (!buffer)
        return -(int64_t)ROBO_EFAULT;
    if (!file->ops || !file->ops->read)
        return -(int64_t)ROBO_ENOTSUP;
    return file->ops->read(file, buffer, count);
}

int64_t robo_file_write(RoboFile *file, const void *buffer, size_t count)
{
    if (!file)
        return -(int64_t)ROBO_EBADF;
    if (!(file->flags & ROBO_FFLAG_WRITE))
        return -(int64_t)ROBO_EBADF;
    if (!count)
        return 0;
    if (!buffer)
        return -(int64_t)ROBO_EFAULT;
    if (!file->ops || !file->ops->write)
        return -(int64_t)ROBO_ENOTSUP;
    return file->ops->write(file, buffer, count);
}

int robo_file_seek(RoboFile *file, int64_t offset, int whence)
{
    if (!file)
        return -ROBO_EBADF;
    if (!file->ops || !file->ops->seek)
        return -ROBO_ENOTSUP;
    return file->ops->seek(file, offset, whence);
}

int robo_file_ioctl(RoboFile *file, uint64_t request, void *arg)
{
    if (!file)
        return -ROBO_EBADF;
    if (!file->ops || !file->ops->ioctl)
        return -ROBO_ENOTSUP;
    return file->ops->ioctl(file, request, arg);
}

int robo_file_poll(RoboFile *file, uint32_t events)
{
    if (!file)
        return -ROBO_EBADF;
    if (!file->ops || !file->ops->poll)
        return -ROBO_ENOTSUP;
    return file->ops->poll(file, events);
}
