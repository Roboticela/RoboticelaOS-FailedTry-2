#ifndef ROBO_VIRTIO_BLK_H
#define ROBO_VIRTIO_BLK_H

#include "fs/block/block.h"
#include "drivers/driver.h"

int VirtioBlkRegister(void);
RoboBlockDevice *VirtioBlkBdev(void);
int StorageSelftest(void);

#endif
