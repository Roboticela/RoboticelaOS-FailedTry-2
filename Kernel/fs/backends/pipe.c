#include "fs/backends/pipe.h"
#include "syscall/syscall_errors.h"

static int64_t PipeRead(RoboFile *file, void *buffer, size_t count)
{
    (void)file;
    (void)buffer;
    (void)count;
    return -(int64_t)RoboEnotsup;
}

static int64_t PipeWrite(RoboFile *file, const void *buffer, size_t count)
{
    (void)file;
    (void)buffer;
    (void)count;
    return -(int64_t)RoboEnotsup;
}

static const RoboFileOps gPipeOps = {
    .read = PipeRead,
    .write = PipeWrite,
    .close = 0,
    .seek = 0,
    .ioctl = 0,
    .poll = 0,
};

RoboFile *PipeFileCreate(uint32_t flags)
{
    return FileCreate(RoboFilePipe, &gPipeOps, 0, flags);
}
