#ifndef ROBO_FS_CONSOLE_H
#define ROBO_FS_CONSOLE_H

#include "fs/file.h"
#include "fs/fd_table.h"

RoboFile *robo_console_open(uint32_t flags);
int robo_console_install_stdio(RoboFDTable *table);

#endif
