#ifndef ROBO_FS_CONSOLE_H
#define ROBO_FS_CONSOLE_H

#include "fs/file.h"
#include "fs/fd_table.h"

RoboFile *ConsoleOpen(uint32_t flags);
int ConsoleInstallStdio(RoboFDTable *table);

#endif
