#include "fs/backends/regular.h"
#include "fs/robofs.h"
#include "syscall/syscall_errors.h"
#include "mm/heap/heap.h"

typedef struct {
    const unsigned char *data;
    uint32_t size;
} RoboFsFilePriv;

static int64_t regular_read(RoboFile *file, void *buffer, size_t count)
{
    RoboFsFilePriv *priv;
    uint32_t left;
    size_t n, i;
    unsigned char *dst;

    if (!file || !file->private_data)
        return -(int64_t)ROBO_EBADF;
    if (!count)
        return 0;
    if (!buffer)
        return -(int64_t)ROBO_EFAULT;
    priv = (RoboFsFilePriv *)file->private_data;
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

static int64_t regular_write(RoboFile *file, const void *buffer, size_t count)
{
    (void)file;
    (void)buffer;
    (void)count;
    return -(int64_t)ROBO_ENOTSUP;
}

static int regular_close(RoboFile *file)
{
    if (file && file->private_data) {
        kfree(file->private_data);
        file->private_data = 0;
    }
    return 0;
}

static int regular_seek(RoboFile *file, int64_t offset, int whence)
{
    RoboFsFilePriv *priv;
    int64_t base, next;

    if (!file || !file->private_data)
        return -ROBO_EBADF;
    priv = (RoboFsFilePriv *)file->private_data;
    if (whence == ROBO_SEEK_SET)
        base = 0;
    else if (whence == ROBO_SEEK_CUR)
        base = (int64_t)file->offset;
    else if (whence == ROBO_SEEK_END)
        base = (int64_t)priv->size;
    else
        return -ROBO_EINVAL;
    next = base + offset;
    if (next < 0)
        return -ROBO_EINVAL;
    file->offset = (uint64_t)next;
    return 0;
}

static const RoboFileOps g_regular_ops = {
    .read = regular_read,
    .write = regular_write,
    .close = regular_close,
    .seek = regular_seek,
    .ioctl = 0,
    .poll = 0,
};

RoboFile *robo_fs_file_open(const char *path, uint32_t flags)
{
    const void *data = 0;
    uint32_t size = 0;
    RoboFsFilePriv *priv;
    RoboFile *file;

    if (!path)
        return 0;
    if (flags & ROBO_FFLAG_WRITE)
        return 0;
    if (!robo_fs_open(path, &data, &size) || !data)
        return 0;
    priv = (RoboFsFilePriv *)kmalloc(sizeof(*priv));
    if (!priv)
        return 0;
    priv->data = (const unsigned char *)data;
    priv->size = size;
    file = robo_file_create(ROBO_FILE_REGULAR, &g_regular_ops, priv,
                            ROBO_FFLAG_READ);
    if (!file) {
        kfree(priv);
        return 0;
    }
    return file;
}
