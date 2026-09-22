#ifndef ROBO_NVME_H
#define ROBO_NVME_H

#include "drivers/driver.h"
#include "fs/block/block.h"

int NvmeRegister(void);
unsigned NvmeDiskCount(void);
RoboBlockDevice *NvmeDiskAt(unsigned i);

#endif
