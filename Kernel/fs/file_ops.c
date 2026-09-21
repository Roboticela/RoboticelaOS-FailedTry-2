#include "fs/file_ops.h"
#include "fs/file.h"
#include "syscall/syscall_errors.h"

int64_t FileRead(RoboFile *file, void *buffer, size_t count)
{
    if (!file)
        return -(int64_t)RoboEbadf;
    if (!(file->flags & RoboFflagRead))
        return -(int64_t)RoboEbadf;
    if (!count)
        return 0;
    if (!buffer)
        return -(int64_t)RoboEfault;
    if (!file->ops || !file->ops->read)
        return -(int64_t)RoboEnotsup;
    return file->ops->read(file, buffer, count);
}

int64_t FileWrite(RoboFile *file, const void *buffer, size_t count)
{
    if (!file)
        return -(int64_t)RoboEbadf;
    if (!(file->flags & RoboFflagWrite))
        return -(int64_t)RoboEbadf;
    if (!count)
        return 0;
    if (!buffer)
        return -(int64_t)RoboEfault;
    if (!file->ops || !file->ops->write)
        return -(int64_t)RoboEnotsup;
    return file->ops->write(file, buffer, count);
}

int FileSeek(RoboFile *file, int64_t offset, int whence)
{
    if (!file)
        return -RoboEbadf;
    if (!file->ops || !file->ops->seek)
        return -RoboEnotsup;
    return file->ops->seek(file, offset, whence);
}

int FileIoctl(RoboFile *file, uint64_t request, void *arg)
{
    if (!file)
        return -RoboEbadf;
    if (!file->ops || !file->ops->ioctl)
        return -RoboEnotsup;
    return file->ops->ioctl(file, request, arg);
}

int FilePoll(RoboFile *file, uint32_t events)
{
    if (!file)
        return -RoboEbadf;
    if (!file->ops || !file->ops->poll)
        return -RoboEnotsup;
    return file->ops->poll(file, events);
}
