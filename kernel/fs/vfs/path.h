#ifndef ROBO_VFS_PATH_H
#define ROBO_VFS_PATH_H

#include "fs/vfs/vfs_types.h"

int robo_path_valid(const char *path);
int robo_name_valid(const char *name);
int robo_path_next(const char **cursor, char *name, unsigned name_max);

#endif
