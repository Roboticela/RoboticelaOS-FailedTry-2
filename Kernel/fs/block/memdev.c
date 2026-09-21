#include "fs/block/block.h"
#include "syscall/syscall_errors.h"

#if defined(PROCESS_HOST_TEST) || defined(RFS_HOST_TEST)
#include <stdlib.h>
#define kmalloc malloc
#define kfree free
#else
#include "mm/heap/heap.h"
#endif

int BdevRead(RoboBlockDevice *dev, uint64_t lba, void *buf)
{
    if (!dev || !dev->read || !buf)
        return -RoboEinval;
    if (lba >= dev->blockCount)
        return -RoboEio;
    return dev->read(dev, lba, buf);
}

int BdevWrite(RoboBlockDevice *dev, uint64_t lba, const void *buf)
{
    if (!dev || !dev->write || !buf)
        return -RoboEinval;
    if (lba >= dev->blockCount)
        return -RoboEio;
    return dev->write(dev, lba, buf);
}

int BdevFlush(RoboBlockDevice *dev)
{
    if (!dev)
        return -RoboEinval;
    if (!dev->flush)
        return 0;
    return dev->flush(dev);
}

int BdevReadN(RoboBlockDevice *dev, uint64_t lba, uint64_t count,
                     void *buf)
{
    uint64_t i;
    unsigned char *p = (unsigned char *)buf;

    if (!dev || !buf)
        return -RoboEinval;
    if (!count)
        return 0;
    if (lba >= dev->blockCount || count > dev->blockCount - lba)
        return -RoboEio;
    for (i = 0; i < count; i++) {
        int r = BdevRead(dev, lba + i, p + i * dev->blockSize);
        if (r != 0)
            return r;
    }
    return 0;
}

int BdevWriteN(RoboBlockDevice *dev, uint64_t lba, uint64_t count,
                      const void *buf)
{
    uint64_t i;
    const unsigned char *p = (const unsigned char *)buf;

    if (!dev || !buf)
        return -RoboEinval;
    if (!count)
        return 0;
    if (lba >= dev->blockCount || count > dev->blockCount - lba)
        return -RoboEio;
    for (i = 0; i < count; i++) {
        int r = BdevWrite(dev, lba + i, p + i * dev->blockSize);
        if (r != 0)
            return r;
    }
    return 0;
}

void BdevDestroy(RoboBlockDevice *dev)
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

static void RoboMemCopy(void *dst, const void *src, unsigned n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
        *d++ = *s++;
}

static int MemRead(RoboBlockDevice *dev, uint64_t lba, void *buf)
{
    MemDev *m = (MemDev *)dev->priv;
    RoboMemCopy(buf, m->data + lba * m->bs, m->bs);
    return 0;
}

static int MemWrite(RoboBlockDevice *dev, uint64_t lba, const void *buf)
{
    MemDev *m = (MemDev *)dev->priv;
    RoboMemCopy(m->data + lba * m->bs, buf, m->bs);
    return 0;
}

static void MemDestroy(RoboBlockDevice *dev)
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

RoboBlockDevice *MemdevCreate(uint32_t blockSize, uint64_t blockCount)
{
    RoboBlockDevice *dev;
    MemDev *m;
    uint64_t bytes, i;

    if (!blockSize || !blockCount)
        return 0;
    if (blockCount > (0xFFFFFFFFFFFFFFFFull / blockSize))
        return 0;
    bytes = blockCount * blockSize;
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
    m->bs = blockSize;
    m->n = blockCount;
    dev = (RoboBlockDevice *)kmalloc(sizeof(*dev));
    if (!dev) {
        kfree(m->data);
        kfree(m);
        return 0;
    }
    dev->blockCount = blockCount;
    dev->blockSize = blockSize;
    dev->priv = m;
    dev->read = MemRead;
    dev->write = MemWrite;
    dev->flush = 0;
    dev->destroy = MemDestroy;
    return dev;
}

uint8_t *MemdevData(RoboBlockDevice *dev)
{
    MemDev *m;

    if (!dev || !dev->priv)
        return 0;
    m = (MemDev *)dev->priv;
    return m->data;
}
