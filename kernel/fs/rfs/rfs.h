#ifndef ROBO_RFS_H
#define ROBO_RFS_H

#include "fs/vfs/vfs.h"
#include "fs/block/block.h"

int robofs_register(void);
int robofs_format(RoboBlockDevice *dev);
int robofs_mount(RoboBlockDevice *dev, uint32_t flags, RoboMount **out);
int robofs_unmount(RoboMount *mount);

int robofs_selftest(void);
#if !defined(PROCESS_HOST_TEST) && !defined(FD_HOST_TEST) && !defined(RFS_HOST_TEST)
void robofs_fb_report(void);
#endif

#endif
