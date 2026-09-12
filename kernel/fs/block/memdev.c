#include "fs/block/block.h"
#include "syscall/syscall_errors.h"

#if defined(PROCESS_HOST_TEST) || defined(RFS_HOST_TEST)
#include <stdlib.h>
#define kmalloc malloc
#define kfree free
#else
#include "mm/heap/heap.h"
#endif

int robo_bdev_read(RoboBlockDevice *dev, uint64_t lba, void *buf)
{
    if (!dev || !dev->read || !buf)
        return -ROBO_EINVAL;
    if (lba >= dev->block_count)
        return -ROBO_EIO;
    return dev->read(dev, lba, buf);
}

int robo_bdev_write(RoboBlockDevice *dev, uint64_t lba, const void *buf)
{
    if (!dev || !dev->write || !buf)
        return -ROBO_EINVAL;
    if (lba >= dev->block_count)
        return -ROBO_EIO;
    return dev->write(dev, lba, buf);
}

int robo_bdev_flush(RoboBlockDevice *dev)
{
    if (!dev)
        return -ROBO_EINVAL;
    if (!dev->flush)
        return 0;
    return dev->flush(dev);
}

int robo_bdev_read_n(RoboBlockDevice *dev, uint64_t lba, uint64_t count,
                     void *buf)
{
    uint64_t i;
    unsigned char *p = (unsigned char *)buf;

    if (!dev || !buf)
        return -ROBO_EINVAL;
    if (!count)
        return 0;
    if (lba >= dev->block_count || count > dev->block_count - lba)
        return -ROBO_EIO;
    for (i = 0; i < count; i++) {
        int r = robo_bdev_read(dev, lba + i, p + i * dev->block_size);
        if (r != 0)
            return r;
    }
    return 0;
}

int robo_bdev_write_n(RoboBlockDevice *dev, uint64_t lba, uint64_t count,
                      const void *buf)
{
    uint64_t i;
    const unsigned char *p = (const unsigned char *)buf;

    if (!dev || !buf)
        return -ROBO_EINVAL;
    if (!count)
        return 0;
    if (lba >= dev->block_count || count > dev->block_count - lba)
        return -ROBO_EIO;
    for (i = 0; i < count; i++) {
        int r = robo_bdev_write(dev, lba + i, p + i * dev->block_size);
        if (r != 0)
            return r;
    }
    return 0;
}

void robo_bdev_destroy(RoboBlockDevice *dev)
{
    if (!dev)
        return;
    if (dev->destroy)
        dev->destroy(dev);
}

typedef struct {
    uint8_t *data;
    uint32_t bs;
    uint64_t n;
} MemDev;

static void mem_copy(void *dst, const void *src, unsigned n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
        *d++ = *s++;
}

static int mem_read(RoboBlockDevice *dev, uint64_t lba, void *buf)
{
    MemDev *m = (MemDev *)dev->priv;
    mem_copy(buf, m->data + lba * m->bs, m->bs);
    return 0;
}

static int mem_write(RoboBlockDevice *dev, uint64_t lba, const void *buf)
{
    MemDev *m = (MemDev *)dev->priv;
    mem_copy(m->data + lba * m->bs, buf, m->bs);
    return 0;
}

static void mem_destroy(RoboBlockDevice *dev)
{
    MemDev *m;

    if (!dev)
        return;
    m = (MemDev *)dev->priv;
    if (m) {
        kfree(m->data);
        kfree(m);
    }
    kfree(dev);
}

RoboBlockDevice *robo_memdev_create(uint32_t block_size, uint64_t block_count)
{
    RoboBlockDevice *dev;
    MemDev *m;
    uint64_t bytes, i;

    if (!block_size || !block_count)
        return 0;
    if (block_count > (0xFFFFFFFFFFFFFFFFull / block_size))
        return 0;
    bytes = block_count * block_size;
    m = (MemDev *)kmalloc(sizeof(*m));
    if (!m)
        return 0;
    m->data = (uint8_t *)kmalloc((unsigned)bytes);
    if (!m->data) {
        kfree(m);
        return 0;
    }
    for (i = 0; i < bytes; i++)
        m->data[i] = 0;
    m->bs = block_size;
    m->n = block_count;
    dev = (RoboBlockDevice *)kmalloc(sizeof(*dev));
    if (!dev) {
        kfree(m->data);
        kfree(m);
        return 0;
    }
    dev->block_count = block_count;
    dev->block_size = block_size;
    dev->priv = m;
    dev->read = mem_read;
    dev->write = mem_write;
    dev->flush = 0;
    dev->destroy = mem_destroy;
    return dev;
}

uint8_t *robo_memdev_data(RoboBlockDevice *dev)
{
    MemDev *m;

    if (!dev || !dev->priv)
        return 0;
    m = (MemDev *)dev->priv;
    return m->data;
}
