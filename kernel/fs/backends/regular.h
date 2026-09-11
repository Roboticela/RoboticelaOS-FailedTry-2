#ifndef ROBO_FS_REGULAR_H
#define ROBO_FS_REGULAR_H

#include "fs/file.h"

RoboFile *robo_fs_file_open(const char *path, uint32_t flags);

#endif
