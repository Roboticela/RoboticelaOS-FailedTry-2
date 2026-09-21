#include "fs/backends/device.h"
#include "syscall/syscall_errors.h"

static int64_t DeviceRead(RoboFile *file, void *buffer, size_t count)
{
    (void)file;
    (void)buffer;
    (void)count;
    return -(int64_t)RoboEnotsup;
}

static int64_t DeviceWrite(RoboFile *file, const void *buffer, size_t count)
{
    (void)file;
    (void)buffer;
    (void)count;
    return -(int64_t)RoboEnotsup;
}

static int DeviceIoctl(RoboFile *file, uint64_t request, void *arg)
{
    (void)file;
    (void)request;
    (void)arg;
    return -RoboEnotsup;
}

static const RoboFileOps gDeviceOps = {
    .read = DeviceRead,
    .write = DeviceWrite,
    .close = 0,
    .seek = 0,
    .ioctl = DeviceIoctl,
    .poll = 0,
};

RoboFile *DeviceFileCreate(uint32_t flags)
{
    return FileCreate(RoboFileDevice, &gDeviceOps, 0, flags);
}
