#ifndef ROBO_BLOCK_H
#define ROBO_BLOCK_H

#include <stdint.h>

typedef struct RoboBlockDevice {
    uint64_t blockCount;
    uint32_t blockSize;
    void *priv;
    int (*read)(struct RoboBlockDevice *dev, uint64_t lba, void *buf);
    int (*write)(struct RoboBlockDevice *dev, uint64_t lba, const void *buf);
    int (*flush)(struct RoboBlockDevice *dev);
    void (*destroy)(struct RoboBlockDevice *dev);
} RoboBlockDevice;

int BdevRead(RoboBlockDevice *dev, uint64_t lba, void *buf);
int BdevWrite(RoboBlockDevice *dev, uint64_t lba, const void *buf);
int BdevFlush(RoboBlockDevice *dev);
int BdevReadN(RoboBlockDevice *dev, uint64_t lba, uint64_t count,
                     void *buf);
int BdevWriteN(RoboBlockDevice *dev, uint64_t lba, uint64_t count,
                      const void *buf);
void BdevDestroy(RoboBlockDevice *dev);

RoboBlockDevice *MemdevCreate(uint32_t blockSize, uint64_t blockCount);
uint8_t *MemdevData(RoboBlockDevice *dev);

#endif
