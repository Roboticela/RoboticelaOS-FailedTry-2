#include "fs/backends/console.h"
#include "syscall/syscall_errors.h"

#ifdef PROCESS_HOST_TEST
static void ConsoleWriteBytes(const void *buffer, size_t count)
{
    (void)buffer;
    (void)count;
}
#else
#include "kio.h"
static void ConsoleWriteBytes(const void *buffer, size_t count)
{
    kwrite((const char *)buffer, (unsigned)count);
}
#endif

static int64_t ConsoleRead(RoboFile *file, void *buffer, size_t count)
{
    (void)file;
    (void)buffer;
    (void)count;
    return -(int64_t)RoboEnotsup;
}

static int64_t ConsoleWrite(RoboFile *file, const void *buffer, size_t count)
{
    (void)file;
    if (!count)
        return 0;
    if (!buffer)
        return -(int64_t)RoboEfault;
    ConsoleWriteBytes(buffer, count);
    return (int64_t)count;
}

static int ConsoleClose(RoboFile *file)
{
    (void)file;
    return 0;
}

static const RoboFileOps gConsoleOps = {
    .read = ConsoleRead,
    .write = ConsoleWrite,
    .close = ConsoleClose,
    .seek = 0,
    .ioctl = 0,
    .poll = 0,
};

RoboFile *ConsoleOpen(uint32_t flags)
{
    return FileCreate(RoboFileConsole, &gConsoleOps, 0, flags);
}

int ConsoleInstallStdio(RoboFDTable *table)
{
    RoboFile *in, *out, *err;
    Fd fd0, fd1, fd2;

    if (!table)
        return -1;
    in = ConsoleOpen(RoboFflagRead);
    out = ConsoleOpen(RoboFflagWrite);
    err = ConsoleOpen(RoboFflagWrite);
    if (!in || !out || !err) {
        if (in)
            FilePut(in);
        if (out)
            FilePut(out);
        if (err)
            FilePut(err);
        return -1;
    }
    fd0 = FdTableAlloc(table, in, 0);
    fd1 = FdTableAlloc(table, out, 0);
    fd2 = FdTableAlloc(table, err, 0);
    FilePut(in);
    FilePut(out);
    FilePut(err);
    if (fd0 != RoboFdStdin || fd1 != RoboFdStdout || fd2 != RoboFdStderr)
        return -1;
    return 0;
}
