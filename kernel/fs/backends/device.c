#include "fs/backends/device.h"
#include "syscall/syscall_errors.h"

static int64_t device_read(RoboFile *file, void *buffer, size_t count)
{
    (void)file;
    (void)buffer;
    (void)count;
    return -(int64_t)ROBO_ENOTSUP;
}

static int64_t device_write(RoboFile *file, const void *buffer, size_t count)
{
    (void)file;
    (void)buffer;
    (void)count;
    return -(int64_t)ROBO_ENOTSUP;
}

static int device_ioctl(RoboFile *file, uint64_t request, void *arg)
{
    (void)file;
    (void)request;
    (void)arg;
    return -ROBO_ENOTSUP;
}

static const RoboFileOps g_device_ops = {
    .read = device_read,
    .write = device_write,
    .close = 0,
    .seek = 0,
    .ioctl = device_ioctl,
    .poll = 0,
};

RoboFile *robo_device_file_create(uint32_t flags)
{
    return robo_file_create(ROBO_FILE_DEVICE, &g_device_ops, 0, flags);
}
