#include "fs/file.h"

#if defined(PROCESS_HOST_TEST) || defined(RFS_HOST_TEST)
#include <stdlib.h>
#define kmalloc malloc
#define kfree free
#else
#include "mm/heap/heap.h"
#endif

static uint64_t g_live;

static void file_zero(void *dst, unsigned n)
{
    unsigned char *p = (unsigned char *)dst;
    while (n--)
        *p++ = 0;
}

RoboFile *robo_file_create(RoboFileType type, const RoboFileOps *ops,
                           void *private_data, uint32_t flags)
{
    RoboFile *file;

    file = (RoboFile *)kmalloc(sizeof(*file));
    if (!file)
        return 0;
    file_zero(file, (unsigned)sizeof(*file));
    file->type = type;
    file->flags = flags;
    file->ops = ops;
    file->private_data = private_data;
    file->refcount = 1;
    file->offset = 0;
    g_live++;
    return file;
}

void robo_file_get(RoboFile *file)
{
    if (!file)
        return;
    file->refcount++;
}

void robo_file_put(RoboFile *file)
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
    if (g_live)
        g_live--;
    kfree(file);
}

uint32_t robo_file_refcount(const RoboFile *file)
{
    return file ? file->refcount : 0;
}

uint64_t robo_file_stat_live(void)
{
    return g_live;
}

const char *robo_file_type_name(RoboFileType type)
{
    switch (type) {
    case ROBO_FILE_REGULAR: return "REGULAR";
    case ROBO_FILE_DIRECTORY: return "DIRECTORY";
    case ROBO_FILE_PIPE: return "PIPE";
    case ROBO_FILE_SOCKET: return "SOCKET";
    case ROBO_FILE_DEVICE: return "DEVICE";
    case ROBO_FILE_CONSOLE: return "CONSOLE";
    default: return "UNKNOWN";
    }
}