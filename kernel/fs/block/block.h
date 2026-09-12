#ifndef ROBO_BLOCK_H
#define ROBO_BLOCK_H

#include <stdint.h>

typedef struct RoboBlockDevice {
    uint64_t block_count;
    uint32_t block_size;
    void *priv;
    int (*read)(struct RoboBlockDevice *dev, uint64_t lba, void *buf);
    int (*write)(struct RoboBlockDevice *dev, uint64_t lba, const void *buf);
    int (*flush)(struct RoboBlockDevice *dev);
    void (*destroy)(struct RoboBlockDevice *dev);
} RoboBlockDevice;

int robo_bdev_read(RoboBlockDevice *dev, uint64_t lba, void *buf);
int robo_bdev_write(RoboBlockDevice *dev, uint64_t lba, const void *buf);
int robo_bdev_flush(RoboBlockDevice *dev);
int robo_bdev_read_n(RoboBlockDevice *dev, uint64_t lba, uint64_t count,
                     void *buf);
int robo_bdev_write_n(RoboBlockDevice *dev, uint64_t lba, uint64_t count,
                      const void *buf);
void robo_bdev_destroy(RoboBlockDevice *dev);

RoboBlockDevice *robo_memdev_create(uint32_t block_size, uint64_t block_count);
uint8_t *robo_memdev_data(RoboBlockDevice *dev);

#endif
