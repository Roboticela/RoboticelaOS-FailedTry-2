#ifndef ROBO_AHCI_H
#define ROBO_AHCI_H

#include "drivers/driver.h"
#include "fs/block/block.h"

int AhciRegister(void);
unsigned AhciDiskCount(void);
RoboBlockDevice *AhciDiskAt(unsigned i);

#endif
