#ifndef ROBO_FS_REGULAR_H
#define ROBO_FS_REGULAR_H

#include "fs/file.h"

RoboFile *FsFileOpen(const char *path, uint32_t flags);

#endif
