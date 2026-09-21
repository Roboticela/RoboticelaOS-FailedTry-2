#include "fs/backends/regular.h"
#include "fs/robofs.h"
#include "syscall/syscall_errors.h"
#include "mm/heap/heap.h"

typedef struct {
    const unsigned char *data;
    uint32_t size;
} RoboFsFilePriv;

static int64_t RegularRead(RoboFile *file, void *buffer, size_t count)
{
    RoboFsFilePriv *priv;
    uint32_t left;
    size_t n, i;
    unsigned char *dst;

    if (!file || !file->privateData)
        return -(int64_t)RoboEbadf;
    if (!count)
        return 0;
    if (!buffer)
        return -(int64_t)RoboEfault;
    priv = (RoboFsFilePriv *)file->privateData;
    if (file->offset >= priv->size)
        return 0;
    left = priv->size - (uint32_t)file->offset;
    n = (count > left) ? (size_t)left : count;
    dst = (unsigned char *)buffer;
    for (i = 0; i < n; i++)
        dst[i] = priv->data[file->offset + i];
    file->offset += n;
    return (int64_t)n;
}

static int64_t RegularWrite(RoboFile *file, const void *buffer, size_t count)
{
    (void)file;
    (void)buffer;
    (void)count;
    return -(int64_t)RoboEnotsup;
}

static int RegularClose(RoboFile *file)
{
    if (file && file->privateData) {
        kfree(file->privateData);
        file->privateData = 0;
    }
    return 0;
}

static int RegularSeek(RoboFile *file, int64_t offset, int whence)
{
    RoboFsFilePriv *priv;
    int64_t base, next;

    if (!file || !file->privateData)
        return -RoboEbadf;
    priv = (RoboFsFilePriv *)file->privateData;
    if (whence == RoboSeekSet)
        base = 0;
    else if (whence == RoboSeekCur)
        base = (int64_t)file->offset;
    else if (whence == RoboSeekEnd)
        base = (int64_t)priv->size;
    else
        return -RoboEinval;
    next = base + offset;
    if (next < 0)
        return -RoboEinval;
    file->offset = (uint64_t)next;
    return 0;
}

static const RoboFileOps gRegularOps = {
    .read = RegularRead,
    .write = RegularWrite,
    .close = RegularClose,
    .seek = RegularSeek,
    .ioctl = 0,
    .poll = 0,
};

RoboFile *FsFileOpen(const char *path, uint32_t flags)
{
    const void *data = 0;
    uint32_t size = 0;
    RoboFsFilePriv *priv;
    RoboFile *file;

    if (!path)
        return 0;
    if (flags & RoboFflagWrite)
        return 0;
    if (!FsOpen(path, &data, &size) || !data)
        return 0;
    priv = (RoboFsFilePriv *)kmalloc(sizeof(*priv));
    if (!priv)
        return 0;
    priv->data = (const unsigned char *)data;
    priv->size = size;
    file = FileCreate(RoboFileRegular, &gRegularOps, priv,
                            RoboFflagRead);
    if (!file) {
        kfree(priv);
        return 0;
    }
    return file;
}
