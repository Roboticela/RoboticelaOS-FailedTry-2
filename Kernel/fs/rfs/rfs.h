#ifndef ROBO_RFS_H
#define ROBO_RFS_H

#include "fs/vfs/vfs.h"
#include "fs/block/block.h"

int RobofsRegister(void);
int RobofsFormat(RoboBlockDevice *dev);
int RobofsMount(RoboBlockDevice *dev, uint32_t flags, RoboMount **out);
int RobofsUnmount(RoboMount *mount);

int RobofsSelftest(void);
#if !defined(PROCESS_HOST_TEST) && !defined(FD_HOST_TEST) && !defined(RFS_HOST_TEST)
void RobofsFbReport(void);
#endif

#endif
