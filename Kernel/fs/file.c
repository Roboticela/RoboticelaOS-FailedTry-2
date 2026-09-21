#include "fs/file.h"

#if defined(PROCESS_HOST_TEST) || defined(RFS_HOST_TEST)
#include <stdlib.h>
#define kmalloc malloc
#define kfree free
#else
#include "mm/heap/heap.h"
#endif

static uint64_t gLive;

static void FileZero(void *dst, unsigned n)
{
    unsigned char *p = (unsigned char *)dst;
    while (n--)
        *p++ = 0;
}

RoboFile *FileCreate(RoboFileType type, const RoboFileOps *ops,
                           void *privateData, uint32_t flags)
{
    RoboFile *file;

    file = (RoboFile *)kmalloc(sizeof(*file));
    if (!file)
        return 0;
    FileZero(file, (unsigned)sizeof(*file));
    file->type = type;
    file->flags = flags;
    file->ops = ops;
    file->privateData = privateData;
    file->refcount = 1;
    file->offset = 0;
    gLive++;
    return file;
}

void FileGet(RoboFile *file)
{
    if (!file)
        return;
    file->refcount++;
}

void FilePut(RoboFile *file)
{
    if (!file)
        return;
    if (!file->refcount)
        return;
    file->refcount--;
    if (file->refcount)
        return;
    if (file->ops && file->ops->close)
        file->ops->close(file);
    if (gLive)
        gLive--;
    kfree(file);
}

uint32_t FileRefcount(const RoboFile *file)
{
    return file ? file->refcount : 0;
}

uint64_t FileStatLive(void)
{
    return gLive;
}

const char *FileTypeName(RoboFileType type)
{
    switch (type) {
    case RoboFileRegular: return "REGULAR";
    case RoboFileDirectory: return "DIRECTORY";
    case RoboFilePipe: return "PIPE";
    case RoboFileSocket: return "SOCKET";
    case RoboFileDevice: return "DEVICE";
    case RoboFileConsole: return "CONSOLE";
    default: return "UNKNOWN";
    }
}