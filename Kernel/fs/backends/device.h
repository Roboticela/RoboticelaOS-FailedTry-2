#ifndef ROBO_FS_DEVICE_H
#define ROBO_FS_DEVICE_H

#include "fs/file.h"

/*
 * Device backend placeholder. Individual devices are not hardcoded here.
 * Operations return ENOTSUP until a real device subsystem exists.
 */
RoboFile *DeviceFileCreate(uint32_t flags);

#endif
