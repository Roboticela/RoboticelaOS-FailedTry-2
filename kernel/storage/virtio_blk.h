#ifndef ROBO_VIRTIO_BLK_H
#define ROBO_VIRTIO_BLK_H

#include "fs/block/block.h"
#include "drivers/driver.h"

int robo_virtio_blk_register(void);
RoboBlockDevice *robo_virtio_blk_bdev(void);
int robo_storage_selftest(void);

#endif
