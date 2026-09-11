#ifndef ROBO_FS_DEVICE_H
#define ROBO_FS_DEVICE_H

#include "fs/file.h"

/*
 * Device backend placeholder. Individual devices are not hardcoded here.
 * Operations return ENOTSUP until a real device subsystem exists.
 */
RoboFile *robo_device_file_create(uint32_t flags);

#endif
