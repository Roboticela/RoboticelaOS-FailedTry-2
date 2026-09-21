#ifndef ROBO_VFS_PATH_H
#define ROBO_VFS_PATH_H

#include "fs/vfs/vfs_types.h"

int PathValid(const char *path);
int NameValid(const char *name);
int PathNext(const char **cursor, char *name, unsigned nameMax);

#endif
